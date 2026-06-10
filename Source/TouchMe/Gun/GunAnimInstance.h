// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/BoneReference.h"
#include "GunAnimInstance.generated.h"

class AGun;

USTRUCT()
struct TOUCHME_API FGunBoneOffset
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = "Gun|Bones")
	FBoneReference Bone;

	UPROPERTY(VisibleAnywhere, Category = "Gun|Bones")
	FTransform LocalOffset = FTransform::Identity;
	
	UPROPERTY(VisibleAnywhere, Category = "Gun|Bones")
	FTransform DefaultOffset = FTransform::Identity;
};

UCLASS(Blueprintable, BlueprintType)
class TOUCHME_API UGunAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "Gun")
	bool IsGunInFakeMode() const { return bFakeMode; }

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Gun|Bones")
	TArray<FGunBoneOffset> BoneOffsets;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Gun")
	TObjectPtr<AGun> GunOwner;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Gun")
	bool bFakeMode = false;

private:
	void RefreshGunOwnerState();
};
