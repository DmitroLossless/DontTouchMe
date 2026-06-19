// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TMCharacter.generated.h"

class UAnimInstance;
class UAnimMontage;
class AGun;

UCLASS(Blueprintable)
class TOUCHME_API ATMCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ATMCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void OnRep_Controller() override;
	virtual void ProcessEvent(UFunction* Function, void* Parameters) override;

	UFUNCTION(BlueprintPure, Category = "TouchMe|ADS")
	AGun* GetActiveGun() const;

	UFUNCTION(BlueprintPure, Category = "TouchMe|ADS")
	bool GetActiveWeaponADSSocketWorldTransform(FTransform& OutTransform) const;

	UFUNCTION(BlueprintPure, Category = "TouchMe|ADS")
	bool GetActiveWeaponADSCameraTargetTransform(float EyeRelief, FTransform& OutTransform) const;

protected:
	UPROPERTY(BlueprintReadOnly, Category = "TouchMe|Character")
	bool bIsLocalPlayerControlled = false;

	void UpdateLocalPlayerControlledFlag();

private:
	UFUNCTION()
	void OnRuntimeTraceMontageStarted(UAnimMontage* Montage);

	UFUNCTION()
	void OnRuntimeTraceMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted);

	UFUNCTION()
	void OnRuntimeTraceMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	UFUNCTION()
	void OnRuntimeTraceMontageSectionChanged(UAnimMontage* Montage, FName SectionName, bool bLooped);

	void BindRuntimeTraceAnimDelegates();
	void LogRuntimeTraceSnapshot(const TCHAR* Reason);

	TWeakObjectPtr<UAnimInstance> RuntimeTraceAnimInstance;
	float RuntimeTraceAccumulator = 0.f;
	bool bHasLastALSAimBridgeState = false;
	bool bLastALSAimBridgeState = false;
	FString LastRuntimeTraceSignature;
	FString LastRuntimeTraceActiveWeaponSignature;
};
