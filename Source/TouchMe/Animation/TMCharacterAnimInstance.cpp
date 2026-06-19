// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMCharacterAnimInstance.h"

UTMCharacterAnimInstance::UTMCharacterAnimInstance()
	: CameraWeaponOffset(
		FRotator::ZeroRotator,
		FVector(0.0f, 0.0f, -35.0f),
		FVector::OneVector)
	, CameraWeaponOffsetAiming(CameraWeaponOffset)
{
	UpdateCameraWeaponOffsetCorrection();
}

void UTMCharacterAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	UpdateCameraWeaponOffsetCorrection();
}

void UTMCharacterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	UpdateCameraWeaponOffsetCorrection();
}

void UTMCharacterAnimInstance::UpdateCameraWeaponOffsetCorrection()
{
	CameraWeaponOffsetCorrection = FTransform(
		CameraWeaponOffset.GetRotation().Inverse(),
		-CameraWeaponOffset.GetTranslation(),
		FVector::OneVector);
}
