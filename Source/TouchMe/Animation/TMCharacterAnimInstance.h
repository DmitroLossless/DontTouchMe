// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "TMCharacterAnimInstance.generated.h"

UCLASS(Blueprintable, BlueprintType)
class TOUCHME_API UTMCharacterAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UTMCharacterAnimInstance();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TouchMe|First Person")
	FTransform CameraWeaponOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TouchMe|First Person")
	FTransform CameraWeaponOffsetNoAiming;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TouchMe|First Person")
	FTransform CameraWeaponOffsetAiming;
};
