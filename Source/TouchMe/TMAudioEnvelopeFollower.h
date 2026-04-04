// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "UObject/Object.h"
#include "TMAudioEnvelopeFollower.generated.h"

class UAudioComponent;
class ULoudnessNRT;
class USoundWave;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTMOnEnvelopeValueSignature, float, EnvelopeValue, float, NormalizedValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FTMOnBeatSignature, int32, BeatIndex, float, Strength, float, TimeSeconds);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FTMOnDownbeatSignature, int32, BeatIndex, int32, BarIndex, float, Strength, float, TimeSeconds);

UCLASS(BlueprintType)
class TOUCHME_API UTMAudioEnvelopeFollower : public UObject
{
	GENERATED_BODY()

public:
	UTMAudioEnvelopeFollower();

	UPROPERTY(BlueprintAssignable, Category = "TM|Audio Analysis")
	FTMOnEnvelopeValueSignature OnEnvelopeValue;

	UPROPERTY(BlueprintAssignable, Category = "TM|Audio Analysis")
	FTMOnBeatSignature OnBeat;

	UPROPERTY(BlueprintAssignable, Category = "TM|Audio Analysis")
	FTMOnDownbeatSignature OnDownbeat;

	UPROPERTY(BlueprintReadOnly, Category = "TM|Audio Analysis")
	bool bIsAnalyzing = false;

	UPROPERTY(BlueprintReadOnly, Category = "TM|Audio Analysis")
	float LastEnvelopeValue = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "TM|Audio Analysis")
	float SmoothedEnvelopeValue = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "TM|Audio Analysis")
	float NormalizedEnvelopeValue = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "TM|Audio Analysis")
	float AdaptiveNoiseFloor = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "TM|Audio Analysis")
	float DynamicThreshold = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "TM|Audio Analysis")
	float EstimatedBeatInterval = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Audio Analysis|Detection", meta = (ClampMin = "1.0"))
	float ThresholdMultiplier = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Audio Analysis|Detection", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TriggerHysteresis = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Audio Analysis|Detection", meta = (ClampMin = "0.0"))
	float MinimumBeatThreshold = 0.025f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Audio Analysis|Detection", meta = (ClampMin = "0.05"))
	float MinBeatInterval = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Audio Analysis|Detection", meta = (ClampMin = "0.1"))
	float MaxBeatInterval = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Audio Analysis|Detection", meta = (ClampMin = "1"))
	int32 BeatsPerBar = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Audio Analysis|Smoothing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EnvelopeRiseAlpha = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Audio Analysis|Smoothing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EnvelopeFallAlpha = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Audio Analysis|Smoothing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NoiseFloorRiseAlpha = 0.015f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Audio Analysis|Smoothing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NoiseFloorFallAlpha = 0.08f;

	UFUNCTION(BlueprintCallable, Category = "TM|Audio Analysis", meta = (WorldContext = "WorldContextObject"))
	static UTMAudioEnvelopeFollower* CreateAudioEnvelopeFollower(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "TM|Audio Analysis")
	bool PlayAndAnalyzeSound2D(UAudioComponent* InAudioComponent, ULoudnessNRT* InLoudnessAnalyzer);

	UFUNCTION(BlueprintCallable, Category = "TM|Audio Analysis")
	bool AnalyzeAudioComponent(UAudioComponent* InAudioComponent, ULoudnessNRT* InLoudnessAnalyzer);

	UFUNCTION(BlueprintCallable, Category = "TM|Audio Analysis")
	void StopAnalyzing(bool bStopPlayback = true);

	UFUNCTION(BlueprintPure, Category = "TM|Audio Analysis")
	UAudioComponent* GetAudioComponent() const;

protected:
	UFUNCTION()
	void HandleAudioFinished();

	void PollAudioAnalysis();
	void ResetAnalysisState();
	void RegisterAudioComponent(UAudioComponent* InAudioComponent);
	void UnregisterAudioComponent();
	float GetCurrentTimeSeconds() const;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> AudioComponent;

	UPROPERTY(Transient)
	TObjectPtr<ULoudnessNRT> LoudnessAnalyzer;

	UPROPERTY(Transient)
	TWeakObjectPtr<const UObject> WorldContextObject;

	FTimerHandle AnalysisTimerHandle;
	float AnalysisIntervalSeconds = 1.0f / 60.0f;
	float AnalysisStartTimeSeconds = 0.0f;
	float CurrentPlaybackTimeSeconds = 0.0f;
	float LastBeatTimeSeconds = -1.0f;
	bool bAboveThreshold = false;
	int32 BeatCounter = 0;
	int32 BarCounter = 0;
};
