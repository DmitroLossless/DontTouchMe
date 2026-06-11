#include "TMLeaderPoseCompatibilitySubsystem.h"

#include "Components/SkinnedMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "ReferenceSkeleton.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogTMLeaderPoseCompatibility, Log, All);

namespace
{
bool IsUrbanMilCharAsset(const USkinnedAsset* Asset)
{
	return Asset && Asset->GetPathName().Contains(TEXT("/Game/UrbanMilChar/"));
}

bool HasExactLeaderPoseHierarchy(const USkinnedMeshComponent& FollowerComponent, const USkinnedMeshComponent& LeaderComponent)
{
	const USkinnedAsset* FollowerAsset = FollowerComponent.GetSkinnedAsset();
	const USkinnedAsset* LeaderAsset = LeaderComponent.GetSkinnedAsset();
	if (!FollowerAsset || !LeaderAsset)
	{
		return false;
	}

	const TArray<int32>& LeaderBoneMap = FollowerComponent.GetLeaderBoneMap();
	const FReferenceSkeleton& FollowerRefSkeleton = FollowerAsset->GetRefSkeleton();
	const FReferenceSkeleton& LeaderRefSkeleton = LeaderAsset->GetRefSkeleton();
	if (LeaderBoneMap.Num() != FollowerRefSkeleton.GetNum())
	{
		return false;
	}

	for (int32 FollowerBoneIndex = 0; FollowerBoneIndex < LeaderBoneMap.Num(); ++FollowerBoneIndex)
	{
		const int32 LeaderBoneIndex = LeaderBoneMap[FollowerBoneIndex];
		if (LeaderBoneIndex == INDEX_NONE)
		{
			continue;
		}

		const int32 FollowerParentIndex = FollowerRefSkeleton.GetParentIndex(FollowerBoneIndex);
		const int32 LeaderParentIndex = LeaderRefSkeleton.GetParentIndex(LeaderBoneIndex);
		const int32 MappedFollowerParentIndex = FollowerParentIndex != INDEX_NONE && LeaderBoneMap.IsValidIndex(FollowerParentIndex)
			? LeaderBoneMap[FollowerParentIndex]
			: INDEX_NONE;

		if (MappedFollowerParentIndex != LeaderParentIndex)
		{
			UE_LOG(
				LogTMLeaderPoseCompatibility,
				Warning,
				TEXT("Rejected %s as MPS leader-pose follower: bone %s parent chain differs from %s."),
				*GetNameSafe(FollowerAsset),
				*FollowerRefSkeleton.GetBoneName(FollowerBoneIndex).ToString(),
				*GetNameSafe(LeaderAsset));
			return false;
		}
	}

	return true;
}

void RestoreLeaderDefaultMaterials(USkinnedMeshComponent& LeaderComponent)
{
	const USkinnedAsset* LeaderAsset = LeaderComponent.GetSkinnedAsset();
	if (!LeaderAsset)
	{
		return;
	}

	const TArray<FSkeletalMaterial>& Materials = LeaderAsset->GetMaterials();
	for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
	{
		LeaderComponent.SetMaterial(MaterialIndex, Materials[MaterialIndex].MaterialInterface);
	}
}
}

void UTMLeaderPoseCompatibilitySubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	for (TObjectIterator<USkinnedMeshComponent> It; It; ++It)
	{
		USkinnedMeshComponent* FollowerComponent = *It;
		if (!FollowerComponent || FollowerComponent->GetWorld() != World || !FollowerComponent->IsRegistered())
		{
			continue;
		}

		PatchUrbanFollowerComponent(FollowerComponent);
	}
}

TStatId UTMLeaderPoseCompatibilitySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTMLeaderPoseCompatibilitySubsystem, STATGROUP_Tickables);
}

bool UTMLeaderPoseCompatibilitySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

void UTMLeaderPoseCompatibilitySubsystem::PatchUrbanFollowerComponent(USkinnedMeshComponent* FollowerComponent) const
{
	USkinnedMeshComponent* LeaderComponent = FollowerComponent ? FollowerComponent->LeaderPoseComponent.Get() : nullptr;
	if (!LeaderComponent)
	{
		return;
	}

	USkinnedAsset* FollowerAsset = FollowerComponent->GetSkinnedAsset();
	if (!IsUrbanMilCharAsset(FollowerAsset))
	{
		return;
	}

	if (HasExactLeaderPoseHierarchy(*FollowerComponent, *LeaderComponent))
	{
		return;
	}

	FollowerComponent->SetLeaderPoseComponent(nullptr, true);
	FollowerComponent->SetHiddenInGame(true, true);
	FollowerComponent->SetVisibility(false, true);
	FollowerComponent->SetComponentTickEnabled(false);

	RestoreLeaderDefaultMaterials(*LeaderComponent);
	LeaderComponent->SetHiddenInGame(false, true);
	LeaderComponent->SetVisibility(true, true);
	LeaderComponent->MarkRenderStateDirty();

	UE_LOG(
		LogTMLeaderPoseCompatibility,
		Warning,
		TEXT("Disabled incompatible Urban secondary mesh %s. Keeping MPS leader visible instead."),
		*GetNameSafe(FollowerAsset));
}
