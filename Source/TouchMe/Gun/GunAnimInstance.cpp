// Copyright Epic Games, Inc. All Rights Reserved.

#include "GunAnimInstance.h"

#include "FakeGunAnimInstance.h"
#include "Gun.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"

#if WITH_EDITOR
#include "UObject/ObjectSaveContext.h"
#endif

void UGunAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	RefreshGunOwnerState();

	const USkeletalMeshComponent* SkeletalMeshComponent = GetSkelMeshComponent();
	const USkeletalMesh* SkeletalMesh = SkeletalMeshComponent ? SkeletalMeshComponent->GetSkeletalMeshAsset() : nullptr;
	const USkeleton* Skeleton = SkeletalMesh ? SkeletalMesh->GetSkeleton() : nullptr;

	for (FGunBoneOffset& BoneOffset : BoneOffsets)
	{
		BoneOffset.Bone.Initialize(Skeleton);
		BoneOffset.LocalOffset = BoneOffset.DefaultOffset;
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
		const TArray<FTransform> BoneSpaceTransforms = SkeletalMeshComponent->GetBoneSpaceTransforms();
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

#if WITH_EDITOR
void UGunAnimInstance::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshDefaultBoneOffsets();
}

void UGunAnimInstance::PreSave(FObjectPreSaveContext SaveContext)
{
	RefreshDefaultBoneOffsets();
	Super::PreSave(SaveContext);
}

void UGunAnimInstance::RefreshDefaultBoneOffsets()
{
	const FReferenceSkeleton* ReferenceSkeleton = nullptr;

	if (const IAnimClassInterface* AnimClass = IAnimClassInterface::GetFromClass(GetClass()))
	{
		if (const USkeleton* Skeleton = AnimClass->GetTargetSkeleton())
		{
			ReferenceSkeleton = &Skeleton->GetReferenceSkeleton();
		}
	}

	if (ReferenceSkeleton)
	{
		const TArray<FTransform>& ReferencePose = ReferenceSkeleton->GetRefBonePose();
		for (FGunBoneOffset& BoneOffset : BoneOffsets)
		{
			const int32 BoneIndex = ReferenceSkeleton->FindBoneIndex(BoneOffset.Bone.BoneName);
			if (ReferencePose.IsValidIndex(BoneIndex))
			{
				BoneOffset.DefaultOffset = ReferencePose[BoneIndex];
			}
		}
	}


}
#endif
