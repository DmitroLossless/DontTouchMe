// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gun.generated.h"

class USceneComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UPhysicalMaterial;
class UProjectileImpactData;
class UFakeGunAnimInstance;

UCLASS(Blueprintable)
class TOUCHME_API AGun : public AActor
{
	GENERATED_BODY()

public:
	AGun();

	UFUNCTION(BlueprintCallable, Category = "Gun|Impact")
	void Impact(FVector Location, FVector Normal, const UPhysicalMaterial* PhysicalMaterial);

	UFUNCTION(BlueprintCallable, Category = "Gun|Fake Mode")
	void SetFakeMode(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Gun|Fake Mode")
	bool IsFakeMode() const { return bFakeMode; }

	UFUNCTION(BlueprintPure, Category = "Gun|Fake Mode")
	UFakeGunAnimInstance* GetFakeAnimInstance() const;

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gun")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun")
	TObjectPtr<UProjectileImpactData> Caliber;

	UPROPERTY(EditAnywhere, BlueprintGetter = IsFakeMode, BlueprintSetter = SetFakeMode, Category = "Gun|Fake Mode")
	bool bFakeMode = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Fake Mode", meta = (EditCondition = "bFakeMode"))
	TObjectPtr<USkeletalMesh> FakeSkeletalMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Fake Mode", meta = (EditCondition = "bFakeMode"))
	TSubclassOf<UFakeGunAnimInstance> FakeAnimInstanceClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun|Fake Mode", meta = (EditCondition = "bFakeMode"))
	FTransform FakeSkeletalMeshOffset = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gun|Fake Mode")
	TObjectPtr<USkeletalMeshComponent> FakeSkeletalMeshComponent;

private:
	void ApplyFakeMode();
	USkeletalMeshComponent* ResolveMainSkeletalMesh() const;

	static const FName MainSkeletalMeshComponentName;

	bool bFakeModeApplied = false;
	bool bMainMeshWasVisible = true;
	uint8 MainMeshPreviousAnimTickOption = 0;
};
