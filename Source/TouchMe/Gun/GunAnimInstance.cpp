// Copyright Epic Games, Inc. All Rights Reserved.

#include "GunAnimInstance.h"

#include "FakeGunAnimInstance.h"
#include "Gun.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"

void UGunAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	RefreshGunOwnerState();

	const USkeletalMeshComponent* SkeletalMeshComponent = GetSkelMeshComponent();
	const USkeletalMesh* SkeletalMesh = SkeletalMeshComponent ? SkeletalMeshComponent->GetSkeletalMeshAsset() : nullptr;
	const USkeleton* Skeleton = SkeletalMesh ? SkeletalMesh->GetSkeleton() : nullptr;
	const FReferenceSkeleton* ReferenceSkeleton = SkeletalMesh ? &SkeletalMesh->GetRefSkeleton() : nullptr;

	for (FGunBoneOffset& BoneOffset : BoneOffsets)
	{
		BoneOffset.Bone.Initialize(Skeleton);

		const int32 BoneIndex = ReferenceSkeleton
			? ReferenceSkeleton->FindBoneIndex(BoneOffset.Bone.BoneName)
			: INDEX_NONE;
		BoneOffset.DefaultOffset = ReferenceSkeleton && ReferenceSkeleton->GetRefBonePose().IsValidIndex(BoneIndex)
			? ReferenceSkeleton->GetRefBonePose()[BoneIndex]
			: FTransform::Identity;
		BoneOffset.LocalOffset = FTransform::Identity;
	}
}

void UGunAnimInstance::NativeUpdateAnimation(const float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	RefreshGunOwnerState();

	if (bFakeMode)
	{
		USkeletalMeshComponent* SkeletalMeshComponent = GetSkelMeshComponent();
		if (!SkeletalMeshComponent)
		{
			return;
		}

		UFakeGunAnimInstance* FakeAnimInstance = GunOwner ? GunOwner->GetFakeAnimInstance() : nullptr;
		const TArray<FTransform>& BoneSpaceTransforms = SkeletalMeshComponent->GetBoneSpaceTransforms();
		for (FGunBoneOffset& BoneOffset : BoneOffsets)
		{
			const int32 BoneIndex = SkeletalMeshComponent->GetBoneIndex(BoneOffset.Bone.BoneName);
			BoneOffset.LocalOffset = BoneSpaceTransforms.IsValidIndex(BoneIndex)
				? BoneSpaceTransforms[BoneIndex].GetRelativeTransform(BoneOffset.DefaultOffset)
				: FTransform::Identity;

			if (FakeAnimInstance)
			{
				FakeAnimInstance->SetBoneOffset(BoneOffset.Bone.BoneName, BoneOffset.LocalOffset);
			}
		}
	}
}

void UGunAnimInstance::RefreshGunOwnerState()
{
	if (!GunOwner)
	{
		GunOwner = Cast<AGun>(GetOwningActor());
	}

	bFakeMode = GunOwner ? GunOwner->IsFakeMode() : false;
}
