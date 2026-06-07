// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "ProjectileImpactData.generated.h"

class UMaterialInterface;
class UFXSystemAsset;
class UPhysicalMaterial;
class USoundBase;

USTRUCT(BlueprintType)
struct TOUCHME_API FProjectileImpactEffects
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact")
	TObjectPtr<UFXSystemAsset> Particle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact")
	FTransform ParticleTransformOffset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact")
	TObjectPtr<USoundBase> Sound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact")
	TObjectPtr<UMaterialInterface> Decal;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact")
	FTransform DecalTransformOffset;
};

UCLASS(BlueprintType)
class TOUCHME_API UProjectileImpactData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact")
	TMap<TEnumAsByte<EPhysicalSurface>, FProjectileImpactEffects> EffectsBySurface;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact")
	FProjectileImpactEffects DefaultEffects;

	UFUNCTION(BlueprintPure, Category = "Impact")
	FProjectileImpactEffects GetEffectsForSurface(TEnumAsByte<EPhysicalSurface> SurfaceType) const;

	UFUNCTION(BlueprintPure, Category = "Impact")
	FProjectileImpactEffects GetEffectsForPhysicalMaterial(const UPhysicalMaterial* PhysicalMaterial) const;
};
