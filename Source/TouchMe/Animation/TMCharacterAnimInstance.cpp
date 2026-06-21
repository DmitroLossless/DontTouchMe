// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMCharacterAnimInstance.h"

UTMCharacterAnimInstance::UTMCharacterAnimInstance()
	: CameraWeaponOffset(
		FRotator::ZeroRotator,
		FVector(0.0f, 0.0f, -35.0f),
		FVector::OneVector)
	, CameraWeaponOffsetNoAiming(CameraWeaponOffset)
	, CameraWeaponOffsetAiming(CameraWeaponOffset)
{
}
