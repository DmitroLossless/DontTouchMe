#include "TMMenuViewerMeshTransitionSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gun/Gun.h"
#include "Kismet/GameplayStatics.h"
#include "TMAudioEnvelopeFollower.h"
#include "TouchMe.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogTMMenuViewerMeshTransition, Log, All);

namespace
{
static TAutoConsoleVariable<int32> CVarMenuViewerMeshTransition(
	TEXT("tm.MenuViewerMeshTransition"),
	1,
	TEXT("Enables BP_MenuViewer mesh tracking and automatic body mesh cycling."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarMenuViewerMeshTransitionAutoCycle(
	TEXT("tm.MenuViewerMeshTransition.AutoCycle"),
	1,
	TEXT("Automatically cycles BP_MenuViewer body meshes so the transition is visible in the main menu."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMenuViewerMeshTransitionAutoCycleInterval(
	TEXT("tm.MenuViewerMeshTransition.AutoCycleInterval"),
	10.0f,
	TEXT("Minimum seconds between automatic BP_MenuViewer body mesh changes."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarMenuViewerMeshTransitionBeatSync(
	TEXT("tm.MenuViewerMeshTransition.BeatSync"),
	1,
	TEXT("Snaps automatic BP_MenuViewer body mesh changes to the active main menu soundtrack beat."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarMenuViewerMeshTransitionBeatSyncDownbeat(
	TEXT("tm.MenuViewerMeshTransition.BeatSyncDownbeat"),
	1,
	TEXT("When beat sync is enabled, use downbeats instead of every beat for menu body mesh changes."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMenuViewerMeshTransitionBeatSyncWindow(
	TEXT("tm.MenuViewerMeshTransition.BeatSyncWindow"),
	0.75f,
	TEXT("Seconds after a detected beat that may be used for a beat-synced menu body mesh change."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMenuViewerMeshTransitionBeatSyncPulseThreshold(
	TEXT("tm.MenuViewerMeshTransition.BeatSyncPulseThreshold"),
	1.0f,
	TEXT("Normalized envelope threshold used as a fallback rhythm pulse when beat events are sparse."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarLoadoutFOV(
	TEXT("tm.LoadoutFOV"),
	1,
	TEXT("Locks the player camera FOV while W_Loadout or W_Attachments is visible."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutFOVAngle(
	TEXT("tm.LoadoutFOV.Angle"),
	90.0f,
	TEXT("Player camera FOV used while the loadout UI is visible."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMenuFOVAngle(
	TEXT("tm.MenuFOV.Angle"),
	82.0f,
	TEXT("Player camera FOV used by default while the main menu UI is visible."),
	ECVF_Default);

static TWeakObjectPtr<USkeletalMesh> GLastMenuViewerVestMesh;

float GetMenuFOVAngle()
{
	return FMath::Clamp(CVarMenuFOVAngle.GetValueOnGameThread(), 5.0f, 170.0f);
}
}

void UTMMenuViewerMeshTransitionSubsystem::Deinitialize()
{
	RestoreLoadoutFOV();
	RestoreMenuFOV();
	MenuViewerStates.Empty();
	Super::Deinitialize();
}

void UTMMenuViewerMeshTransitionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const bool bLoadoutFOVVisible =
		CVarLoadoutFOV.GetValueOnGameThread() != 0 && IsLoadoutFOVVisible(World);
	const bool bMainMenuVisible = IsMainMenuVisible(World);
	UpdateMenuFOV(World, bMainMenuVisible, bLoadoutFOVVisible);
	UpdateLoadoutFOV(World, bLoadoutFOVVisible);

	if (CVarMenuViewerMeshTransition.GetValueOnGameThread() == 0)
	{
		return;
	}

	const bool bLoadoutPreviewVisible = IsLoadoutPreviewVisible(World);

	for (auto It = MenuViewerStates.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (IsMenuViewerActor(Actor))
		{
			TrackMenuViewer(Actor, DeltaTime, bLoadoutPreviewVisible);
		}
	}
}

TStatId UTMMenuViewerMeshTransitionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTMMenuViewerMeshTransitionSubsystem, STATGROUP_Tickables);
}

bool UTMMenuViewerMeshTransitionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

void UTMMenuViewerMeshTransitionSubsystem::TrackMenuViewer(
	AActor* Actor,
	const float DeltaTime,
	const bool bLoadoutPreviewVisible)
{
	if (!Actor)
	{
		return;
	}

	FMenuViewerState& State = MenuViewerStates.FindOrAdd(Actor);
	State.Actor = Actor;
	UpdateAttachedWeaponVisibility(Actor, State, bLoadoutPreviewVisible);

	USkeletalMeshComponent* VestComponent = ReadMeshComponentProperty(Actor, TEXT("Vest"));
	if (!VestComponent || !VestComponent->IsRegistered())
	{
		return;
	}

	USkeletalMesh* CurrentMesh = VestComponent->GetSkeletalMeshAsset();
	if (!CurrentMesh)
	{
		return;
	}

	State.VestComponent = VestComponent;
	const FBeatSyncSnapshot BeatSync = FindActiveBeatSync(Actor->GetWorld());

	USkeletalMeshComponent* LeaderComponent = ResolveLeaderComponent(Actor, VestComponent);
	if (!LeaderComponent)
	{
		CaptureStableState(State, VestComponent);
		return;
	}

	const bool bHasTrackedMesh = State.LastMesh.IsValid();
	if (bHasTrackedMesh && State.LastMesh.Get() != CurrentMesh)
	{
		NoteMeshChanged(State, VestComponent, State.LastMesh.Get());
		CaptureStableState(State, VestComponent);
		return;
	}

	if (!bHasTrackedMesh)
	{
		if (GLastMenuViewerVestMesh.IsValid() && GLastMenuViewerVestMesh.Get() != CurrentMesh)
		{
			NoteMeshChanged(State, VestComponent, GLastMenuViewerVestMesh.Get());
		}
		CaptureStableState(State, VestComponent);
		return;
	}

	CaptureStableState(State, VestComponent);
	UpdateAutoCycle(State, VestComponent, LeaderComponent, DeltaTime, BeatSync);
}

void UTMMenuViewerMeshTransitionSubsystem::NoteMeshChanged(
	const FMenuViewerState& State,
	USkeletalMeshComponent* VestComponent,
	USkeletalMesh* PreviousMesh) const
{
	if (!VestComponent || !PreviousMesh)
	{
		return;
	}

	UE_LOG(
		LogTMMenuViewerMeshTransition,
		Display,
		TEXT("Menu mesh changed: %s -> %s on %s"),
		*GetNameSafe(PreviousMesh),
		*GetNameSafe(VestComponent->GetSkeletalMeshAsset()),
		*GetNameSafe(State.Actor.Get()));
}

void UTMMenuViewerMeshTransitionSubsystem::CaptureStableState(FMenuViewerState& State, USkeletalMeshComponent* VestComponent)
{
	if (!VestComponent)
	{
		return;
	}

	State.LastMesh = VestComponent->GetSkeletalMeshAsset();
	GLastMenuViewerVestMesh = State.LastMesh;
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateAutoCycle(
	FMenuViewerState& State,
	USkeletalMeshComponent* VestComponent,
	USkeletalMeshComponent* LeaderComponent,
	const float DeltaTime,
	const FBeatSyncSnapshot& BeatSync)
{
	if (!IsAutoCycleEnabled() || !VestComponent || !LeaderComponent)
	{
		return;
	}

	State.AutoCycleElapsedSeconds += FMath::Max(0.f, DeltaTime);
	if (State.AutoCycleElapsedSeconds < GetAutoCycleInterval())
	{
		UpdateRhythmPulseMemory(State, BeatSync);
		return;
	}

	UWorld* World = VestComponent->GetWorld();
	if (IsBeatSyncEnabled() && BeatSync.bHasActiveFollower)
	{
		const float WorldTimeSeconds = World ? World->GetTimeSeconds() : 0.0f;
		if (!IsReadyForBeatSyncedCycle(State, BeatSync, WorldTimeSeconds))
		{
			return;
		}
	}

	USkeletalMesh* PreviousMesh = VestComponent->GetSkeletalMeshAsset();
	USkeletalMesh* NextMesh = ChooseNextMenuMesh(PreviousMesh);
	if (!PreviousMesh || !NextMesh || PreviousMesh == NextMesh)
	{
		return;
	}

	VestComponent->SetSkeletalMeshAsset(NextMesh);
	State.AutoCycleElapsedSeconds = 0.f;
	NoteMeshChanged(State, VestComponent, PreviousMesh);
	CaptureStableState(State, VestComponent);

	UE_LOG(
		LogTMMenuViewerMeshTransition,
		Display,
		TEXT("Auto-cycled menu mesh: %s -> %s Beat=%d Strength=%.2f Env=%.2f"),
		*GetNameSafe(PreviousMesh),
		*GetNameSafe(NextMesh),
		BeatSync.BeatIndex,
		BeatSync.LastBeatStrength,
		BeatSync.NormalizedEnvelopeValue);
}

bool UTMMenuViewerMeshTransitionSubsystem::IsMenuViewerActor(const AActor* Actor)
{
	const UClass* ActorClass = Actor ? Actor->GetClass() : nullptr;
	return ActorClass && ActorClass->GetPathName().Contains(TEXT("BP_MenuViewer"));
}

bool UTMMenuViewerMeshTransitionSubsystem::IsAttachedWeaponActor(const AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}

	if (Actor->IsA(AGun::StaticClass()))
	{
		return true;
	}

	const UClass* ActorClass = Actor->GetClass();
	const FString ActorPath = Actor->GetPathName();
	const FString ClassPath = ActorClass ? ActorClass->GetPathName() : FString();
	return ActorPath.Contains(TEXT("/Weapons/")) || ClassPath.Contains(TEXT("/Weapons/"));
}

bool UTMMenuViewerMeshTransitionSubsystem::IsLoadoutFOVVisible(UWorld* World)
{
	if (!World)
	{
		return false;
	}

	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->GetWorld() != World)
		{
			continue;
		}

		const UClass* WidgetClass = Widget->GetClass();
		const FString ClassName = WidgetClass ? WidgetClass->GetName() : FString();
		if (!ClassName.Contains(TEXT("W_Loadout")) && !ClassName.Contains(TEXT("W_Attachments")))
		{
			continue;
		}

		if (!Widget->IsVisible())
		{
			continue;
		}

		const ESlateVisibility Visibility = Widget->GetVisibility();
		if (Visibility == ESlateVisibility::Collapsed || Visibility == ESlateVisibility::Hidden)
		{
			continue;
		}

		if (Widget->IsInViewport())
		{
			return true;
		}

		const FVector2D DrawnSize = Widget->GetCachedGeometry().GetLocalSize();
		if (DrawnSize.X > 16.0f && DrawnSize.Y > 16.0f)
		{
			return true;
		}
	}

	return false;
}

bool UTMMenuViewerMeshTransitionSubsystem::IsMainMenuVisible(UWorld* World)
{
	if (!World)
	{
		return false;
	}

	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->GetWorld() != World)
		{
			continue;
		}

		const UClass* WidgetClass = Widget->GetClass();
		if (!WidgetClass || !WidgetClass->GetPathName().Contains(TEXT("W_MainMenu")))
		{
			continue;
		}

		if (!Widget->IsVisible())
		{
			continue;
		}

		const ESlateVisibility Visibility = Widget->GetVisibility();
		if (Visibility == ESlateVisibility::Collapsed || Visibility == ESlateVisibility::Hidden)
		{
			continue;
		}

		if (Widget->IsInViewport())
		{
			return true;
		}

		const FVector2D DrawnSize = Widget->GetCachedGeometry().GetLocalSize();
		if (DrawnSize.X > 16.0f && DrawnSize.Y > 16.0f)
		{
			return true;
		}
	}

	return false;
}

bool UTMMenuViewerMeshTransitionSubsystem::IsLoadoutPreviewVisible(UWorld* World)
{
	if (!World)
	{
		return false;
	}

	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!Widget || Widget->HasAnyFlags(RF_ClassDefaultObject))
		{
			continue;
		}

		const UClass* WidgetClass = Widget->GetClass();
		if (!WidgetClass || !WidgetClass->GetPathName().Contains(TEXT("W_Loadout")))
		{
			continue;
		}

		if (Widget->GetWorld() != World)
		{
			continue;
		}

		const ESlateVisibility Visibility = Widget->GetVisibility();
		if (Visibility == ESlateVisibility::Collapsed || Visibility == ESlateVisibility::Hidden)
		{
			continue;
		}

		const FObjectPropertyBase* ActiveWeaponProperty =
			FindFProperty<FObjectPropertyBase>(WidgetClass, TEXT("ActiveWeapon"));
		if (!ActiveWeaponProperty)
		{
			continue;
		}

		if (IsValid(Cast<AActor>(ActiveWeaponProperty->GetObjectPropertyValue_InContainer(Widget))))
		{
			return true;
		}
	}

	return false;
}

APlayerCameraManager* UTMMenuViewerMeshTransitionSubsystem::ResolvePlayerCameraManager(UWorld* World)
{
	return World ? UGameplayStatics::GetPlayerCameraManager(World, 0) : nullptr;
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateAttachedWeaponVisibility(
	AActor* Actor,
	FMenuViewerState& State,
	const bool bHide)
{
	if (bHide)
	{
		HideAttachedWeaponActors(Actor, State);
	}
	else
	{
		RestoreAttachedWeaponActors(State);
	}
}

void UTMMenuViewerMeshTransitionSubsystem::HideAttachedWeaponActors(AActor* Actor, FMenuViewerState& State)
{
	if (!Actor)
	{
		return;
	}

	TArray<AActor*> AttachedActors;
	Actor->GetAttachedActors(AttachedActors, true, true);
	for (AActor* AttachedActor : AttachedActors)
	{
		if (!IsAttachedWeaponActor(AttachedActor))
		{
			continue;
		}

		if (!State.AttachedWeaponActorHiddenStates.Contains(AttachedActor))
		{
			State.AttachedWeaponActorHiddenStates.Add(AttachedActor, AttachedActor->IsHidden());
		}
		AttachedActor->SetActorHiddenInGame(true);

		TArray<UPrimitiveComponent*> PrimitiveComponents;
		AttachedActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
		for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			if (!PrimitiveComponent)
			{
				continue;
			}

			if (!State.AttachedWeaponComponentVisibilityStates.Contains(PrimitiveComponent))
			{
				FComponentVisibilityState VisibilityState;
				VisibilityState.bVisible = PrimitiveComponent->IsVisible();
				VisibilityState.bHiddenInGame = PrimitiveComponent->bHiddenInGame;
				State.AttachedWeaponComponentVisibilityStates.Add(PrimitiveComponent, VisibilityState);
			}

			PrimitiveComponent->SetVisibility(false, true);
			PrimitiveComponent->SetHiddenInGame(true, true);
		}
	}
}

void UTMMenuViewerMeshTransitionSubsystem::RestoreAttachedWeaponActors(FMenuViewerState& State)
{
	for (const TPair<TWeakObjectPtr<AActor>, bool>& ActorState : State.AttachedWeaponActorHiddenStates)
	{
		if (AActor* Actor = ActorState.Key.Get())
		{
			Actor->SetActorHiddenInGame(ActorState.Value);
		}
	}
	State.AttachedWeaponActorHiddenStates.Empty();

	for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, FComponentVisibilityState>& ComponentState :
		State.AttachedWeaponComponentVisibilityStates)
	{
		if (UPrimitiveComponent* PrimitiveComponent = ComponentState.Key.Get())
		{
			PrimitiveComponent->SetVisibility(ComponentState.Value.bVisible, true);
			PrimitiveComponent->SetHiddenInGame(ComponentState.Value.bHiddenInGame, true);
		}
	}
	State.AttachedWeaponComponentVisibilityStates.Empty();
}

USkeletalMeshComponent* UTMMenuViewerMeshTransitionSubsystem::ReadMeshComponentProperty(UObject* Object, const FName PropertyName)
{
	if (!Object)
	{
		return nullptr;
	}

	const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Object->GetClass(), PropertyName);
	if (!Property)
	{
		return nullptr;
	}

	return Cast<USkeletalMeshComponent>(Property->GetObjectPropertyValue_InContainer(Object));
}

USkeletalMeshComponent* UTMMenuViewerMeshTransitionSubsystem::ResolveLeaderComponent(AActor* Actor, USkeletalMeshComponent* VestComponent)
{
	if (USkeletalMeshComponent* LeaderComponent = VestComponent ? Cast<USkeletalMeshComponent>(VestComponent->LeaderPoseComponent.Get()) : nullptr)
	{
		return LeaderComponent;
	}

	return ReadMeshComponentProperty(Actor, TEXT("Character"));
}

bool UTMMenuViewerMeshTransitionSubsystem::IsAutoCycleEnabled()
{
	return CVarMenuViewerMeshTransitionAutoCycle.GetValueOnGameThread() != 0;
}

bool UTMMenuViewerMeshTransitionSubsystem::IsBeatSyncEnabled()
{
	return CVarMenuViewerMeshTransitionBeatSync.GetValueOnGameThread() != 0;
}

bool UTMMenuViewerMeshTransitionSubsystem::ShouldUseDownbeat()
{
	return CVarMenuViewerMeshTransitionBeatSyncDownbeat.GetValueOnGameThread() != 0;
}

float UTMMenuViewerMeshTransitionSubsystem::GetAutoCycleInterval()
{
	return FMath::Max(1.f, CVarMenuViewerMeshTransitionAutoCycleInterval.GetValueOnGameThread());
}

float UTMMenuViewerMeshTransitionSubsystem::GetBeatSyncWindow()
{
	return FMath::Max(0.01f, CVarMenuViewerMeshTransitionBeatSyncWindow.GetValueOnGameThread());
}

float UTMMenuViewerMeshTransitionSubsystem::GetBeatSyncPulseThreshold()
{
	return FMath::Max(0.01f, CVarMenuViewerMeshTransitionBeatSyncPulseThreshold.GetValueOnGameThread());
}

UTMMenuViewerMeshTransitionSubsystem::FBeatSyncSnapshot UTMMenuViewerMeshTransitionSubsystem::MakeBeatSyncSnapshot(
	const UTMAudioEnvelopeFollower* Follower)
{
	FBeatSyncSnapshot Snapshot;
	if (!Follower || Follower->HasAnyFlags(RF_ClassDefaultObject) || !Follower->bIsAnalyzing)
	{
		return Snapshot;
	}

	Snapshot.bHasActiveFollower = true;
	Snapshot.BeatIndex = Follower->GetBeatCounter();
	Snapshot.BeatsPerBar = FMath::Max(1, Follower->BeatsPerBar);
	Snapshot.LastBeatTimeSeconds = Follower->GetLastBeatTimeSeconds();
	Snapshot.LastBeatStrength = Follower->GetLastBeatStrength();
	Snapshot.NormalizedEnvelopeValue = Follower->NormalizedEnvelopeValue;
	return Snapshot;
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateLoadoutFOV(UWorld* World, const bool bLoadoutVisible)
{
	if (!bLoadoutVisible)
	{
		RestoreLoadoutFOV();
		return;
	}

	APlayerCameraManager* PlayerCameraManager = ResolvePlayerCameraManager(World);
	if (!PlayerCameraManager)
	{
		return;
	}

	if (bLoadoutFOVApplied && LoadoutFOVCameraManager.IsValid() && LoadoutFOVCameraManager.Get() != PlayerCameraManager)
	{
		RestoreLoadoutFOV();
	}

	const float TargetFOV = FMath::Clamp(CVarLoadoutFOVAngle.GetValueOnGameThread(), 5.0f, 170.0f);
	if (!bLoadoutFOVApplied)
	{
		SavedLoadoutPreviousFOV = PlayerCameraManager->GetFOVAngle();
		if (!FMath::IsFinite(SavedLoadoutPreviousFOV))
		{
			SavedLoadoutPreviousFOV = PlayerCameraManager->DefaultFOV;
		}

		LoadoutFOVCameraManager = PlayerCameraManager;
		bLoadoutFOVApplied = true;
		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Display,
			TEXT("[TMLoadoutFOV] Applied loadout FOV %.1f; saved previous FOV %.1f."),
			TargetFOV,
			SavedLoadoutPreviousFOV);
	}

	if (!FMath::IsNearlyEqual(PlayerCameraManager->GetFOVAngle(), TargetFOV, 0.05f))
	{
		PlayerCameraManager->SetFOV(TargetFOV);
	}
}

void UTMMenuViewerMeshTransitionSubsystem::RestoreLoadoutFOV()
{
	if (!bLoadoutFOVApplied)
	{
		return;
	}

	if (APlayerCameraManager* PlayerCameraManager = LoadoutFOVCameraManager.Get())
	{
		const bool bRestoreMenuFOV = bMenuFOVApplied && MenuFOVCameraManager.Get() == PlayerCameraManager;
		const float RestoreFOV = bRestoreMenuFOV
			? GetMenuFOVAngle()
			: FMath::Clamp(SavedLoadoutPreviousFOV, 5.0f, 170.0f);
		if (bRestoreMenuFOV)
		{
			PlayerCameraManager->DefaultFOV = RestoreFOV;
		}
		PlayerCameraManager->SetFOV(RestoreFOV);
		if (!bRestoreMenuFOV)
		{
			PlayerCameraManager->UnlockFOV();
		}
		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Display,
			TEXT("[TMLoadoutFOV] Restored %s FOV %.1f after leaving loadout."),
			bRestoreMenuFOV ? TEXT("menu") : TEXT("previous"),
			RestoreFOV);
	}

	LoadoutFOVCameraManager.Reset();
	bLoadoutFOVApplied = false;
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateMenuFOV(
	UWorld* World,
	const bool bMainMenuVisible,
	const bool bLoadoutVisible)
{
	if (!bMainMenuVisible)
	{
		RestoreMenuFOV();
		return;
	}

	APlayerCameraManager* PlayerCameraManager = ResolvePlayerCameraManager(World);
	if (!PlayerCameraManager)
	{
		return;
	}

	if (bMenuFOVApplied && MenuFOVCameraManager.IsValid() && MenuFOVCameraManager.Get() != PlayerCameraManager)
	{
		RestoreMenuFOV();
	}

	const float TargetFOV = GetMenuFOVAngle();
	if (!bMenuFOVApplied)
	{
		SavedMenuPreviousDefaultFOV = PlayerCameraManager->DefaultFOV;
		if (!FMath::IsFinite(SavedMenuPreviousDefaultFOV))
		{
			SavedMenuPreviousDefaultFOV = 90.0f;
		}

		MenuFOVCameraManager = PlayerCameraManager;
		bMenuFOVApplied = true;
		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Display,
			TEXT("[TMMenuFOV] Applied menu default FOV %.1f; saved previous default FOV %.1f."),
			TargetFOV,
			SavedMenuPreviousDefaultFOV);
	}

	if (!FMath::IsNearlyEqual(PlayerCameraManager->DefaultFOV, TargetFOV, 0.05f))
	{
		PlayerCameraManager->DefaultFOV = TargetFOV;
	}

	if (!bLoadoutVisible && !FMath::IsNearlyEqual(PlayerCameraManager->GetFOVAngle(), TargetFOV, 0.05f))
	{
		PlayerCameraManager->SetFOV(TargetFOV);
	}
}

void UTMMenuViewerMeshTransitionSubsystem::RestoreMenuFOV()
{
	if (!bMenuFOVApplied)
	{
		return;
	}

	if (APlayerCameraManager* PlayerCameraManager = MenuFOVCameraManager.Get())
	{
		PlayerCameraManager->DefaultFOV = FMath::Clamp(SavedMenuPreviousDefaultFOV, 5.0f, 170.0f);
		if (!bLoadoutFOVApplied)
		{
			PlayerCameraManager->UnlockFOV();
		}
		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Display,
			TEXT("[TMMenuFOV] Restored previous default FOV %.1f after leaving main menu."),
			PlayerCameraManager->DefaultFOV);
	}

	MenuFOVCameraManager.Reset();
	bMenuFOVApplied = false;
}

USkeletalMesh* UTMMenuViewerMeshTransitionSubsystem::ChooseNextMenuMesh(USkeletalMesh* CurrentMesh) const
{
	static const TCHAR* MeshPaths[] =
	{
		TEXT("/Game/Test/MPVS_SkeletonProbe/Secondary/SKM_Urban_Soldier_01_MPSBones.SKM_Urban_Soldier_01_MPSBones"),
		TEXT("/Game/Test/MPVS_SkeletonProbe/Secondary/SKM_Urban_Soldier_02_MPSBones.SKM_Urban_Soldier_02_MPSBones"),
		TEXT("/Game/Test/MPVS_SkeletonProbe/Secondary/SKM_Urban_Soldier_05_MPSBones.SKM_Urban_Soldier_05_MPSBones"),
		TEXT("/Game/Test/MPVS_SkeletonProbe/Secondary/SKM_Urban_Soldier_06_MPSBones.SKM_Urban_Soldier_06_MPSBones"),
	};

	TArray<USkeletalMesh*> CandidateMeshes;
	for (const TCHAR* MeshPath : MeshPaths)
	{
		if (USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, MeshPath))
		{
			CandidateMeshes.Add(Mesh);
		}
	}

	if (CandidateMeshes.Num() <= 1)
	{
		return nullptr;
	}

	int32 CurrentIndex = CandidateMeshes.IndexOfByKey(CurrentMesh);
	if (CurrentIndex == INDEX_NONE)
	{
		return CandidateMeshes[0];
	}

	return CandidateMeshes[(CurrentIndex + 1) % CandidateMeshes.Num()];
}

UTMMenuViewerMeshTransitionSubsystem::FBeatSyncSnapshot UTMMenuViewerMeshTransitionSubsystem::FindActiveBeatSync(
	UWorld* World) const
{
	FBeatSyncSnapshot Snapshot;
	if (!World)
	{
		return Snapshot;
	}

	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!Widget || Widget->HasAnyFlags(RF_ClassDefaultObject))
		{
			continue;
		}

		const UClass* WidgetClass = Widget->GetClass();
		if (!WidgetClass || !WidgetClass->GetPathName().Contains(TEXT("W_MainMenu")))
		{
			continue;
		}

		if (const UWorld* WidgetWorld = Widget->GetWorld())
		{
			if (WidgetWorld != World)
			{
				continue;
			}
		}

		const FObjectPropertyBase* EnvelopeFollowerProperty =
			FindFProperty<FObjectPropertyBase>(WidgetClass, TEXT("EnvelopeFollower"));
		if (!EnvelopeFollowerProperty)
		{
			continue;
		}

		const UTMAudioEnvelopeFollower* Follower = Cast<UTMAudioEnvelopeFollower>(
			EnvelopeFollowerProperty->GetObjectPropertyValue_InContainer(Widget));
		Snapshot = MakeBeatSyncSnapshot(Follower);
		if (Snapshot.bHasActiveFollower)
		{
			return Snapshot;
		}
	}

	for (TObjectIterator<UTMAudioEnvelopeFollower> It; It; ++It)
	{
		const UTMAudioEnvelopeFollower* Follower = *It;
		if (!Follower)
		{
			continue;
		}

		if (const UWorld* FollowerWorld = Follower->GetWorld())
		{
			if (FollowerWorld != World)
			{
				continue;
			}
		}

		Snapshot = MakeBeatSyncSnapshot(Follower);
		if (Snapshot.bHasActiveFollower)
		{
			return Snapshot;
		}
	}

	return Snapshot;
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateRhythmPulseMemory(
	FMenuViewerState& State,
	const FBeatSyncSnapshot& BeatSync) const
{
	if (!BeatSync.bHasActiveFollower)
	{
		State.bRhythmPulseWasHigh = false;
		return;
	}

	const float PulseThreshold = GetBeatSyncPulseThreshold();
	if (BeatSync.NormalizedEnvelopeValue <= PulseThreshold * 0.5f)
	{
		State.bRhythmPulseWasHigh = false;
	}
	else if (BeatSync.NormalizedEnvelopeValue >= PulseThreshold)
	{
		State.bRhythmPulseWasHigh = true;
	}
}

bool UTMMenuViewerMeshTransitionSubsystem::IsReadyForBeatSyncedCycle(
	FMenuViewerState& State,
	const FBeatSyncSnapshot& BeatSync,
	const float WorldTimeSeconds) const
{
	if (!BeatSync.bHasActiveFollower)
	{
		return true;
	}

	const float PulseThreshold = GetBeatSyncPulseThreshold();
	const bool bPulseHigh = BeatSync.NormalizedEnvelopeValue >= PulseThreshold;
	const bool bNewRhythmPulse = bPulseHigh && !State.bRhythmPulseWasHigh;
	UpdateRhythmPulseMemory(State, BeatSync);

	if (BeatSync.BeatIndex <= 0)
	{
		return bNewRhythmPulse;
	}

	const bool bNewBeat = BeatSync.BeatIndex > State.LastObservedBeatIndex;
	if (bNewBeat)
	{
		State.LastObservedBeatIndex = BeatSync.BeatIndex;
	}

	const bool bBeatInWindow = WorldTimeSeconds - BeatSync.LastBeatTimeSeconds <= GetBeatSyncWindow();
	const bool bValidBarBeat = !ShouldUseDownbeat() || ((BeatSync.BeatIndex - 1) % BeatSync.BeatsPerBar) == 0;
	if (bNewBeat && bBeatInWindow && bValidBarBeat)
	{
		return true;
	}

	if (bNewRhythmPulse)
	{
		return true;
	}

	return false;
}
