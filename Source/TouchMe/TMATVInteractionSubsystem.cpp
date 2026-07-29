#include "TMATVInteractionSubsystem.h"

#include "Character/TMCharacter.h"
#if WITH_EDITOR
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"
#endif
#include "Animation/AnimInstance.h"
#include "Animation/AnimationAsset.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogTMATVInteraction, Log, All);

namespace
{
	const FName TMATVInteractActionName(TEXT("Interact"));
	const FName TMATVDriverComponentName(TEXT("Driver"));
	const FName TMATVATVCollisionComponentName(TEXT("Box_ATVvsATV_Collision"));
	const FName TMATVEngineStartedPropertyName(TEXT("EngineStarted"));
	const FName TMATVControlledByPlayerPropertyName(TEXT("IsControlledByPlayer"));
	const FVector TMATVCharacterProxyOffset(0.0f, 0.0f, 70.0f);
	const ECollisionChannel TMATVPathChannel = ECC_GameTraceChannel7;
	const ECollisionChannel TMATVCollisionChannel = ECC_GameTraceChannel8;

	const TCHAR* TMATVClassPath = TEXT("/Game/PB_ATV/ATV/Blueprints/BP_ATV.BP_ATV_C");
	const TCHAR* TMATVClassPathToken = TEXT("/Game/PB_ATV/ATV/Blueprints/BP_ATV");
#if WITH_EDITOR
	const TCHAR* TMATVSourceDrivingAnimationPath = TEXT("/Game/PB_ATV/DemoContent/Mannequin/Animations/AS_Driving.AS_Driving");
	const TCHAR* TMATVTargetDrivingAnimationObjectPath = TEXT("/Game/Test/MPVS_SkeletonProbe/ImportedOnALS/ATV/AS_Driving_MPSBones.AS_Driving_MPSBones");
	const TCHAR* TMATVTargetDrivingAnimationPackageName = TEXT("/Game/Test/MPVS_SkeletonProbe/ImportedOnALS/ATV/AS_Driving_MPSBones");
	const TCHAR* TMATVTargetSkeletonPath = TEXT("/Game/Test/MPVS_SkeletonProbe/ImportedOnALS/SK_Mannequin_MPSBonesSkeleton.SK_Mannequin_MPSBonesSkeleton");
	const FName TMATVTargetDrivingAnimationAssetName(TEXT("AS_Driving_MPSBones"));
#endif

	const TCHAR* TMATVDrivingAnimationPaths[] =
	{
		TEXT("/Game/Test/MPVS_SkeletonProbe/ImportedOnALS/ATV/AS_Driving_MPSBones.AS_Driving_MPSBones"),
		TEXT("/Game/Test/MPVS_SkeletonProbe/ImportedOnALS/AS_Driving_MPSBones.AS_Driving_MPSBones"),
		TEXT("/Game/PB_ATV/DemoContent/Mannequin/Animations/AS_Driving_MPSBones.AS_Driving_MPSBones")
	};

	static TAutoConsoleVariable<int32> CVarTMATVInteraction(
		TEXT("tm.ATVInteraction"),
		1,
		TEXT("Enables Interact-based entering and exiting of PB_ATV BP_ATV pawns."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarTMATVInteractionRadius(
		TEXT("tm.ATVInteraction.Radius"),
		350.0f,
		TEXT("Maximum distance from the player character to a BP_ATV pawn for entering it."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarTMATVInteractionCooldown(
		TEXT("tm.ATVInteraction.Cooldown"),
		0.45f,
		TEXT("Seconds after entering or exiting an ATV before another ATV transition can happen."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarTMATVExitRightOffset(
		TEXT("tm.ATVInteraction.ExitRightOffset"),
		-155.0f,
		TEXT("Right-vector offset from the ATV used when placing the character after exiting."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarTMATVExitBackOffset(
		TEXT("tm.ATVInteraction.ExitBackOffset"),
		-70.0f,
		TEXT("Forward-vector offset from the ATV used when placing the character after exiting."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarTMATVExitUpOffset(
		TEXT("tm.ATVInteraction.ExitUpOffset"),
		55.0f,
		TEXT("Up offset from the ATV used when placing the character after exiting."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarTMATVStartupStabilizeDuration(
		TEXT("tm.ATVInteraction.StartupStabilizeDuration"),
		0.75f,
		TEXT("Seconds after BP_ATV spawn/BeginPlay where runtime velocity is clamped to prevent Chaos startup explosions."),
		ECVF_Default);
#if WITH_EDITOR
	static TAutoConsoleVariable<int32> CVarTMATVAutoRepairDrivingAnimation(
		TEXT("tm.ATVInteraction.AutoRepairDrivingAnimation"),
		1,
		TEXT("In editor, creates/fixes AS_Driving_MPSBones for the runtime MPS skeleton before ATV enter logic uses it."),
		ECVF_Default);
#endif

	bool ReadBoolProperty(UObject* Object, const FName PropertyName, bool& OutValue)
	{
		if (!Object)
		{
			return false;
		}

		FBoolProperty* BoolProperty = FindFProperty<FBoolProperty>(Object->GetClass(), PropertyName);
		if (!BoolProperty)
		{
			return false;
		}

		OutValue = BoolProperty->GetPropertyValue_InContainer(Object);
		return true;
	}

	bool WriteBoolProperty(UObject* Object, const FName PropertyName, const bool Value)
	{
		if (!Object)
		{
			return false;
		}

		FBoolProperty* BoolProperty = FindFProperty<FBoolProperty>(Object->GetClass(), PropertyName);
		if (!BoolProperty)
		{
			return false;
		}

		BoolProperty->SetPropertyValue_InContainer(Object, Value);
		return true;
	}

	USkeletalMeshComponent* FindDriverMeshComponent(APawn* ATV)
	{
		if (!ATV)
		{
			return nullptr;
		}

		TInlineComponentArray<USkeletalMeshComponent*> MeshComponents(ATV);
		for (USkeletalMeshComponent* MeshComponent : MeshComponents)
		{
			if (MeshComponent && MeshComponent->GetFName() == TMATVDriverComponentName)
			{
				return MeshComponent;
			}
		}

		for (USkeletalMeshComponent* MeshComponent : MeshComponents)
		{
			if (MeshComponent && MeshComponent->GetName().Contains(TEXT("Driver"), ESearchCase::IgnoreCase))
			{
				return MeshComponent;
			}
		}

		return nullptr;
	}

	bool IsATVDriverComponent(const UPrimitiveComponent* Component)
	{
		return Component && Component->GetFName() == TMATVDriverComponentName;
	}

	bool IsATVCollisionBoxComponent(const UPrimitiveComponent* Component)
	{
		return Component && Component->GetFName() == TMATVATVCollisionComponentName;
	}
}

void UTMATVInteractionSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	StabilizeATVPawns();
}

void UTMATVInteractionSubsystem::Tick(float DeltaTime)
{
	if (CVarTMATVInteraction.GetValueOnGameThread() == 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

#if WITH_EDITOR
	if (!bAttemptedDrivingAnimationRepair)
	{
		bAttemptedDrivingAnimationRepair = true;
		if (CVarTMATVAutoRepairDrivingAnimation.GetValueOnGameThread() != 0)
		{
			EnsureDrivingAnimationAsset();
		}
	}
#endif

	StabilizeATVPawns();

	for (auto It = ActiveRides.CreateIterator(); It; ++It)
	{
		APlayerController* PlayerController = It.Key().Get();
		FATVRideState& RideState = It.Value();
		if (!PlayerController || !RideState.ATV.IsValid() || !RideState.Character.IsValid())
		{
			It.RemoveCurrent();
		}
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PlayerController = It->Get();
		if (!PlayerController || !WasInteractPressed(PlayerController))
		{
			continue;
		}

		const float CurrentTime = World->GetTimeSeconds();
		if (const float* LastTransitionTime = LastTransitionTimes.Find(PlayerController))
		{
			if (CurrentTime - *LastTransitionTime < CVarTMATVInteractionCooldown.GetValueOnGameThread())
			{
				continue;
			}
		}

		if (FATVRideState* RideState = ActiveRides.Find(PlayerController))
		{
			if (TryExitATV(PlayerController, *RideState))
			{
				ActiveRides.Remove(PlayerController);
				LastTransitionTimes.FindOrAdd(PlayerController) = CurrentTime;
			}
			continue;
		}

		if (ATMCharacter* Character = Cast<ATMCharacter>(PlayerController->GetPawn()))
		{
			if (TryEnterNearestATV(PlayerController, Character))
			{
				LastTransitionTimes.FindOrAdd(PlayerController) = CurrentTime;
			}
		}
	}
}

TStatId UTMATVInteractionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTMATVInteractionSubsystem, STATGROUP_Tickables);
}

bool UTMATVInteractionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UTMATVInteractionSubsystem::WasInteractPressed(APlayerController* PlayerController) const
{
	if (!PlayerController)
	{
		return false;
	}

	for (const FKey& Key : ResolveInteractKeys())
	{
		if (Key.IsValid() && PlayerController->WasInputKeyJustPressed(Key))
		{
			return true;
		}
	}

	return false;
}

bool UTMATVInteractionSubsystem::TryEnterNearestATV(APlayerController* PlayerController, ATMCharacter* Character)
{
	if (!PlayerController || !Character || !PlayerController->HasAuthority())
	{
		return false;
	}

	APawn* ATV = FindNearestATV(Character);
	if (!ATV || ATV->GetController())
	{
		return false;
	}

	FATVRideState RideState;
	RideState.Character = Character;
	RideState.ATV = ATV;
	RideState.CharacterTransform = Character->GetActorTransform();
	RideState.bCharacterCollisionEnabled = Character->GetActorEnableCollision();
	RideState.bCharacterHidden = Character->IsHidden();

	if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
	{
		RideState.MovementMode = MovementComponent->MovementMode;
		RideState.CustomMovementMode = MovementComponent->CustomMovementMode;
	}

	SnapshotATVPlayerState(ATV, RideState);
	ApplyCharacterToATVDriver(ATV, Character, RideState);
	SetATVPlayerState(ATV, true);

	PlayerController->Possess(ATV);
	ATV->EnableInput(PlayerController);

	if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
	{
		MovementComponent->DisableMovement();
	}

	Character->SetActorHiddenInGame(true);
	AttachCharacterProxyToATV(Character, ATV, RideState);

	CallATVNoParamFunction(ATV, TEXT("StartEngine"));
	CallATVNoParamFunction(ATV, TEXT("Ai_StartEngine"));

	ActiveRides.Add(PlayerController, MoveTemp(RideState));
	UE_LOG(LogTMATVInteraction, Display, TEXT("[TMATV] Entered ATV=%s Character=%s Controller=%s"),
		*GetPathNameSafe(ATV),
		*GetPathNameSafe(Character),
		*GetPathNameSafe(PlayerController));
	return true;
}

bool UTMATVInteractionSubsystem::TryExitATV(APlayerController* PlayerController, FATVRideState& RideState)
{
	if (!PlayerController || !PlayerController->HasAuthority())
	{
		return false;
	}

	ATMCharacter* Character = RideState.Character.Get();
	APawn* ATV = RideState.ATV.Get();
	if (!Character || !ATV || PlayerController->GetPawn() != ATV)
	{
		return false;
	}

	const FVector ExitLocation =
		ATV->GetActorLocation()
		+ ATV->GetActorRightVector() * CVarTMATVExitRightOffset.GetValueOnGameThread()
		+ ATV->GetActorForwardVector() * CVarTMATVExitBackOffset.GetValueOnGameThread()
		+ FVector::UpVector * CVarTMATVExitUpOffset.GetValueOnGameThread();
	const FRotator ExitRotation(0.0f, ATV->GetActorRotation().Yaw, 0.0f);

	RestoreCharacterProxy(Character, RideState);
	Character->SetActorLocationAndRotation(ExitLocation, ExitRotation, false, nullptr, ETeleportType::TeleportPhysics);
	Character->SetActorHiddenInGame(RideState.bCharacterHidden);

	if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
	{
		MovementComponent->SetMovementMode(RideState.MovementMode, RideState.CustomMovementMode);
	}

	PlayerController->Possess(Character);

	CallATVNoParamFunction(ATV, TEXT("ShutDownEngine"));
	SetATVPlayerState(ATV, false);
	RestoreATVPlayerState(ATV, RideState);
	RestoreATVDriver(RideState);

	UE_LOG(LogTMATVInteraction, Display, TEXT("[TMATV] Exited ATV=%s Character=%s Controller=%s"),
		*GetPathNameSafe(ATV),
		*GetPathNameSafe(Character),
		*GetPathNameSafe(PlayerController));
	return true;
}

APawn* UTMATVInteractionSubsystem::FindNearestATV(const ATMCharacter* Character) const
{
	UWorld* World = GetWorld();
	if (!World || !Character)
	{
		return nullptr;
	}

	const FVector CharacterLocation = Character->GetActorLocation();
	const float Radius = FMath::Max(0.0f, CVarTMATVInteractionRadius.GetValueOnGameThread());
	const float RadiusSquared = FMath::Square(Radius);
	float BestDistanceSquared = RadiusSquared;
	APawn* BestATV = nullptr;

	UClass* ATVClass = LoadClass<APawn>(nullptr, TMATVClassPath);
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Candidate = *It;
		if (!Candidate || Candidate == Character || Candidate->GetController())
		{
			continue;
		}

		const bool bIsATVClass = ATVClass
			? Candidate->IsA(ATVClass)
			: IsATVPawn(Candidate);
		if (!bIsATVClass)
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(CharacterLocation, Candidate->GetActorLocation());
		if (DistanceSquared <= BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestATV = Candidate;
		}
	}

	return BestATV;
}

bool UTMATVInteractionSubsystem::IsATVPawn(const APawn* Pawn) const
{
	return Pawn && Pawn->GetClass()->GetPathName().Contains(TMATVClassPathToken, ESearchCase::IgnoreCase);
}

void UTMATVInteractionSubsystem::StabilizeATVPawns()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float CurrentTime = World->GetTimeSeconds();
	const float StabilizeDuration = FMath::Max(0.0f, CVarTMATVStartupStabilizeDuration.GetValueOnGameThread());
	UClass* ATVClass = LoadClass<APawn>(nullptr, TMATVClassPath);

	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Candidate = *It;
		if (!Candidate)
		{
			continue;
		}

		const bool bIsATVClass = ATVClass
			? Candidate->IsA(ATVClass)
			: IsATVPawn(Candidate);
		if (!bIsATVClass)
		{
			continue;
		}

		const TWeakObjectPtr<APawn> CandidateKey(Candidate);
		float& StabilizeEndTime = StartupStabilizeEndTimes.FindOrAdd(
			CandidateKey,
			CurrentTime + StabilizeDuration);
		const bool bResetVelocity = CurrentTime <= StabilizeEndTime;
		StabilizeATVPawn(Candidate, bResetVelocity);
	}

	for (auto It = StartupStabilizeEndTimes.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void UTMATVInteractionSubsystem::StabilizeATVPawn(APawn* ATV, const bool bResetVelocity) const
{
	if (!ATV)
	{
		return;
	}

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(ATV);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!PrimitiveComponent)
		{
			continue;
		}

		if (IsATVDriverComponent(PrimitiveComponent))
		{
			if (PrimitiveComponent->IsSimulatingPhysics())
			{
				PrimitiveComponent->SetSimulatePhysics(false);
			}
			PrimitiveComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			PrimitiveComponent->SetGenerateOverlapEvents(false);
		}
		else if (IsATVCollisionBoxComponent(PrimitiveComponent))
		{
			if (PrimitiveComponent->IsSimulatingPhysics())
			{
				PrimitiveComponent->SetSimulatePhysics(false);
			}
			PrimitiveComponent->SetCollisionObjectType(TMATVCollisionChannel);
			PrimitiveComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			PrimitiveComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
			PrimitiveComponent->SetCollisionResponseToChannel(TMATVCollisionChannel, ECR_Block);
			PrimitiveComponent->SetCollisionResponseToChannel(TMATVPathChannel, ECR_Ignore);
			PrimitiveComponent->SetGenerateOverlapEvents(false);
		}
		else
		{
			PrimitiveComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
			PrimitiveComponent->SetCollisionResponseToChannel(TMATVPathChannel, ECR_Ignore);
			PrimitiveComponent->SetCollisionResponseToChannel(TMATVCollisionChannel, ECR_Ignore);
		}

		if (bResetVelocity && PrimitiveComponent->IsSimulatingPhysics())
		{
			PrimitiveComponent->SetPhysicsLinearVelocity(FVector::ZeroVector);
			PrimitiveComponent->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		}
	}
}

void UTMATVInteractionSubsystem::AttachCharacterProxyToATV(ATMCharacter* Character, APawn* ATV, FATVRideState& RideState) const
{
	if (!Character || !ATV)
	{
		return;
	}

	if (USceneComponent* RootComponent = Character->GetRootComponent())
	{
		RideState.bCharacterWasAttached = RootComponent->GetAttachParent() != nullptr;
		RideState.CharacterAttachParent = RootComponent->GetAttachParent();
		RideState.CharacterAttachSocketName = RootComponent->GetAttachSocketName();
		RideState.CharacterRelativeTransform = RootComponent->GetRelativeTransform();
	}

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(Character);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!PrimitiveComponent)
		{
			continue;
		}

		FPrimitiveCollisionState PrimitiveState;
		PrimitiveState.Component = PrimitiveComponent;
		PrimitiveState.CollisionEnabled = PrimitiveComponent->GetCollisionEnabled();
		PrimitiveState.ObjectType = PrimitiveComponent->GetCollisionObjectType();
		PrimitiveState.Responses = PrimitiveComponent->GetCollisionResponseToChannels();
		PrimitiveState.bGenerateOverlapEvents = PrimitiveComponent->GetGenerateOverlapEvents();
		RideState.CharacterPrimitiveCollisionStates.Add(MoveTemp(PrimitiveState));

		PrimitiveComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PrimitiveComponent->SetGenerateOverlapEvents(false);
	}

	Character->SetActorEnableCollision(true);

	if (UCapsuleComponent* CapsuleComponent = Character->GetCapsuleComponent())
	{
		CapsuleComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		CapsuleComponent->SetCollisionObjectType(ECC_Pawn);
		CapsuleComponent->SetCollisionResponseToAllChannels(ECR_Overlap);
		CapsuleComponent->SetGenerateOverlapEvents(true);
		CapsuleComponent->UpdateOverlaps();
	}

	Character->SetActorLocation(ATV->GetActorLocation() + TMATVCharacterProxyOffset, false, nullptr, ETeleportType::TeleportPhysics);

	const FAttachmentTransformRules AttachRules(
		EAttachmentRule::KeepWorld,
		EAttachmentRule::KeepWorld,
		EAttachmentRule::KeepWorld,
		false);
	Character->AttachToActor(ATV, AttachRules);
}

void UTMATVInteractionSubsystem::RestoreCharacterProxy(ATMCharacter* Character, const FATVRideState& RideState) const
{
	if (!Character)
	{
		return;
	}

	Character->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	if (RideState.bCharacterWasAttached)
	{
		if (USceneComponent* RootComponent = Character->GetRootComponent())
		{
			if (USceneComponent* AttachParent = RideState.CharacterAttachParent.Get())
			{
				RootComponent->AttachToComponent(
					AttachParent,
					FAttachmentTransformRules::KeepRelativeTransform,
					RideState.CharacterAttachSocketName);
				RootComponent->SetRelativeTransform(RideState.CharacterRelativeTransform);
			}
		}
	}

	Character->SetActorEnableCollision(RideState.bCharacterCollisionEnabled);

	for (const FPrimitiveCollisionState& PrimitiveState : RideState.CharacterPrimitiveCollisionStates)
	{
		UPrimitiveComponent* PrimitiveComponent = PrimitiveState.Component.Get();
		if (!PrimitiveComponent)
		{
			continue;
		}

		PrimitiveComponent->SetCollisionEnabled(PrimitiveState.CollisionEnabled);
		PrimitiveComponent->SetCollisionObjectType(PrimitiveState.ObjectType);
		PrimitiveComponent->SetCollisionResponseToChannels(PrimitiveState.Responses);
		PrimitiveComponent->SetGenerateOverlapEvents(PrimitiveState.bGenerateOverlapEvents);
		PrimitiveComponent->UpdateOverlaps();
	}
}

void UTMATVInteractionSubsystem::ApplyCharacterToATVDriver(APawn* ATV, ATMCharacter* Character, FATVRideState& RideState)
{
	USkeletalMeshComponent* DriverMesh = FindDriverMeshComponent(ATV);
	USkeletalMeshComponent* CharacterMesh = Character ? Character->GetMesh() : nullptr;
	if (!DriverMesh || !CharacterMesh || !CharacterMesh->GetSkeletalMeshAsset())
	{
		return;
	}

	UAnimationAsset* DrivingAnimation = ResolveDrivingAnimation();
	if (!DrivingAnimation)
	{
		if (!bLoggedMissingDrivingAnimation)
		{
			bLoggedMissingDrivingAnimation = true;
			UE_LOG(
				LogTMATVInteraction,
				Warning,
				TEXT("[TMATV] Retargeted driving animation for SK_Mannequin_MPSBonesSkeleton was not found. Keeping the ATV package driver mesh to avoid a T-pose. Expected asset: /Game/Test/MPVS_SkeletonProbe/ImportedOnALS/ATV/AS_Driving_MPSBones"));
		}
		return;
	}

	FDriverMeshState DriverState;
	DriverState.Component = DriverMesh;
	DriverState.Mesh = DriverMesh->GetSkeletalMeshAsset();
	DriverState.AnimClass = DriverMesh->GetAnimClass();
	DriverState.AnimationMode = DriverMesh->GetAnimationMode();
	if (DriverState.AnimationMode == EAnimationMode::AnimationSingleNode)
	{
		DriverState.AnimationAsset = DriverMesh->AnimationData.AnimToPlay;
	}
	DriverState.bVisible = DriverMesh->IsVisible();
	for (int32 MaterialIndex = 0; MaterialIndex < DriverMesh->GetNumMaterials(); ++MaterialIndex)
	{
		DriverState.Materials.Add(DriverMesh->GetMaterial(MaterialIndex));
	}
	RideState.DriverMeshStates.Add(MoveTemp(DriverState));

	DriverMesh->SetSkeletalMesh(CharacterMesh->GetSkeletalMeshAsset());
	for (int32 MaterialIndex = 0; MaterialIndex < CharacterMesh->GetNumMaterials(); ++MaterialIndex)
	{
		DriverMesh->SetMaterial(MaterialIndex, CharacterMesh->GetMaterial(MaterialIndex));
	}
	DriverMesh->SetVisibility(true, true);

	DriverMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	DriverMesh->SetAnimation(DrivingAnimation);
	DriverMesh->Play(true);
}

void UTMATVInteractionSubsystem::RestoreATVDriver(FATVRideState& RideState) const
{
	for (const FDriverMeshState& DriverState : RideState.DriverMeshStates)
	{
		USkeletalMeshComponent* DriverMesh = DriverState.Component.Get();
		if (!DriverMesh)
		{
			continue;
		}

		DriverMesh->SetSkeletalMesh(DriverState.Mesh);
		DriverMesh->SetAnimationMode(DriverState.AnimationMode);
		if (DriverState.AnimationMode == EAnimationMode::AnimationBlueprint)
		{
			DriverMesh->SetAnimInstanceClass(DriverState.AnimClass);
		}
		else if (DriverState.AnimationAsset)
		{
			DriverMesh->SetAnimation(DriverState.AnimationAsset);
			DriverMesh->Play(true);
		}

		for (int32 MaterialIndex = 0; MaterialIndex < DriverState.Materials.Num(); ++MaterialIndex)
		{
			DriverMesh->SetMaterial(MaterialIndex, DriverState.Materials[MaterialIndex]);
		}
		DriverMesh->SetVisibility(DriverState.bVisible, true);
	}

	RideState.DriverMeshStates.Reset();
}

void UTMATVInteractionSubsystem::SetATVPlayerState(APawn* ATV, const bool bControlledByPlayer)
{
	WriteBoolProperty(ATV, TMATVControlledByPlayerPropertyName, bControlledByPlayer);
	WriteBoolProperty(ATV, TMATVEngineStartedPropertyName, bControlledByPlayer);
}

void UTMATVInteractionSubsystem::SnapshotATVPlayerState(APawn* ATV, FATVRideState& RideState) const
{
	bool Value = false;
	if (ReadBoolProperty(ATV, TMATVControlledByPlayerPropertyName, Value))
	{
		RideState.ATVBoolProperties.Add(TMATVControlledByPlayerPropertyName, Value);
	}
	if (ReadBoolProperty(ATV, TMATVEngineStartedPropertyName, Value))
	{
		RideState.ATVBoolProperties.Add(TMATVEngineStartedPropertyName, Value);
	}
}

void UTMATVInteractionSubsystem::RestoreATVPlayerState(APawn* ATV, const FATVRideState& RideState) const
{
	for (const TPair<FName, bool>& Pair : RideState.ATVBoolProperties)
	{
		WriteBoolProperty(ATV, Pair.Key, Pair.Value);
	}
}

void UTMATVInteractionSubsystem::CallATVNoParamFunction(APawn* ATV, const FName FunctionName) const
{
	if (!ATV)
	{
		return;
	}

	UFunction* Function = ATV->FindFunction(FunctionName);
	if (!Function || Function->ParmsSize != 0)
	{
		return;
	}

	ATV->ProcessEvent(Function, nullptr);
}

UAnimationAsset* UTMATVInteractionSubsystem::ResolveDrivingAnimation() const
{
	for (const TCHAR* DrivingAnimationPath : TMATVDrivingAnimationPaths)
	{
		if (UAnimationAsset* DrivingAnimation = LoadObject<UAnimationAsset>(nullptr, DrivingAnimationPath))
		{
			return DrivingAnimation;
		}
	}

	return nullptr;
}

#if WITH_EDITOR
bool UTMATVInteractionSubsystem::EnsureDrivingAnimationAsset() const
{
	UAnimSequence* SourceAnim = LoadObject<UAnimSequence>(nullptr, TMATVSourceDrivingAnimationPath);
	if (!SourceAnim)
	{
		UE_LOG(LogTMATVInteraction, Warning, TEXT("[TMATV] Could not load source driving anim: %s"), TMATVSourceDrivingAnimationPath);
		return false;
	}

	USkeleton* TargetSkeleton = LoadObject<USkeleton>(nullptr, TMATVTargetSkeletonPath);
	if (!TargetSkeleton)
	{
		UE_LOG(LogTMATVInteraction, Warning, TEXT("[TMATV] Could not load target driving skeleton: %s"), TMATVTargetSkeletonPath);
		return false;
	}

	UAnimSequence* TargetAnim = LoadObject<UAnimSequence>(nullptr, TMATVTargetDrivingAnimationObjectPath);
	if (!TargetAnim)
	{
		UPackage* TargetPackage = CreatePackage(TMATVTargetDrivingAnimationPackageName);
		if (!TargetPackage)
		{
			UE_LOG(LogTMATVInteraction, Warning, TEXT("[TMATV] Could not create package for: %s"), TMATVTargetDrivingAnimationPackageName);
			return false;
		}

		TargetAnim = DuplicateObject<UAnimSequence>(SourceAnim, TargetPackage, TMATVTargetDrivingAnimationAssetName);
		if (!TargetAnim)
		{
			UE_LOG(LogTMATVInteraction, Warning, TEXT("[TMATV] Could not duplicate source driving animation."));
			return false;
		}

		TargetAnim->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(TargetAnim);
	}

	if (TargetAnim->GetSkeleton() == TargetSkeleton)
	{
		return true;
	}

	TargetAnim->Modify();
	TargetAnim->SetSkeleton(TargetSkeleton);
	TargetAnim->PostEditChange();
	TargetAnim->MarkPackageDirty();

	UPackage* Package = TargetAnim->GetOutermost();
	const FString PackageFilename = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFilename), true);

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;

	if (!UPackage::SavePackage(Package, nullptr, *PackageFilename, SaveArgs))
	{
		UE_LOG(LogTMATVInteraction, Warning, TEXT("[TMATV] Could not save repaired driving animation: %s"), *PackageFilename);
		return false;
	}

	UE_LOG(
		LogTMATVInteraction,
		Display,
		TEXT("[TMATV] Repaired driving animation %s for skeleton %s."),
		*TargetAnim->GetPathName(),
		*TargetSkeleton->GetPathName());
	return true;
}
#endif

TArray<FKey> UTMATVInteractionSubsystem::ResolveInteractKeys() const
{
	if (!CachedInteractKeys.IsEmpty())
	{
		return CachedInteractKeys;
	}

	if (const UInputSettings* InputSettings = UInputSettings::GetInputSettings())
	{
		TArray<FInputActionKeyMapping> ActionMappings;
		InputSettings->GetActionMappingByName(TMATVInteractActionName, ActionMappings);
		for (const FInputActionKeyMapping& Mapping : ActionMappings)
		{
			if (Mapping.Key.IsValid())
			{
				CachedInteractKeys.AddUnique(Mapping.Key);
			}
		}
	}

	if (CachedInteractKeys.IsEmpty())
	{
		CachedInteractKeys.Add(EKeys::F);
	}

	return CachedInteractKeys;
}
