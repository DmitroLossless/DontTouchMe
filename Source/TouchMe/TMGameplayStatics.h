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
class UParticleSystem;
class UParticleSystemComponent;
class USaveGame;
class USceneCaptureComponent2D;
class USceneComponent;
class USoundAttenuation;
class USoundBase;
class USoundConcurrency;
class UStaticMesh;
class UProjectileMovementComponent;
class FMemoryReader;
class APlayerController;
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

	UFUNCTION(BlueprintCallable, Category = "Audio", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "5", UnsafeDuringActorConstruction = "true", Keywords = "play"))
	static TOUCHME_API void MarketSoundRoom(bool enable);

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

