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

#if WITH_EDITOR
bool UFakeGunAnimInstance::EditorSetBoneReferenceTarget(const FName BoneName, const FName TargetBoneName)
{
	if (FFakeGunBoneOffset* BoneReference = BoneReferences.Find(BoneName))
	{
		Modify();
		BoneReference->TargetBoneName.BoneName = TargetBoneName;
		BoneReference->TargetBoneName.InvalidateCachedBoneIndex();
		MarkPackageDirty();
		return true;
	}

	return false;
}

FName UFakeGunAnimInstance::EditorGetBoneReferenceTarget(const FName BoneName) const
{
	if (const FFakeGunBoneOffset* BoneReference = BoneReferences.Find(BoneName))
	{
		return BoneReference->TargetBoneName.BoneName;
	}

	return NAME_None;
}
#endif
