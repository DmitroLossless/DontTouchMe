// Copyright Epic Games, Inc. All Rights Reserved.

#include "FakeGunAnimInstance.h"

#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

void UFakeGunAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	const USkeletalMeshComponent* SkeletalMeshComponent = GetSkelMeshComponent();
	const USkeletalMesh* SkeletalMesh = SkeletalMeshComponent ? SkeletalMeshComponent->GetSkeletalMeshAsset() : nullptr;
	const USkeleton* Skeleton = SkeletalMesh ? SkeletalMesh->GetSkeleton() : nullptr;

	for (TPair<FName, FFakeGunBoneOffset>& BoneReference : BoneReferences)
	{
		BoneReference.Value.TargetBoneName.Initialize(Skeleton);
	}
}

bool UFakeGunAnimInstance::SetBoneOffset(const FName BoneName, const FTransform& Offset)
{
	if (FFakeGunBoneOffset* BoneReference = BoneReferences.Find(BoneName))
	{
		BoneReference->CurrentOffset = Offset;
		UE_LOG(LogTemp, Log, TEXT("Fake gun bone offset [%s]: %s"), *BoneName.ToString(), *Offset.ToHumanReadableString());
		return true;
	}

	return false;
}

FTransform UFakeGunAnimInstance::GetBoneOffset(const FName BoneName) const
{
	if (const FFakeGunBoneOffset* BoneReference = BoneReferences.Find(BoneName))
	{
		return BoneReference->CurrentOffset;
	}

	return FTransform::Identity;
}
