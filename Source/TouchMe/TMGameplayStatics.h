// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "Engine/EngineTypes.h"
#include "UObject/Interface.h"
#include "GameFramework/Actor.h"
#include "CollisionQueryParams.h"
#include "Engine/LatentActionManager.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStaticsTypes.h"
#include "TMGameplayStatics.generated.h"

class ASceneCapture2D;
class UAudioComponent;
class UInitialActiveSoundParams;
class UBlueprint;
class UDecalComponent;
class UDialogueWave;
class UFXSystemAsset;
class UFXSystemComponent;
class UParticleSystem;
class UParticleSystemComponent;
class USaveGame;
class USceneCaptureComponent2D;
class USceneComponent;
class USoundAttenuation;
class USoundBase;
class USoundConcurrency;
class UUserWidget;
class UStaticMesh;
class UProjectileMovementComponent;
class FMemoryReader;
class APlayerController;
class ACharacter;
class ATMFoliageExplosionCollisionTester;
class ATMFoliageCollisionPushTester;
struct FDialogueContext;


/** Static class with useful gameplay utility functions that can be called from both Blueprint and C++ */
UCLASS(MinimalAPI)
class UTMGameplayStatics : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()


	UFUNCTION(BlueprintCallable, Category="Audio", meta=(WorldContext="WorldContextObject", AdvancedDisplay = "3", UnsafeDuringActorConstruction = "true", Keywords = "play"))
	static TOUCHME_API void PlaySoundAtLocationDistanced(const UObject* WorldContextObject, USoundBase* Sound, FVector Location, FRotator Rotation, float VolumeMultiplier = 1.f, float PitchMultiplier = 1.f, float StartTime = 0.f, class USoundAttenuation* AttenuationSettings = nullptr, USoundConcurrency* ConcurrencySettings = nullptr, const AActor* OwningActor = nullptr, const UInitialActiveSoundParams* InitialParams = nullptr);

	static void PlaySoundAtLocationDistanced(const UObject* WorldContextObject, USoundBase* Sound, FVector Location, float VolumeMultiplier = 1.f, float PitchMultiplier = 1.f, float StartTime = 0.f, class USoundAttenuation* AttenuationSettings = nullptr, USoundConcurrency* ConcurrencySettings = nullptr, const UInitialActiveSoundParams* InitialParams = nullptr)
	{
		PlaySoundAtLocationDistanced(WorldContextObject, Sound, Location, FRotator::ZeroRotator, VolumeMultiplier, PitchMultiplier, StartTime, AttenuationSettings, ConcurrencySettings, nullptr, InitialParams);
	}

	UFUNCTION(BlueprintCallable, Category="Audio", meta=(WorldContext="WorldContextObject", AdvancedDisplay = "4", UnsafeDuringActorConstruction = "true", Keywords = "play"))
	static TOUCHME_API UAudioComponent* SpawnSoundAtLocationDistanced(const UObject* WorldContextObject, USoundBase* Sound, FVector Location, FRotator Rotation = FRotator::ZeroRotator, float VolumeMultiplier = 1.f, float PitchMultiplier = 1.f, float StartTime = 0.f, class USoundAttenuation* AttenuationSettings = nullptr, USoundConcurrency* ConcurrencySettings = nullptr, bool bAutoDestroy = true);

	UFUNCTION(BlueprintCallable, Category = "TM|FX", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "6", UnsafeDuringActorConstruction = "true", Keywords = "spawn fx cascade niagara emitter particle"))
	static TOUCHME_API UFXSystemComponent* SpawnFXSystemAtLocation(
		const UObject* WorldContextObject,
		UFXSystemAsset* EmitterTemplate,
		FVector Location,
		FRotator Rotation = FRotator::ZeroRotator,
		FVector Scale = FVector(1.f),
		bool bAutoDestroy = true,
		EPSCPoolMethod PoolingMethod = EPSCPoolMethod::None,
		bool bAutoActivateSystem = true);

	UFUNCTION(BlueprintCallable, Category = "TM|FX", meta = (AdvancedDisplay = "7", UnsafeDuringActorConstruction = "true", Keywords = "spawn fx cascade niagara emitter particle"))
	static TOUCHME_API UFXSystemComponent* SpawnFXSystemAttached(
		UFXSystemAsset* EmitterTemplate,
		USceneComponent* AttachToComponent,
		FName AttachPointName = NAME_None,
		FVector Location = FVector(ForceInit),
		FRotator Rotation = FRotator::ZeroRotator,
		FVector Scale = FVector(1.f),
		EAttachLocation::Type LocationType = EAttachLocation::KeepRelativeOffset,
		bool bAutoDestroy = true,
		EPSCPoolMethod PoolingMethod = EPSCPoolMethod::None,
		bool bAutoActivate = true);

	UFUNCTION(BlueprintCallable, Category = "TM|Foliage", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "3", Keywords = "foliage grass bush branch grenade explosion impulse collision sphere"))
	static TOUCHME_API void ApplyRadialFoliageImpulse(
		const UObject* WorldContextObject,
		FVector Origin,
		float Radius = 220.f,
		float ImpulseStrength = 75000.f,
		float Duration = 0.5f);

	UFUNCTION(BlueprintCallable, Category = "TM|Foliage|Debug", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "4", Keywords = "foliage grass bush branch explosion collision tester debug"))
	static TOUCHME_API ATMFoliageExplosionCollisionTester* SpawnFoliageExplosionCollisionTester(
		const UObject* WorldContextObject,
		FVector Origin,
		float Radius = 220.f,
		float Strength = 75000.f,
		FVector PullDirection = FVector::ForwardVector,
		float BendDistance = 240.f,
		float ExpansionDuration = 0.5f,
		bool bAutoDestroyAfterExpansion = true);

	UFUNCTION(BlueprintCallable, Category = "TM|Foliage|Debug", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "4", Keywords = "foliage grass bush branch collision push physics tester debug"))
	static TOUCHME_API ATMFoliageCollisionPushTester* SpawnFoliageCollisionPushTester(
		const UObject* WorldContextObject,
		FVector Origin,
		float Radius = 220.f,
		float ExpansionDuration = 0.5f,
		bool bAutoDestroyAfterExpansion = true,
		bool bCreatePhysicsProxyBodies = false);

	UFUNCTION(BlueprintCallable, Category = "TM|Loadout|Feedback", meta = (DisplayName = "Play Weapon Spawn Feedback For Actor"))
	static TOUCHME_API void PlayWeaponSpawnFeedbackForActor(AActor* WeaponActor);

	UFUNCTION(BlueprintCallable, Category = "TM|Loadout|Debug", meta = (DisplayName = "Log Loadout Preview Offset Applied"))
	static TOUCHME_API void LogLoadoutPreviewOffsetApplied(
		AActor* WeaponActor,
		USceneComponent* TargetComponent,
		FVector AppliedViewOffset);

	UFUNCTION(BlueprintCallable, Category = "TM|Loadout", meta = (DisplayName = "Cleanup Loadout Preview"))
	static TOUCHME_API void CleanupLoadoutPreview(UUserWidget* OwnerWidget);

	UFUNCTION(BlueprintCallable, Category = "TM|Loadout", meta = (DisplayName = "Apply Loadout Weapon Layer Icon"))
	static TOUCHME_API bool ApplyLoadoutWeaponLayerIcon(UUserWidget* WeaponLayerWidget);

	UFUNCTION(BlueprintCallable, Category = "TM|Loadout", meta = (DisplayName = "Attach Active Loadout Weapon To Transformator"))
	static TOUCHME_API bool AttachActiveLoadoutWeaponToTransformator(AActor* WeaponActor);

	UFUNCTION(BlueprintCallable, Category = "Audio", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "5", UnsafeDuringActorConstruction = "true", Keywords = "play"))
	static TOUCHME_API void MarketSoundRoom(bool enable);

	UFUNCTION(BlueprintCallable, Category="TM|Animation")
	static TOUCHME_API bool ApplyMPSOverlayPose(ACharacter* Character, UObject* ActiveWeapon);

	UFUNCTION(BlueprintCallable, Category="TM|Animation")
	static TOUCHME_API bool ApplyALSAimState(ACharacter* Character, bool bAiming);

	UFUNCTION(BlueprintCallable, Category="TM|Animation")
	static TOUCHME_API bool ApplyALSTurnInPlaceState(ACharacter* Character, float DeltaSeconds);

	UFUNCTION(BlueprintPure, Category="TM|Combat", meta=(DisplayName="Is Head Hit Bone", Keywords="headshot hit bone damage"))
	static TOUCHME_API bool IsHeadHitBone(FName HitBone);

	UFUNCTION(BlueprintPure, Category="TM|Combat", meta=(DisplayName="Is Head Hit", Keywords="headshot hit result bone damage"))
	static TOUCHME_API bool IsHeadHit(const FHitResult& HitResult);

	UFUNCTION(BlueprintPure, Category="TM|Combat", meta=(DisplayName="Get Bone Damage Multiplier", Keywords="headshot hit bone damage multiplier"))
	static TOUCHME_API float GetBoneDamageMultiplier(
		FName HitBone,
		UPARAM(meta=(ClampMin="0.0", UIMin="0.0")) float HeadMultiplier = 4.f,
		UPARAM(meta=(ClampMin="0.0", UIMin="0.0")) float DefaultMultiplier = 1.f);

	UFUNCTION(BlueprintCallable, Category="TM|Debug")
	static TOUCHME_API bool DumpAnimBlueprintGraphLinks();

	UFUNCTION(BlueprintCallable, Category="TM|Debug")
	static TOUCHME_API bool FixMPSBonesAimTargetGraph();

	UFUNCTION(BlueprintCallable, Category="TM|Debug")
	static TOUCHME_API bool PatchMenuViewerNoReinitPose();

	UFUNCTION(BlueprintCallable, Category="TM|Projectile", meta=(WorldContext="WorldContextObject"))
	static TOUCHME_API AActor* Shoot(
		const UObject* WorldContextObject,
		TSubclassOf<AActor> ProjectileClass,
		FVector Start,
		FVector Direction,
		float Distance,
		float ProjectileSpeed = 0.f,
		TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility,
		AActor* Owner = nullptr,
		APawn* Instigator = nullptr,
		USoundBase* ShootSound = nullptr,
		USoundAttenuation* AttenuationSettings = nullptr,
		USoundConcurrency* ConcurrencySettings = nullptr);
};

