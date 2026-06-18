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

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TouchMe|First Person")
	FTransform CameraWeaponOffset;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "TouchMe|First Person")
	FTransform CameraWeaponOffsetCorrection;

private:
	void UpdateCameraWeaponOffsetCorrection();
};
