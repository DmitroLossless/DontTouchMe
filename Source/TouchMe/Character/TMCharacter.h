// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TMCharacter.generated.h"

class UAnimInstance;
class UAnimMontage;
class USoundClass;
class USoundMix;
class AGun;

UCLASS(Blueprintable)
class TOUCHME_API ATMCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ATMCharacter();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
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

	UFUNCTION(BlueprintCallable, Category = "TouchMe|Combat")
	void Shoot();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TouchMe|Combat", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "m"))
	float ShootTraceDistanceMeters = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TouchMe|Combat")
	TSubclassOf<AActor> ShootFallbackProjectileClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TouchMe|Combat", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm/s"))
	float ShootFallbackProjectileSpeed = 30000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TouchMe|Combat", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DefaultHitDamageMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TouchMe|Combat", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float HeadshotDamageMultiplier = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TouchMe|Audio Muffle")
	bool bEnableAnimCurveAudioMuffle = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TouchMe|Audio Muffle")
	FName AudioMuffleCurveName = TEXT("AimedSound");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TouchMe|Audio Muffle")
	TArray<TSoftObjectPtr<USoundClass>> AudioMuffleSoundClasses;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TouchMe|Audio Muffle", meta = (ClampMin = "20.0", UIMin = "20.0", UIMax = "20000.0", Units = "Hz"))
	float AudioMuffleFullyMuffledLowPassFrequency = 6000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TouchMe|Audio Muffle", meta = (ClampMin = "20.0", UIMin = "20.0", UIMax = "20000.0", Units = "Hz"))
	float AudioMuffleClearLowPassFrequency = 20000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TouchMe|Audio Muffle", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float AudioMuffleInterpSpeed = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TouchMe|Audio Muffle", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "Hz"))
	float AudioMuffleUpdateThresholdHz = 10.f;

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
	void UpdateAnimCurveAudioMuffle(float DeltaSeconds);
	bool EnsureAudioMuffleSoundMix();
	void ApplyAudioMuffleLowPassFrequency(float LowPassFrequency);
	void PopAudioMuffleSoundMix();

	TWeakObjectPtr<UAnimInstance> RuntimeTraceAnimInstance;
	UPROPERTY(Transient)
	TObjectPtr<USoundMix> AudioMuffleRuntimeSoundMix;

	float RuntimeTraceAccumulator = 0.f;
	float CurrentAudioMuffleAlpha = 0.f;
	float LastAppliedAudioMuffleLowPassFrequency = -1.f;
	bool bHasLastALSAimBridgeState = false;
	bool bLastALSAimBridgeState = false;
	bool bAudioMuffleSoundMixPushed = false;
	FString LastRuntimeTraceSignature;
	FString LastRuntimeTraceActiveWeaponSignature;
};
