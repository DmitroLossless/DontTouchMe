// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gun.generated.h"

class USceneComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UPhysicalMaterial;
class UProjectileImpactData;
class UFakeGunAnimInstance;

UCLASS(Blueprintable)
class TOUCHME_API AGun : public AActor
{
	GENERATED_BODY()

public:
	AGun();

	UFUNCTION(BlueprintPure, Category = "Gun", meta = (DisplayName = "Get Weapon Display Name"))
	FText GetWeaponDisplayName() const;

	UFUNCTION(BlueprintPure, Category = "Gun", meta = (DisplayName = "Get Weapon Display Name From Class"))
	static FText GetWeaponDisplayNameFromClass(TSubclassOf<AGun> WeaponClass);

	UFUNCTION(BlueprintCallable, Category = "Gun|Impact")
	void Impact(FVector Location, FVector Normal, const UPhysicalMaterial* PhysicalMaterial);

	UFUNCTION(BlueprintCallable, Category = "Gun|Fake Mode")
	void SetFakeMode(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Gun|Fake Mode")
	bool IsFakeMode() const { return bFakeMode; }

	UFUNCTION(BlueprintPure, Category = "Gun|Fake Mode")
	UFakeGunAnimInstance* GetFakeAnimInstance() const;

	UFUNCTION(BlueprintCallable, Category = "Gun|Attachments")
	int32 SanitizeInvalidAttachmentComponents();

	virtual void ProcessEvent(UFunction* Function, void* Parameters) override;

protected:
	virtual void PostLoad() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gun")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun")
	TObjectPtr<UProjectileImpactData> Caliber;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Gun", meta = (DisplayName = "Display Name"))
	FText WeaponDisplayName;

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
	static FText MakeDefaultWeaponDisplayName(const UClass* WeaponClass);

	bool HasCustomWeaponDisplayName() const;
	void ApplyFakeMode();
	void RestoreFromFakeMode();
	void RequestDeferredAttachmentSanitize();
	void RunDeferredAttachmentSanitize();
	bool IsInvalidWeaponAttachmentComponent(const UStaticMeshComponent* Component) const;
	USkeletalMeshComponent* ResolveMainSkeletalMesh() const;
	static bool IsWeaponAttachmentMesh(const UStaticMeshComponent* Component);
	static bool ShouldRequestAttachmentSanitizeForFunction(const UFunction* Function);

	static const FName MainSkeletalMeshComponentName;

	bool bAttachmentSanitizeRequested = false;
	bool bSanitizingAttachmentComponents = false;
	bool bFakeModeApplied = false;
	bool bMainMeshWasVisible = true;
	uint8 MainMeshPreviousAnimTickOption = 0;
};
