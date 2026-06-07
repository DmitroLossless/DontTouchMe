// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gun.generated.h"

class USceneComponent;
class UPhysicalMaterial;
class UProjectileImpactData;

UCLASS(Blueprintable)
class TOUCHME_API AGun : public AActor
{
	GENERATED_BODY()

public:
	AGun();

	UFUNCTION(BlueprintCallable, Category = "Gun|Impact")
	void Impact(FVector Location, FVector Normal, const UPhysicalMaterial* PhysicalMaterial);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gun")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun")
	TObjectPtr<UProjectileImpactData> Caliber;
};
