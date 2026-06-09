// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/BoneReference.h"
#include "FakeGunAnimInstance.generated.h"

USTRUCT(BlueprintType)
struct TOUCHME_API FFakeGunBoneOffset
{
	GENERATED_BODY()
	
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Fake Gun|Bone")
	FTransform CurrentOffset = FTransform::Identity;
	
	UPROPERTY(EditDefaultsOnly, Category = "Fake Gun|Bones")
	FBoneReference TargetBoneName;
};

UCLASS(Blueprintable, BlueprintType)
class TOUCHME_API UFakeGunAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;

	bool SetBoneOffset(FName BoneName, const FTransform& Offset);

	UFUNCTION(BlueprintPure, Category = "Fake Gun|Bones", meta = (BlueprintThreadSafe))
	FTransform GetBoneOffset(FName BoneName) const;

protected:
	// Logical name -> skeletal bone reference. Bone references cache their runtime indices on initialization.
	UPROPERTY(EditDefaultsOnly, Category = "Fake Gun|Bones")
	TMap<FName, FFakeGunBoneOffset> BoneReferences;
};
