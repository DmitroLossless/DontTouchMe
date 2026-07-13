#include "TMMenuViewerMeshTransitionSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/ActorComponent.h"
#include "Components/LightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gun/Gun.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
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
	85.0f,
	TEXT("Player camera FOV used while the loadout UI is visible."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMenuFOVAngle(
	TEXT("tm.MenuFOV.Angle"),
	90.0f,
	TEXT("Player camera FOV used by default while the main menu UI is visible."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarLoadoutPostProcess(
	TEXT("tm.LoadoutPostProcess"),
	1,
	TEXT("Enables cinematic main menu and loadout camera post process profiles."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutPostProcessBlendWeight(
	TEXT("tm.LoadoutPostProcess.BlendWeight"),
	1.0f,
	TEXT("Blend weight for the cinematic main menu post process."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutPostProcessEdgeBlur(
	TEXT("tm.LoadoutPostProcess.EdgeBlur"),
	82.0f,
	TEXT("Depth-of-field vignette size for main menu edge blur. Lower values blur more of the edge."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutPostProcessPetzval(
	TEXT("tm.LoadoutPostProcess.Petzval"),
	0.8f,
	TEXT("Petzval bokeh strength for the cinematic main menu edge blur."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutPostProcessVignette(
	TEXT("tm.LoadoutPostProcess.Vignette"),
	0.42f,
	TEXT("Dark vignette strength for the cinematic main menu post process."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarLoadoutBackGlowDucking(
	TEXT("tm.LoadoutBackGlowDucking"),
	1,
	TEXT("Enables the delayed bright rear glow profile while the loadout UI is visible."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutBrightBackGlowDelay(
	TEXT("tm.LoadoutBackGlow.Delay"),
	1.0f,
	TEXT("Seconds to wait after entering loadout before fading in the bright rear glow."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutBrightBackGlowFadeDuration(
	TEXT("tm.LoadoutBackGlow.FadeDuration"),
	2.0f,
	TEXT("Seconds used to fade the bright rear glow in while loadout is visible."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutBrightBackGlowLightIntensity(
	TEXT("tm.LoadoutBackGlow.LightIntensity"),
	4.0f,
	TEXT("Final rear RectLight intensity for the bright loadout glow."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutBrightBackGlowVisualScale(
	TEXT("tm.LoadoutBackGlow.VisualScale"),
	2.8f,
	TEXT("Final absolute P_Ambient_Glow scale for the bright loadout glow."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutBrightBackGlowAttenuationRadius(
	TEXT("tm.LoadoutBackGlow.AttenuationRadius"),
	300.0f,
	TEXT("Final rear RectLight attenuation radius for the bright loadout glow."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutBrightBackGlowSourceWidth(
	TEXT("tm.LoadoutBackGlow.SourceWidth"),
	240.0f,
	TEXT("Minimum rear RectLight source width for the bright loadout glow."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutBrightBackGlowSourceHeight(
	TEXT("tm.LoadoutBackGlow.SourceHeight"),
	120.0f,
	TEXT("Minimum rear RectLight source height for the bright loadout glow."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarMainMenuBackGlow(
	TEXT("tm.MainMenuBackGlow"),
	1,
	TEXT("Fades in a subtle white rear glow behind the main menu character after a delay."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMainMenuBackGlowDelay(
	TEXT("tm.MainMenuBackGlow.Delay"),
	1.0f,
	TEXT("Seconds to keep the rear main menu glow black before fading it in."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMainMenuBackGlowFadeDuration(
	TEXT("tm.MainMenuBackGlow.FadeDuration"),
	1.5f,
	TEXT("Seconds used for the smooth neon fade-in behind the main menu character."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMainMenuBackGlowScale(
	TEXT("tm.MainMenuBackGlow.Scale"),
	1.25f,
	TEXT("Final RectLight intensity scale for the delayed main menu rear glow."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMainMenuBackGlowVisualScale(
	TEXT("tm.MainMenuBackGlow.VisualScale"),
	1.18f,
	TEXT("Final P_Ambient_Glow scale for the delayed main menu rear glow."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMainMenuBackGlowEmissive(
	TEXT("tm.MainMenuBackGlow.Emissive"),
	280.0f,
	TEXT("Final emissive multiplier for delayed main menu rear glow materials."),
	ECVF_Default);

static TWeakObjectPtr<USkeletalMesh> GLastMenuViewerVestMesh;

float GetMenuFOVAngle()
{
	return FMath::Clamp(CVarMenuFOVAngle.GetValueOnGameThread(), 5.0f, 170.0f);
}

void ApplyFixedMenuExposure(FPostProcessSettings& Settings, const float ExposureBias)
{
	Settings.bOverride_AutoExposureMethod = true;
	Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Settings.AutoExposureApplyPhysicalCameraExposure = false;
	Settings.bOverride_AutoExposureBias = true;
	Settings.AutoExposureBias = ExposureBias;
	Settings.bOverride_AutoExposureMinBrightness = true;
	Settings.AutoExposureMinBrightness = 0.0f;
	Settings.bOverride_AutoExposureMaxBrightness = true;
	Settings.AutoExposureMaxBrightness = 0.0f;
	Settings.bOverride_AutoExposureSpeedUp = true;
	Settings.AutoExposureSpeedUp = 100.0f;
	Settings.bOverride_AutoExposureSpeedDown = true;
	Settings.AutoExposureSpeedDown = 100.0f;
}

FVector4 LerpVector4(const FVector4& From, const FVector4& To, const float Alpha)
{
	return FVector4(
		FMath::Lerp(From.X, To.X, Alpha),
		FMath::Lerp(From.Y, To.Y, Alpha),
		FMath::Lerp(From.Z, To.Z, Alpha),
		FMath::Lerp(From.W, To.W, Alpha));
}

FLinearColor LerpLinearColor(const FLinearColor& From, const FLinearColor& To, const float Alpha)
{
	return FLinearColor(
		FMath::Lerp(From.R, To.R, Alpha),
		FMath::Lerp(From.G, To.G, Alpha),
		FMath::Lerp(From.B, To.B, Alpha),
		FMath::Lerp(From.A, To.A, Alpha));
}
}

void UTMMenuViewerMeshTransitionSubsystem::Deinitialize()
{
	RestoreLoadoutPostProcess();
	RestoreMainMenuBackGlow();
	RestoreLoadoutBackGlow();
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
		IsLoadoutFOVVisible(World);
	const bool bLoadoutFOVEnabled =
		CVarLoadoutFOV.GetValueOnGameThread() != 0 && bLoadoutFOVVisible;
	const bool bMainMenuVisible = IsMainMenuVisible(World);
	const bool bPostProcessVisible =
		CVarLoadoutPostProcess.GetValueOnGameThread() != 0
		&& (bMainMenuVisible || bLoadoutFOVVisible);
	UpdateMenuFOV(World, bMainMenuVisible, bLoadoutFOVVisible);
	UpdateLoadoutFOV(World, bLoadoutFOVEnabled);
	UpdateLoadoutBackGlowTiming(bLoadoutFOVVisible, DeltaTime);
	UpdateLoadoutPostProcess(World, bPostProcessVisible, bLoadoutFOVVisible);
	if (bLoadoutFOVVisible)
	{
		UpdateMainMenuBackGlow(World, false, DeltaTime);
		UpdateLoadoutBackGlow(World, true, DeltaTime);
	}
	else
	{
		UpdateLoadoutBackGlow(World, false, DeltaTime);
		UpdateMainMenuBackGlow(World, bMainMenuVisible, DeltaTime);
	}

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

bool UTMMenuViewerMeshTransitionSubsystem::IsVisibleLoadoutPreviewWeaponActor(AActor* Actor)
{
	if (!IsValid(Actor)
		|| Actor->HasAnyFlags(RF_ClassDefaultObject)
		|| Actor->IsHidden()
		|| !IsAttachedWeaponActor(Actor)
		|| Actor->GetAttachParentActor())
	{
		return false;
	}

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(Actor);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (IsValid(PrimitiveComponent) && PrimitiveComponent->IsVisible())
		{
			return true;
		}
	}

	return false;
}

bool UTMMenuViewerMeshTransitionSubsystem::IsLoadoutBackGlowLight(const ULightComponent* LightComponent)
{
	if (!IsValid(LightComponent))
	{
		return false;
	}

	const AActor* Owner = LightComponent->GetOwner();
	if (!IsValid(Owner))
	{
		return false;
	}

	const UClass* ComponentClass = LightComponent->GetClass();
	const FString ComponentClassName = ComponentClass ? ComponentClass->GetName() : FString();
	if (!ComponentClassName.Contains(TEXT("RectLight")))
	{
		return false;
	}

	const FString OwnerName = Owner->GetName();
	if (!OwnerName.Contains(TEXT("RectLight")))
	{
		return false;
	}

	return Owner->GetActorLocation().X <= -250.0f;
}

bool UTMMenuViewerMeshTransitionSubsystem::IsLoadoutBackGlowVisual(const UPrimitiveComponent* PrimitiveComponent)
{
	if (!IsValid(PrimitiveComponent))
	{
		return false;
	}

	if (PrimitiveComponent->ComponentHasTag(TEXT("TMSpawnedLoadoutBackGlowVisual")))
	{
		return false;
	}

	const AActor* Owner = PrimitiveComponent->GetOwner();
	if (!IsValid(Owner))
	{
		return false;
	}

	const UParticleSystemComponent* ParticleComponent = Cast<UParticleSystemComponent>(PrimitiveComponent);
	if (!ParticleComponent || !ParticleComponent->Template)
	{
		return false;
	}

	const FString OwnerName = Owner->GetName();
	const FString TemplatePath = ParticleComponent->Template->GetPathName();
	return OwnerName.Contains(TEXT("P_Ambient_Glow"))
		|| TemplatePath.Contains(TEXT("P_Ambient_Glow"));
}

AActor* UTMMenuViewerMeshTransitionSubsystem::ResolveLoadoutPreviewWeaponActor(
	UWorld* World,
	const FVector& CameraLocation) const
{
	if (!World)
	{
		return nullptr;
	}

	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->GetWorld() != World)
		{
			continue;
		}

		const UClass* WidgetClass = Widget->GetClass();
		if (!WidgetClass || !WidgetClass->GetPathName().Contains(TEXT("W_Loadout")))
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

		AActor* ActiveWeapon = Cast<AActor>(ActiveWeaponProperty->GetObjectPropertyValue_InContainer(Widget));
		if (IsVisibleLoadoutPreviewWeaponActor(ActiveWeapon))
		{
			return ActiveWeapon;
		}
	}

	AActor* BestActor = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsVisibleLoadoutPreviewWeaponActor(Actor))
		{
			continue;
		}

		const float DistanceSq = FVector::DistSquared(CameraLocation, Actor->GetActorLocation());
		if (FMath::IsFinite(DistanceSq) && DistanceSq < BestDistanceSq)
		{
			BestActor = Actor;
			BestDistanceSq = DistanceSq;
		}
	}

	return BestActor;
}

APlayerCameraManager* UTMMenuViewerMeshTransitionSubsystem::ResolvePlayerCameraManager(UWorld* World)
{
	return World ? UGameplayStatics::GetPlayerCameraManager(World, 0) : nullptr;
}

UCameraComponent* UTMMenuViewerMeshTransitionSubsystem::ResolveActiveCameraComponent(UWorld* World)
{
	APlayerCameraManager* PlayerCameraManager = ResolvePlayerCameraManager(World);
	AActor* ViewTarget = PlayerCameraManager ? PlayerCameraManager->GetViewTarget() : nullptr;
	if (ViewTarget)
	{
		TInlineComponentArray<UCameraComponent*> CameraComponents(ViewTarget);
		for (UCameraComponent* CameraComponent : CameraComponents)
		{
			if (CameraComponent && CameraComponent->IsActive())
			{
				return CameraComponent;
			}
		}

		if (CameraComponents.Num() > 0)
		{
			return CameraComponents[0];
		}
	}

	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsMenuViewerActor(Actor))
		{
			continue;
		}

		if (UCameraComponent* CameraComponent = Actor->FindComponentByClass<UCameraComponent>())
		{
			return CameraComponent;
		}
	}

	return nullptr;
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

void UTMMenuViewerMeshTransitionSubsystem::UpdateLoadoutPostProcess(
	UWorld* World,
	const bool bPostProcessVisible,
	const bool bLoadoutVisible)
{
	if (!bPostProcessVisible)
	{
		RestoreLoadoutPostProcess();
		return;
	}

	UCameraComponent* CameraComponent = ResolveActiveCameraComponent(World);
	if (!CameraComponent)
	{
		RestoreLoadoutPostProcess();
		return;
	}

	if (bLoadoutPostProcessApplied
		&& LoadoutPostProcessCamera.IsValid()
		&& LoadoutPostProcessCamera.Get() != CameraComponent)
	{
		RestoreLoadoutPostProcess();
	}

	if (!bLoadoutPostProcessApplied)
	{
		SavedLoadoutPostProcessSettings = CameraComponent->PostProcessSettings;
		SavedLoadoutPostProcessBlendWeight = CameraComponent->PostProcessBlendWeight;
		LoadoutPostProcessCamera = CameraComponent;
		bLoadoutPostProcessApplied = true;
		bLoadoutPostProcessLastLoadoutMode = bLoadoutVisible;
		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Display,
			TEXT("[TMMenuPP] Applied cinematic %s post process on %s. SavedBlendWeight=%.2f"),
			bLoadoutVisible ? TEXT("loadout") : TEXT("main menu"),
			*GetNameSafe(CameraComponent),
			SavedLoadoutPostProcessBlendWeight);
	}
	else if (bLoadoutPostProcessLastLoadoutMode != bLoadoutVisible)
	{
		bLoadoutPostProcessLastLoadoutMode = bLoadoutVisible;
		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Display,
			TEXT("[TMMenuPP] Switched cinematic post process to %s profile on %s."),
			bLoadoutVisible ? TEXT("loadout") : TEXT("main menu"),
			*GetNameSafe(CameraComponent));
	}

	ApplyLoadoutPostProcess(World, CameraComponent, bLoadoutVisible);
}

void UTMMenuViewerMeshTransitionSubsystem::ApplyLoadoutPostProcess(
	UWorld* World,
	UCameraComponent* CameraComponent,
	const bool bLoadoutVisible)
{
	if (!CameraComponent)
	{
		return;
	}

	FPostProcessSettings Settings = SavedLoadoutPostProcessSettings;
	const FLoadoutPostProcessFocus Focus = ResolveLoadoutPostProcessFocus(World, CameraComponent);

	if (bLoadoutVisible)
	{
		const float GlowAlpha = FMath::Clamp(LoadoutBackGlowCurrentAlpha, 0.0f, 1.0f);
		Settings.bOverride_DepthOfFieldFstop = true;
		Settings.DepthOfFieldFstop = FMath::Lerp(2.6f, 8.0f, GlowAlpha);
		Settings.bOverride_DepthOfFieldMinFstop = true;
		Settings.DepthOfFieldMinFstop = FMath::Lerp(1.8f, 8.0f, GlowAlpha);
		Settings.bOverride_DepthOfFieldFocalDistance = true;
		Settings.DepthOfFieldFocalDistance = Focus.FocalDistance;
		Settings.bOverride_DepthOfFieldFocalRegion = true;
		Settings.DepthOfFieldFocalRegion = FMath::Lerp(FMath::Max(Focus.FocalRegion, 210.0f), 1000.0f, GlowAlpha);
		Settings.bOverride_DepthOfFieldNearTransitionRegion = true;
		Settings.DepthOfFieldNearTransitionRegion = FMath::Lerp(130.0f, 1000.0f, GlowAlpha);
		Settings.bOverride_DepthOfFieldFarTransitionRegion = true;
		Settings.DepthOfFieldFarTransitionRegion = FMath::Lerp(190.0f, 1000.0f, GlowAlpha);
		Settings.bOverride_DepthOfFieldScale = true;
		Settings.DepthOfFieldScale = FMath::Lerp(0.55f, 0.0f, GlowAlpha);
		Settings.bOverride_DepthOfFieldNearBlurSize = true;
		Settings.DepthOfFieldNearBlurSize = FMath::Lerp(1.5f, 0.0f, GlowAlpha);
		Settings.bOverride_DepthOfFieldFarBlurSize = true;
		Settings.DepthOfFieldFarBlurSize = FMath::Lerp(6.0f, 0.0f, GlowAlpha);
		Settings.bOverride_DepthOfFieldPetzvalBokeh = true;
		Settings.DepthOfFieldPetzvalBokeh = FMath::Lerp(0.28f, 0.0f, GlowAlpha);
		Settings.bOverride_DepthOfFieldPetzvalBokehFalloff = true;
		Settings.DepthOfFieldPetzvalBokehFalloff = FMath::Lerp(2.8f, 0.0f, GlowAlpha);
		Settings.bOverride_DepthOfFieldPetzvalExclusionBoxExtents = true;
		Settings.DepthOfFieldPetzvalExclusionBoxExtents = FVector2f(
			FMath::Lerp(0.72f, 1.0f, GlowAlpha),
			FMath::Lerp(0.52f, 1.0f, GlowAlpha));
		Settings.bOverride_DepthOfFieldVignetteSize = true;
		Settings.DepthOfFieldVignetteSize = FMath::Lerp(94.0f, 100.0f, GlowAlpha);

		Settings.bOverride_BloomIntensity = true;
		Settings.BloomIntensity = FMath::Lerp(0.82f, 0.35f, GlowAlpha);
		Settings.bOverride_BloomGaussianIntensity = true;
		Settings.BloomGaussianIntensity = FMath::Lerp(0.55f, 0.35f, GlowAlpha);
		Settings.bOverride_BloomThreshold = true;
		Settings.BloomThreshold = FMath::Lerp(1.25f, 0.4f, GlowAlpha);
		Settings.bOverride_BloomSizeScale = true;
		Settings.BloomSizeScale = FMath::Lerp(1.3f, 1.6f, GlowAlpha);
		Settings.bOverride_Bloom1Tint = true;
		Settings.Bloom1Tint = LerpLinearColor(
			FLinearColor(0.52f, 0.52f, 0.52f, 1.0f),
			FLinearColor::White,
			GlowAlpha);
		Settings.bOverride_Bloom3Tint = true;
		Settings.Bloom3Tint = LerpLinearColor(
			FLinearColor(0.36f, 0.36f, 0.36f, 1.0f),
			FLinearColor::White,
			GlowAlpha);
		Settings.bOverride_Bloom5Tint = true;
		Settings.Bloom5Tint = LerpLinearColor(
			FLinearColor(0.24f, 0.24f, 0.24f, 1.0f),
			FLinearColor::White,
			GlowAlpha);

		ApplyFixedMenuExposure(Settings, FMath::Lerp(-1.18f, -0.55f, GlowAlpha));
		Settings.bOverride_VignetteIntensity = true;
		Settings.VignetteIntensity = FMath::Lerp(0.74f, 0.58f, GlowAlpha);
		Settings.bOverride_SceneFringeIntensity = true;
		Settings.SceneFringeIntensity = FMath::Lerp(0.045f, 0.0f, GlowAlpha);
		Settings.bOverride_ChromaticAberrationStartOffset = true;
		Settings.ChromaticAberrationStartOffset = FMath::Lerp(0.62f, 0.0f, GlowAlpha);
		Settings.bOverride_FilmGrainIntensity = true;
		Settings.FilmGrainIntensity = FMath::Lerp(0.075f, 0.0f, GlowAlpha);

		Settings.bOverride_SceneColorTint = true;
		Settings.SceneColorTint = LerpLinearColor(
			FLinearColor(0.86f, 0.86f, 0.86f, 1.0f),
			FLinearColor(0.9f, 0.9f, 0.9f, 1.0f),
			GlowAlpha);
		Settings.bOverride_ColorSaturation = true;
		Settings.ColorSaturation = LerpVector4(
			FVector4(0.62f, 0.62f, 0.62f, 1.0f),
			FVector4(0.82f, 0.82f, 0.82f, 1.0f),
			GlowAlpha);
		Settings.bOverride_ColorContrast = true;
		Settings.ColorContrast = LerpVector4(
			FVector4(1.12f, 1.12f, 1.12f, 1.0f),
			FVector4(1.0f, 1.0f, 1.0f, 1.0f),
			GlowAlpha);
		Settings.bOverride_ColorGamma = true;
		Settings.ColorGamma = LerpVector4(
			FVector4(0.94f, 0.94f, 0.94f, 1.0f),
			FVector4(1.0f, 1.0f, 1.0f, 1.0f),
			GlowAlpha);
		Settings.bOverride_ColorGain = true;
		Settings.ColorGain = LerpVector4(
			FVector4(0.68f, 0.68f, 0.68f, 1.0f),
			FVector4(0.78f, 0.78f, 0.78f, 1.0f),
			GlowAlpha);
		Settings.bOverride_ColorOffset = true;
		Settings.ColorOffset = LerpVector4(
			FVector4(-0.04f, -0.04f, -0.04f, 0.0f),
			FVector4(0.0f, 0.0f, 0.0f, 0.0f),
			GlowAlpha);
	}
	else
	{
		Settings.bOverride_DepthOfFieldFstop = true;
		Settings.DepthOfFieldFstop = 1.05f;
		Settings.bOverride_DepthOfFieldMinFstop = true;
		Settings.DepthOfFieldMinFstop = 1.0f;
		Settings.bOverride_DepthOfFieldFocalDistance = true;
		Settings.DepthOfFieldFocalDistance = Focus.FocalDistance;
		Settings.bOverride_DepthOfFieldFocalRegion = true;
		Settings.DepthOfFieldFocalRegion = Focus.FocalRegion;
		Settings.bOverride_DepthOfFieldNearTransitionRegion = true;
		Settings.DepthOfFieldNearTransitionRegion = 45.0f;
		Settings.bOverride_DepthOfFieldFarTransitionRegion = true;
		Settings.DepthOfFieldFarTransitionRegion = 70.0f;
		Settings.bOverride_DepthOfFieldScale = true;
		Settings.DepthOfFieldScale = 1.15f;
		Settings.bOverride_DepthOfFieldNearBlurSize = true;
		Settings.DepthOfFieldNearBlurSize = 5.0f;
		Settings.bOverride_DepthOfFieldFarBlurSize = true;
		Settings.DepthOfFieldFarBlurSize = 10.0f;
		Settings.bOverride_DepthOfFieldPetzvalBokeh = true;
		Settings.DepthOfFieldPetzvalBokeh = FMath::Clamp(
			CVarLoadoutPostProcessPetzval.GetValueOnGameThread(),
			-10.0f,
			10.0f);
		Settings.bOverride_DepthOfFieldPetzvalBokehFalloff = true;
		Settings.DepthOfFieldPetzvalBokehFalloff = 4.0f;
		Settings.bOverride_DepthOfFieldPetzvalExclusionBoxExtents = true;
		Settings.DepthOfFieldPetzvalExclusionBoxExtents = FVector2f(0.62f, 0.42f);
		Settings.bOverride_DepthOfFieldVignetteSize = true;
		Settings.DepthOfFieldVignetteSize = FMath::Clamp(
			CVarLoadoutPostProcessEdgeBlur.GetValueOnGameThread(),
			5.0f,
			100.0f);

		ApplyFixedMenuExposure(Settings, -1.0f);
		Settings.bOverride_VignetteIntensity = true;
		Settings.VignetteIntensity = FMath::Clamp(
			CVarLoadoutPostProcessVignette.GetValueOnGameThread(),
			0.0f,
			1.0f);
		Settings.bOverride_SceneFringeIntensity = true;
		Settings.SceneFringeIntensity = 0.25f;
		Settings.bOverride_FilmGrainIntensity = true;
		Settings.FilmGrainIntensity = 0.04f;
	}

	CameraComponent->PostProcessSettings = Settings;
	CameraComponent->SetPostProcessBlendWeight(FMath::Clamp(
		CVarLoadoutPostProcessBlendWeight.GetValueOnGameThread(),
		0.0f,
		1.0f));
}

void UTMMenuViewerMeshTransitionSubsystem::RestoreLoadoutPostProcess()
{
	if (!bLoadoutPostProcessApplied)
	{
		return;
	}

	if (UCameraComponent* CameraComponent = LoadoutPostProcessCamera.Get())
	{
		CameraComponent->PostProcessSettings = SavedLoadoutPostProcessSettings;
		CameraComponent->SetPostProcessBlendWeight(SavedLoadoutPostProcessBlendWeight);
		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Display,
			TEXT("[TMMenuPP] Restored original post process on %s."),
			*GetNameSafe(CameraComponent));
	}

	LoadoutPostProcessCamera.Reset();
	SavedLoadoutPostProcessSettings = FPostProcessSettings();
	SavedLoadoutPostProcessBlendWeight = 0.0f;
	bLoadoutPostProcessApplied = false;
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateLoadoutBackGlowTiming(
	const bool bLoadoutVisible,
	const float DeltaTime)
{
	if (!bLoadoutVisible)
	{
		if (bLoadoutBackGlowActive)
		{
			bMainMenuBackGlowForceComplete = true;
		}

		bLoadoutBackGlowActive = false;
		bLoadoutBackGlowTargetVisible = false;
		LoadoutBackGlowElapsedSeconds = 0.0f;
		LoadoutBackGlowCurrentAlpha = 0.0f;
		return;
	}

	if (!bLoadoutBackGlowActive)
	{
		bLoadoutBackGlowActive = true;
		bLoadoutBackGlowTargetVisible = true;
		LoadoutBackGlowElapsedSeconds = 0.0f;
		LoadoutBackGlowCurrentAlpha = 0.0f;
		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Display,
			TEXT("[TMLoadoutBackGlow] Holding loadout glow for %.2fs, then fading to bright profile over %.2fs."),
			FMath::Max(0.0f, CVarLoadoutBrightBackGlowDelay.GetValueOnGameThread()),
			FMath::Max(0.01f, CVarLoadoutBrightBackGlowFadeDuration.GetValueOnGameThread()));
		return;
	}

	LoadoutBackGlowElapsedSeconds += FMath::Max(0.0f, DeltaTime);
	const float Delay = FMath::Max(0.0f, CVarLoadoutBrightBackGlowDelay.GetValueOnGameThread());
	const float FadeDuration = FMath::Max(0.01f, CVarLoadoutBrightBackGlowFadeDuration.GetValueOnGameThread());
	const float FadeAlpha = FMath::Clamp((LoadoutBackGlowElapsedSeconds - Delay) / FadeDuration, 0.0f, 1.0f);
	LoadoutBackGlowCurrentAlpha = FadeAlpha * FadeAlpha * (3.0f - 2.0f * FadeAlpha);
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateLoadoutBackGlow(
	UWorld* World,
	const bool bLoadoutVisible,
	const float DeltaTime)
{
	(void)DeltaTime;

	if (!World || CVarLoadoutBackGlowDucking.GetValueOnGameThread() == 0)
	{
		RestoreLoadoutBackGlow();
		return;
	}

	if (!bLoadoutVisible)
	{
		RestoreLoadoutBackGlow();
		return;
	}

	for (auto It = LoadoutBackGlowLightStates.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	for (auto It = LoadoutBackGlowVisualStates.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	const float GlowAlpha = FMath::Clamp(LoadoutBackGlowCurrentAlpha, 0.0f, 1.0f);
	const FVector TargetGlowLocation(-740.0f, 65.0f, -35.0f);
	const FRotator TargetGlowRotation(0.0f, -90.0f, 0.0f);
	const FVector TargetGlowScale(FMath::Max(0.0f, CVarLoadoutBrightBackGlowVisualScale.GetValueOnGameThread()));
	const float TargetLightIntensity = FMath::Max(0.0f, CVarLoadoutBrightBackGlowLightIntensity.GetValueOnGameThread());
	const float TargetAttenuationRadius =
		FMath::Max(0.0f, CVarLoadoutBrightBackGlowAttenuationRadius.GetValueOnGameThread());
	const float TargetSourceWidth = FMath::Max(0.0f, CVarLoadoutBrightBackGlowSourceWidth.GetValueOnGameThread());
	const float TargetSourceHeight = FMath::Max(0.0f, CVarLoadoutBrightBackGlowSourceHeight.GetValueOnGameThread());

	int32 AppliedLights = 0;
	int32 AppliedVisuals = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		TInlineComponentArray<ULightComponent*> LightComponents(Actor);
		for (ULightComponent* LightComponent : LightComponents)
		{
			if (!IsLoadoutBackGlowLight(LightComponent))
			{
				continue;
			}

			if (!LoadoutBackGlowLightStates.Contains(LightComponent))
			{
				FBackGlowLightState LightState;
				LightState.OriginalIntensity = LightComponent->Intensity;
				LightState.OriginalColor = LightComponent->GetLightColor();
				if (const ULocalLightComponent* LocalLightComponent = Cast<ULocalLightComponent>(LightComponent))
				{
					LightState.OriginalAttenuationRadius = LocalLightComponent->AttenuationRadius;
					LightState.bHasAttenuationRadius = true;
				}
				if (const URectLightComponent* RectLightComponent = Cast<URectLightComponent>(LightComponent))
				{
					LightState.OriginalSourceWidth = RectLightComponent->SourceWidth;
					LightState.OriginalSourceHeight = RectLightComponent->SourceHeight;
					LightState.bHasRectSourceSize = true;
				}
				LoadoutBackGlowLightStates.Add(LightComponent, LightState);
			}

			const FBackGlowLightState LightState = LoadoutBackGlowLightStates.FindRef(LightComponent);
			LightComponent->SetLightColor(LerpLinearColor(LightState.OriginalColor, FLinearColor::White, GlowAlpha));
			LightComponent->SetIntensity(FMath::Lerp(
				LightState.OriginalIntensity,
				TargetLightIntensity,
				GlowAlpha));
			if (ULocalLightComponent* LocalLightComponent = Cast<ULocalLightComponent>(LightComponent);
				LocalLightComponent && LightState.bHasAttenuationRadius)
			{
				LocalLightComponent->SetAttenuationRadius(FMath::Lerp(
					LightState.OriginalAttenuationRadius,
					TargetAttenuationRadius,
					GlowAlpha));
			}
			if (URectLightComponent* RectLightComponent = Cast<URectLightComponent>(LightComponent);
				RectLightComponent && LightState.bHasRectSourceSize)
			{
				RectLightComponent->SetSourceWidth(FMath::Lerp(
					LightState.OriginalSourceWidth,
					TargetSourceWidth,
					GlowAlpha));
				RectLightComponent->SetSourceHeight(FMath::Lerp(
					LightState.OriginalSourceHeight,
					TargetSourceHeight,
					GlowAlpha));
			}
			++AppliedLights;
		}

		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(Actor);
		for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			if (!IsLoadoutBackGlowVisual(PrimitiveComponent))
			{
				continue;
			}

			if (!LoadoutBackGlowVisualStates.Contains(PrimitiveComponent))
			{
				FBackGlowVisualState VisualState;
				VisualState.OriginalLocation = PrimitiveComponent->GetComponentLocation();
				VisualState.OriginalRotation = PrimitiveComponent->GetComponentRotation();
				VisualState.OriginalScale = PrimitiveComponent->GetComponentScale();
				VisualState.bVisible = PrimitiveComponent->IsVisible();
				VisualState.bHiddenInGame = PrimitiveComponent->bHiddenInGame;
				const int32 MaterialCount = PrimitiveComponent->GetNumMaterials();
				VisualState.OriginalMaterials.Reserve(MaterialCount);
				for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
				{
					VisualState.OriginalMaterials.Add(PrimitiveComponent->GetMaterial(MaterialIndex));
				}
				LoadoutBackGlowVisualStates.Add(PrimitiveComponent, VisualState);
			}

			const FBackGlowVisualState VisualState = LoadoutBackGlowVisualStates.FindRef(PrimitiveComponent);
			PrimitiveComponent->SetWorldLocation(FMath::Lerp(
				VisualState.OriginalLocation,
				TargetGlowLocation,
				GlowAlpha));
			PrimitiveComponent->SetWorldRotation(FQuat::Slerp(
				VisualState.OriginalRotation.Quaternion(),
				TargetGlowRotation.Quaternion(),
				GlowAlpha).Rotator());
			PrimitiveComponent->SetWorldScale3D(FMath::Lerp(
				VisualState.OriginalScale,
				TargetGlowScale,
				GlowAlpha));
			PrimitiveComponent->SetVisibility(true, true);
			PrimitiveComponent->SetHiddenInGame(false, true);

			if (UParticleSystemComponent* ParticleComponent = Cast<UParticleSystemComponent>(PrimitiveComponent))
			{
				ParticleComponent->SetFloatParameter(TEXT("Alpha"), GlowAlpha);
				ParticleComponent->SetFloatParameter(TEXT("Opacity"), GlowAlpha);
				ParticleComponent->SetFloatParameter(TEXT("Intensity"), GlowAlpha);
				ParticleComponent->SetFloatParameter(TEXT("Brightness"), GlowAlpha);
				ParticleComponent->SetColorParameter(TEXT("Color"), FLinearColor::White);
				ParticleComponent->SetColorParameter(TEXT("Tint"), FLinearColor::White);
			}

			++AppliedVisuals;
		}
	}

	if (!LoadoutBackGlowSpawnedVisual.IsValid())
	{
		UParticleSystem* GlowTemplate = LoadObject<UParticleSystem>(
			nullptr,
			TEXT("/Game/MP_System_V3/Game/Commons/Particles/P_Ambient_Glow.P_Ambient_Glow"));
		if (GlowTemplate)
		{
			LoadoutBackGlowSpawnedVisual = UGameplayStatics::SpawnEmitterAtLocation(
				World,
				GlowTemplate,
				TargetGlowLocation,
				TargetGlowRotation,
				FVector::OneVector,
				false,
				EPSCPoolMethod::None,
				false);
			if (UParticleSystemComponent* SpawnedVisual = LoadoutBackGlowSpawnedVisual.Get())
			{
				SpawnedVisual->ComponentTags.AddUnique(TEXT("TMSpawnedLoadoutBackGlowVisual"));
			}
		}
	}

	if (UParticleSystemComponent* SpawnedVisual = LoadoutBackGlowSpawnedVisual.Get())
	{
		const bool bVisualVisible = GlowAlpha > 0.001f;
		SpawnedVisual->SetWorldLocation(TargetGlowLocation);
		SpawnedVisual->SetWorldRotation(TargetGlowRotation);
		SpawnedVisual->SetWorldScale3D(TargetGlowScale * GlowAlpha);
		SpawnedVisual->SetVisibility(bVisualVisible, true);
		SpawnedVisual->SetHiddenInGame(!bVisualVisible, true);
		if (!SpawnedVisual->IsActive())
		{
			SpawnedVisual->ActivateSystem(true);
		}
		SpawnedVisual->SetFloatParameter(TEXT("Alpha"), GlowAlpha);
		SpawnedVisual->SetFloatParameter(TEXT("Opacity"), GlowAlpha);
		SpawnedVisual->SetFloatParameter(TEXT("Intensity"), GlowAlpha);
		SpawnedVisual->SetFloatParameter(TEXT("Brightness"), GlowAlpha);
		SpawnedVisual->SetColorParameter(TEXT("Color"), FLinearColor::White);
		SpawnedVisual->SetColorParameter(TEXT("Tint"), FLinearColor::White);
		++AppliedVisuals;
	}

	if (AppliedLights == 0 && AppliedVisuals == 0 && bLoadoutVisible)
	{
		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Verbose,
			TEXT("[TMLoadoutBackGlow] No rear RectLight glow components found for bright loadout profile."));
	}
}

void UTMMenuViewerMeshTransitionSubsystem::RestoreLoadoutBackGlow()
{
	if (UParticleSystemComponent* SpawnedVisual = LoadoutBackGlowSpawnedVisual.Get())
	{
		SpawnedVisual->DeactivateSystem();
		SpawnedVisual->DestroyComponent();
	}
	LoadoutBackGlowSpawnedVisual.Reset();

	for (const TPair<TWeakObjectPtr<ULightComponent>, FBackGlowLightState>& LightState : LoadoutBackGlowLightStates)
	{
		if (ULightComponent* LightComponent = LightState.Key.Get())
		{
			LightComponent->SetIntensity(LightState.Value.OriginalIntensity);
			LightComponent->SetLightColor(LightState.Value.OriginalColor);
			if (ULocalLightComponent* LocalLightComponent = Cast<ULocalLightComponent>(LightComponent);
				LocalLightComponent && LightState.Value.bHasAttenuationRadius)
			{
				LocalLightComponent->SetAttenuationRadius(LightState.Value.OriginalAttenuationRadius);
			}
			if (URectLightComponent* RectLightComponent = Cast<URectLightComponent>(LightComponent);
				RectLightComponent && LightState.Value.bHasRectSourceSize)
			{
				RectLightComponent->SetSourceWidth(LightState.Value.OriginalSourceWidth);
				RectLightComponent->SetSourceHeight(LightState.Value.OriginalSourceHeight);
			}
		}
	}

	for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, FBackGlowVisualState>& VisualState : LoadoutBackGlowVisualStates)
	{
		if (UPrimitiveComponent* PrimitiveComponent = VisualState.Key.Get())
		{
			for (int32 MaterialIndex = 0; MaterialIndex < VisualState.Value.OriginalMaterials.Num(); ++MaterialIndex)
			{
				if (UMaterialInterface* OriginalMaterial = VisualState.Value.OriginalMaterials[MaterialIndex].Get())
				{
					PrimitiveComponent->SetMaterial(MaterialIndex, OriginalMaterial);
				}
			}
			PrimitiveComponent->SetWorldLocation(VisualState.Value.OriginalLocation);
			PrimitiveComponent->SetWorldRotation(VisualState.Value.OriginalRotation);
			PrimitiveComponent->SetWorldScale3D(VisualState.Value.OriginalScale);
			PrimitiveComponent->SetVisibility(VisualState.Value.bVisible, true);
			PrimitiveComponent->SetHiddenInGame(VisualState.Value.bHiddenInGame, true);
			if (UParticleSystemComponent* ParticleComponent = Cast<UParticleSystemComponent>(PrimitiveComponent))
			{
				ParticleComponent->SetFloatParameter(TEXT("Alpha"), 1.0f);
				ParticleComponent->SetFloatParameter(TEXT("Opacity"), 1.0f);
				ParticleComponent->SetFloatParameter(TEXT("Intensity"), 1.0f);
				ParticleComponent->SetFloatParameter(TEXT("Brightness"), 1.0f);
			}
		}
	}

	LoadoutBackGlowLightStates.Empty();
	LoadoutBackGlowVisualStates.Empty();
	LoadoutBackGlowElapsedSeconds = 0.0f;
	LoadoutBackGlowCurrentAlpha = 0.0f;
	bLoadoutBackGlowTargetVisible = false;
	bLoadoutBackGlowActive = false;
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateMainMenuBackGlow(
	UWorld* World,
	const bool bMainMenuGlowVisible,
	const float DeltaTime)
{
	if (!World || CVarMainMenuBackGlow.GetValueOnGameThread() == 0 || !bMainMenuGlowVisible)
	{
		RestoreMainMenuBackGlow();
		return;
	}

	const float Delay = FMath::Max(0.0f, CVarMainMenuBackGlowDelay.GetValueOnGameThread());
	const float FadeDuration = FMath::Max(0.01f, CVarMainMenuBackGlowFadeDuration.GetValueOnGameThread());
	if (!bMainMenuBackGlowActive)
	{
		bMainMenuBackGlowActive = true;
		if (bMainMenuBackGlowForceComplete)
		{
			MainMenuBackGlowElapsedSeconds = Delay + FadeDuration;
			MainMenuBackGlowCurrentScale = FMath::Clamp(CVarMainMenuBackGlowScale.GetValueOnGameThread(), 0.0f, 2.0f);
			bMainMenuBackGlowForceComplete = false;
			UE_LOG(
				LogTMMenuViewerMeshTransition,
				Display,
				TEXT("[TMMainMenuBackGlow] Restored final main menu rear glow immediately after leaving loadout."));
		}
		else
		{
			MainMenuBackGlowElapsedSeconds = 0.0f;
			MainMenuBackGlowCurrentScale = 0.0f;
			UE_LOG(
				LogTMMenuViewerMeshTransition,
				Display,
				TEXT("[TMMainMenuBackGlow] Holding rear glow black for %.2fs before neon fade-in."),
				Delay);
		}
	}
	else
	{
		MainMenuBackGlowElapsedSeconds += FMath::Max(0.0f, DeltaTime);
		if (bMainMenuBackGlowForceComplete)
		{
			MainMenuBackGlowElapsedSeconds = Delay + FadeDuration;
			bMainMenuBackGlowForceComplete = false;
		}
	}

	const float FadeAlpha = FMath::Clamp((MainMenuBackGlowElapsedSeconds - Delay) / FadeDuration, 0.0f, 1.0f);
	const float SmoothAlpha = FadeAlpha * FadeAlpha * (3.0f - 2.0f * FadeAlpha);
	MainMenuBackGlowCurrentScale =
		FMath::Clamp(CVarMainMenuBackGlowScale.GetValueOnGameThread(), 0.0f, 2.0f) * SmoothAlpha;

	for (auto It = MainMenuBackGlowLightStates.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	for (auto It = MainMenuBackGlowVisualStates.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	int32 AppliedLights = 0;
	int32 AppliedVisuals = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		TInlineComponentArray<ULightComponent*> LightComponents(Actor);
		for (ULightComponent* LightComponent : LightComponents)
		{
			if (!IsLoadoutBackGlowLight(LightComponent))
			{
				continue;
			}

			if (!MainMenuBackGlowLightStates.Contains(LightComponent))
			{
				FBackGlowLightState LightState;
				LightState.OriginalIntensity = LightComponent->Intensity;
				LightState.OriginalColor = LightComponent->GetLightColor();
				MainMenuBackGlowLightStates.Add(LightComponent, LightState);
			}

			const FBackGlowLightState LightState = MainMenuBackGlowLightStates.FindRef(LightComponent);
			LightComponent->SetLightColor(FLinearColor::White);
			LightComponent->SetIntensity(LightState.OriginalIntensity * MainMenuBackGlowCurrentScale);
			++AppliedLights;
		}

		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(Actor);
		for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			if (!IsLoadoutBackGlowVisual(PrimitiveComponent))
			{
				continue;
			}

			if (!MainMenuBackGlowVisualStates.Contains(PrimitiveComponent))
			{
				FBackGlowVisualState VisualState;
				VisualState.OriginalLocation = PrimitiveComponent->GetComponentLocation();
				VisualState.OriginalRotation = PrimitiveComponent->GetComponentRotation();
				VisualState.OriginalScale = PrimitiveComponent->GetComponentScale();
				VisualState.bVisible = PrimitiveComponent->IsVisible();
				VisualState.bHiddenInGame = PrimitiveComponent->bHiddenInGame;
				const int32 MaterialCount = PrimitiveComponent->GetNumMaterials();
				VisualState.OriginalMaterials.Reserve(MaterialCount);
				for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
				{
					VisualState.OriginalMaterials.Add(PrimitiveComponent->GetMaterial(MaterialIndex));
				}
				MainMenuBackGlowVisualStates.Add(PrimitiveComponent, VisualState);
			}

			const FBackGlowVisualState VisualState = MainMenuBackGlowVisualStates.FindRef(PrimitiveComponent);
			const float VisualScale =
				FMath::Clamp(CVarMainMenuBackGlowVisualScale.GetValueOnGameThread(), 0.0f, 2.0f) * SmoothAlpha;
			PrimitiveComponent->SetVisibility(VisualState.bVisible, true);
			PrimitiveComponent->SetHiddenInGame(VisualState.bHiddenInGame, true);
			PrimitiveComponent->SetWorldScale3D(VisualState.OriginalScale * VisualScale);

			for (int32 MaterialIndex = 0; MaterialIndex < VisualState.OriginalMaterials.Num(); ++MaterialIndex)
			{
				UMaterialInterface* SourceMaterial = VisualState.OriginalMaterials[MaterialIndex].Get();
				UMaterialInstanceDynamic* DynamicMaterial =
					Cast<UMaterialInstanceDynamic>(PrimitiveComponent->GetMaterial(MaterialIndex));
				if (!DynamicMaterial)
				{
					DynamicMaterial = PrimitiveComponent->CreateDynamicMaterialInstance(MaterialIndex, SourceMaterial);
				}
				if (DynamicMaterial)
				{
					const float Emissive = FMath::Max(0.0f, CVarMainMenuBackGlowEmissive.GetValueOnGameThread()) * SmoothAlpha;
					DynamicMaterial->SetScalarParameterValue(TEXT("Emissive"), Emissive);
					DynamicMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor::White);
					DynamicMaterial->SetVectorParameterValue(TEXT("EmissiveColor"), FLinearColor::White);
					DynamicMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor::White);
				}
			}

			if (UParticleSystemComponent* ParticleComponent = Cast<UParticleSystemComponent>(PrimitiveComponent))
			{
				ParticleComponent->SetFloatParameter(TEXT("Alpha"), SmoothAlpha);
				ParticleComponent->SetFloatParameter(TEXT("Opacity"), SmoothAlpha);
				ParticleComponent->SetFloatParameter(TEXT("Intensity"), SmoothAlpha);
				ParticleComponent->SetFloatParameter(TEXT("Brightness"), SmoothAlpha);
				ParticleComponent->SetColorParameter(TEXT("Color"), FLinearColor::White);
				ParticleComponent->SetColorParameter(TEXT("Tint"), FLinearColor::White);
			}

			++AppliedVisuals;
		}
	}

	if (AppliedLights == 0 && AppliedVisuals == 0)
	{
		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Verbose,
			TEXT("[TMMainMenuBackGlow] No rear glow components found."));
	}
}

void UTMMenuViewerMeshTransitionSubsystem::RestoreMainMenuBackGlow()
{
	for (const TPair<TWeakObjectPtr<ULightComponent>, FBackGlowLightState>& LightState : MainMenuBackGlowLightStates)
	{
		if (ULightComponent* LightComponent = LightState.Key.Get())
		{
			LightComponent->SetIntensity(LightState.Value.OriginalIntensity);
			LightComponent->SetLightColor(LightState.Value.OriginalColor);
		}
	}

	for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, FBackGlowVisualState>& VisualState : MainMenuBackGlowVisualStates)
	{
		if (UPrimitiveComponent* PrimitiveComponent = VisualState.Key.Get())
		{
			for (int32 MaterialIndex = 0; MaterialIndex < VisualState.Value.OriginalMaterials.Num(); ++MaterialIndex)
			{
				if (UMaterialInterface* OriginalMaterial = VisualState.Value.OriginalMaterials[MaterialIndex].Get())
				{
					PrimitiveComponent->SetMaterial(MaterialIndex, OriginalMaterial);
				}
			}
			PrimitiveComponent->SetWorldLocation(VisualState.Value.OriginalLocation);
			PrimitiveComponent->SetWorldRotation(VisualState.Value.OriginalRotation);
			PrimitiveComponent->SetWorldScale3D(VisualState.Value.OriginalScale);
			PrimitiveComponent->SetVisibility(VisualState.Value.bVisible, true);
			PrimitiveComponent->SetHiddenInGame(VisualState.Value.bHiddenInGame, true);
			if (UParticleSystemComponent* ParticleComponent = Cast<UParticleSystemComponent>(PrimitiveComponent))
			{
				ParticleComponent->SetFloatParameter(TEXT("Alpha"), 1.0f);
				ParticleComponent->SetFloatParameter(TEXT("Opacity"), 1.0f);
				ParticleComponent->SetFloatParameter(TEXT("Intensity"), 1.0f);
				ParticleComponent->SetFloatParameter(TEXT("Brightness"), 1.0f);
			}
		}
	}

	MainMenuBackGlowLightStates.Empty();
	MainMenuBackGlowVisualStates.Empty();
	MainMenuBackGlowElapsedSeconds = 0.0f;
	MainMenuBackGlowCurrentScale = 0.0f;
	bMainMenuBackGlowActive = false;
}

UTMMenuViewerMeshTransitionSubsystem::FLoadoutPostProcessFocus
UTMMenuViewerMeshTransitionSubsystem::ResolveLoadoutPostProcessFocus(
	UWorld* World,
	const UCameraComponent* CameraComponent) const
{
	FLoadoutPostProcessFocus Focus;
	if (!World || !CameraComponent)
	{
		return Focus;
	}

	const FVector CameraLocation = CameraComponent->GetComponentLocation();
	const FVector CameraForward = CameraComponent->GetForwardVector().GetSafeNormal();
	if (AActor* PreviewWeaponActor = ResolveLoadoutPreviewWeaponActor(World, CameraLocation))
	{
		float MinWeaponDepth = TNumericLimits<float>::Max();
		float MaxWeaponDepth = -TNumericLimits<float>::Max();

		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(PreviewWeaponActor);
		for (const UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			if (!IsValid(PrimitiveComponent) || !PrimitiveComponent->IsVisible())
			{
				continue;
			}

			const FBoxSphereBounds& Bounds = PrimitiveComponent->Bounds;
			const float CenterDepth = FVector::DotProduct(Bounds.Origin - CameraLocation, CameraForward);
			const FVector BoxExtent = Bounds.BoxExtent;
			const float DepthRadius =
				FMath::Abs(CameraForward.X) * BoxExtent.X
				+ FMath::Abs(CameraForward.Y) * BoxExtent.Y
				+ FMath::Abs(CameraForward.Z) * BoxExtent.Z;
			const float ComponentMinDepth = FMath::Max(10.0f, CenterDepth - DepthRadius);
			const float ComponentMaxDepth = CenterDepth + DepthRadius;
			if (!FMath::IsFinite(ComponentMinDepth)
				|| !FMath::IsFinite(ComponentMaxDepth)
				|| ComponentMaxDepth <= 10.0f)
			{
				continue;
			}

			MinWeaponDepth = FMath::Min(MinWeaponDepth, ComponentMinDepth);
			MaxWeaponDepth = FMath::Max(MaxWeaponDepth, ComponentMaxDepth);
		}

		if (FMath::IsFinite(MinWeaponDepth)
			&& FMath::IsFinite(MaxWeaponDepth)
			&& MinWeaponDepth < MaxWeaponDepth)
		{
			const float WeaponDepthSpan = MaxWeaponDepth - MinWeaponDepth;
			Focus.FocalDistance = FMath::Clamp((MinWeaponDepth + MaxWeaponDepth) * 0.5f, 40.0f, 2000.0f);
			Focus.FocalRegion = FMath::Clamp(WeaponDepthSpan + 28.0f, 55.0f, 150.0f);
			return Focus;
		}

		const float PreviewWeaponDepth = FVector::DotProduct(
			PreviewWeaponActor->GetActorLocation() - CameraLocation,
			CameraForward);
		if (FMath::IsFinite(PreviewWeaponDepth) && PreviewWeaponDepth > 10.0f)
		{
			Focus.FocalDistance = FMath::Clamp(PreviewWeaponDepth, 40.0f, 2000.0f);
			Focus.FocalRegion = 70.0f;
			return Focus;
		}
	}

	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsMenuViewerActor(Actor))
		{
			continue;
		}

		FVector FocusLocation = Actor->GetActorLocation();
		if (const USkeletalMeshComponent* CharacterComponent = ReadMeshComponentProperty(Actor, TEXT("Character")))
		{
			FocusLocation = CharacterComponent->Bounds.Origin;
		}

		const float Distance = FVector::Dist(CameraLocation, FocusLocation);
		if (FMath::IsFinite(Distance) && Distance > 10.0f && Distance < BestDistance)
		{
			BestDistance = Distance;
		}
	}

	if (!FMath::IsFinite(BestDistance) || BestDistance == TNumericLimits<float>::Max())
	{
		return Focus;
	}

	Focus.FocalDistance = FMath::Clamp(BestDistance, 80.0f, 2000.0f);
	return Focus;
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
