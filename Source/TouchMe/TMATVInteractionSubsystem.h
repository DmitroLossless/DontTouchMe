#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "TMATVInteractionSubsystem.generated.h"

class APlayerController;
class APawn;
class ATMCharacter;
class UAnimationAsset;
class UAnimInstance;
class UMaterialInterface;
class UPrimitiveComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class USceneComponent;

UCLASS()
class TOUCHME_API UTMATVInteractionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FDriverMeshState
	{
		TWeakObjectPtr<USkeletalMeshComponent> Component;
		TObjectPtr<USkeletalMesh> Mesh = nullptr;
		TSubclassOf<UAnimInstance> AnimClass;
		TObjectPtr<UAnimationAsset> AnimationAsset = nullptr;
		EAnimationMode::Type AnimationMode = EAnimationMode::AnimationBlueprint;
		TArray<TObjectPtr<UMaterialInterface>> Materials;
		bool bVisible = true;
	};

	struct FPrimitiveCollisionState
	{
		TWeakObjectPtr<UPrimitiveComponent> Component;
		ECollisionEnabled::Type CollisionEnabled = ECollisionEnabled::NoCollision;
		ECollisionChannel ObjectType = ECC_WorldStatic;
		FCollisionResponseContainer Responses;
		bool bGenerateOverlapEvents = false;
	};

	struct FATVRideState
	{
		TWeakObjectPtr<ATMCharacter> Character;
		TWeakObjectPtr<APawn> ATV;
		FTransform CharacterTransform = FTransform::Identity;
		TArray<FDriverMeshState> DriverMeshStates;
		TArray<FPrimitiveCollisionState> CharacterPrimitiveCollisionStates;
		TMap<FName, bool> ATVBoolProperties;
		TWeakObjectPtr<USceneComponent> CharacterAttachParent;
		FName CharacterAttachSocketName;
		FTransform CharacterRelativeTransform = FTransform::Identity;
		TEnumAsByte<EMovementMode> MovementMode = MOVE_Walking;
		uint8 CustomMovementMode = 0;
		bool bCharacterCollisionEnabled = true;
		bool bCharacterHidden = false;
		bool bCharacterWasAttached = false;
	};

	bool WasInteractPressed(APlayerController* PlayerController) const;
	bool TryEnterNearestATV(APlayerController* PlayerController, ATMCharacter* Character);
	bool TryExitATV(APlayerController* PlayerController, FATVRideState& RideState);
	APawn* FindNearestATV(const ATMCharacter* Character) const;
	bool IsATVPawn(const APawn* Pawn) const;
	void StabilizeATVPawns();
	void StabilizeATVPawn(APawn* ATV, bool bResetVelocity) const;
	void AttachCharacterProxyToATV(ATMCharacter* Character, APawn* ATV, FATVRideState& RideState) const;
	void RestoreCharacterProxy(ATMCharacter* Character, const FATVRideState& RideState) const;
	void ApplyCharacterToATVDriver(APawn* ATV, ATMCharacter* Character, FATVRideState& RideState);
	void RestoreATVDriver(FATVRideState& RideState) const;
	void SetATVPlayerState(APawn* ATV, bool bControlledByPlayer);
	void SnapshotATVPlayerState(APawn* ATV, FATVRideState& RideState) const;
	void RestoreATVPlayerState(APawn* ATV, const FATVRideState& RideState) const;
	void CallATVNoParamFunction(APawn* ATV, const FName FunctionName) const;
	UAnimationAsset* ResolveDrivingAnimation() const;
	TArray<FKey> ResolveInteractKeys() const;
#if WITH_EDITOR
	bool EnsureDrivingAnimationAsset() const;
#endif

	TMap<TWeakObjectPtr<APlayerController>, FATVRideState> ActiveRides;
	TMap<TWeakObjectPtr<APlayerController>, float> LastTransitionTimes;
	TMap<TWeakObjectPtr<APawn>, float> StartupStabilizeEndTimes;
	mutable TArray<FKey> CachedInteractKeys;
	mutable bool bLoggedMissingDrivingAnimation = false;
#if WITH_EDITOR
	mutable bool bAttemptedDrivingAnimationRepair = false;
#endif
};
