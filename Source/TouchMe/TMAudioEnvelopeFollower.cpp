// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMAudioEnvelopeFollower.h"

#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundWave.h"

UTMAudioEnvelopeFollower::UTMAudioEnvelopeFollower()
{
}

UTMAudioEnvelopeFollower* UTMAudioEnvelopeFollower::CreateAudioEnvelopeFollower(const UObject* WorldContextObject)
{
	UObject* Outer = GetTransientPackage();

	if (WorldContextObject)
	{
		if (const UWorld* World = WorldContextObject->GetWorld())
		{
			if (UGameInstance* GameInstance = World->GetGameInstance())
			{
				Outer = GameInstance;
			}
			else
			{
				Outer = const_cast<UObject*>(WorldContextObject);
			}
		}
		else
		{
			Outer = const_cast<UObject*>(WorldContextObject);
		}
	}

	UTMAudioEnvelopeFollower* Analyzer = NewObject<UTMAudioEnvelopeFollower>(Outer);
	Analyzer->WorldContextObject = WorldContextObject;
	return Analyzer;
}

bool UTMAudioEnvelopeFollower::PlayAndAnalyzeSound2D(
	const UObject* InWorldContextObject,
	USoundBase* Sound,
	float VolumeMultiplier,
	float PitchMultiplier,
	float StartTime,
	USoundConcurrency* ConcurrencySettings,
	bool bPersistAcrossLevelTransition,
	bool bAutoDestroy)
{
	if (!InWorldContextObject || !Sound)
	{
		return false;
	}

	WorldContextObject = InWorldContextObject;

	UAudioComponent* SpawnedComponent = UGameplayStatics::SpawnSound2D(
		InWorldContextObject,
		Sound,
		VolumeMultiplier,
		PitchMultiplier,
		StartTime,
		ConcurrencySettings,
		bPersistAcrossLevelTransition,
		bAutoDestroy);

	return AnalyzeAudioComponent(SpawnedComponent);
}

bool UTMAudioEnvelopeFollower::AnalyzeAudioComponent(UAudioComponent* InAudioComponent)
{
	if (!InAudioComponent)
	{
		return false;
	}

	UnregisterAudioComponent();
	RegisterAudioComponent(InAudioComponent);
	ResetAnalysisState();
	bIsAnalyzing = true;
	return true;
}

void UTMAudioEnvelopeFollower::StopAnalyzing(bool bStopPlayback)
{
	bIsAnalyzing = false;

	if (bStopPlayback && AudioComponent)
	{
		AudioComponent->Stop();
	}

	UnregisterAudioComponent();
	ResetAnalysisState();
}

UAudioComponent* UTMAudioEnvelopeFollower::GetAudioComponent() const
{
	return AudioComponent;
}

void UTMAudioEnvelopeFollower::HandleSingleEnvelopeValue(const USoundWave* PlayingSoundWave, const float EnvelopeValue)
{
	if (!bIsAnalyzing || !AudioComponent)
	{
		return;
	}

	const float SafeEnvelope = FMath::Max(EnvelopeValue, 0.0f);
	const float RiseOrFallAlpha = SafeEnvelope >= SmoothedEnvelopeValue ? EnvelopeRiseAlpha : EnvelopeFallAlpha;
	LastEnvelopeValue = SafeEnvelope;
	SmoothedEnvelopeValue = FMath::Lerp(SmoothedEnvelopeValue, SafeEnvelope, RiseOrFallAlpha);

	const float NoiseAlpha = SmoothedEnvelopeValue <= AdaptiveNoiseFloor ? NoiseFloorFallAlpha : NoiseFloorRiseAlpha;
	AdaptiveNoiseFloor = FMath::Lerp(AdaptiveNoiseFloor, SmoothedEnvelopeValue, NoiseAlpha);

	DynamicThreshold = FMath::Max(MinimumBeatThreshold, AdaptiveNoiseFloor * ThresholdMultiplier);

	const float ThresholdRange = FMath::Max(DynamicThreshold - AdaptiveNoiseFloor, KINDA_SMALL_NUMBER);
	NormalizedEnvelopeValue = FMath::Clamp((SmoothedEnvelopeValue - AdaptiveNoiseFloor) / ThresholdRange, 0.0f, 4.0f);
	OnEnvelopeValue.Broadcast(SmoothedEnvelopeValue, NormalizedEnvelopeValue);

	const float CurrentTimeSeconds = GetCurrentTimeSeconds();
	const bool bCrossedTrigger = !bAboveThreshold && SmoothedEnvelopeValue >= DynamicThreshold;
	const bool bCanResetTrigger = bAboveThreshold && SmoothedEnvelopeValue <= (DynamicThreshold * TriggerHysteresis);

	if (bCanResetTrigger)
	{
		bAboveThreshold = false;
	}

	if (!bCrossedTrigger)
	{
		return;
	}

	const float TimeSinceLastBeat = LastBeatTimeSeconds < 0.0f ? TNumericLimits<float>::Max() : (CurrentTimeSeconds - LastBeatTimeSeconds);
	if (TimeSinceLastBeat < MinBeatInterval)
	{
		bAboveThreshold = true;
		return;
	}

	bAboveThreshold = true;
	BeatCounter++;

	if (LastBeatTimeSeconds >= 0.0f && TimeSinceLastBeat <= MaxBeatInterval)
	{
		EstimatedBeatInterval = EstimatedBeatInterval <= 0.0f
			? TimeSinceLastBeat
			: FMath::Lerp(EstimatedBeatInterval, TimeSinceLastBeat, 0.25f);
	}

	LastBeatTimeSeconds = CurrentTimeSeconds;

	const float BeatStrength = NormalizedEnvelopeValue;
	OnBeat.Broadcast(BeatCounter, BeatStrength, CurrentTimeSeconds);

	if (BeatsPerBar > 0 && ((BeatCounter - 1) % BeatsPerBar) == 0)
	{
		BarCounter++;
		OnDownbeat.Broadcast(BeatCounter, BarCounter, BeatStrength, CurrentTimeSeconds);
	}
}

void UTMAudioEnvelopeFollower::HandleAudioFinished()
{
	bIsAnalyzing = false;
	UnregisterAudioComponent();
}

void UTMAudioEnvelopeFollower::ResetAnalysisState()
{
	LastEnvelopeValue = 0.0f;
	SmoothedEnvelopeValue = 0.0f;
	NormalizedEnvelopeValue = 0.0f;
	AdaptiveNoiseFloor = 0.0f;
	DynamicThreshold = MinimumBeatThreshold;
	EstimatedBeatInterval = 0.0f;
	LastBeatTimeSeconds = -1.0f;
	bAboveThreshold = false;
	BeatCounter = 0;
	BarCounter = 0;
}

void UTMAudioEnvelopeFollower::RegisterAudioComponent(UAudioComponent* InAudioComponent)
{
	AudioComponent = InAudioComponent;

	if (!AudioComponent)
	{
		return;
	}

	AudioComponent->EnvelopeFollowerAttackTime = EnvelopeFollowerAttackTimeMs;
	AudioComponent->EnvelopeFollowerReleaseTime = EnvelopeFollowerReleaseTimeMs;
	AudioComponent->OnAudioSingleEnvelopeValue.AddDynamic(this, &UTMAudioEnvelopeFollower::HandleSingleEnvelopeValue);
	AudioComponent->OnAudioFinished.AddDynamic(this, &UTMAudioEnvelopeFollower::HandleAudioFinished);
}

void UTMAudioEnvelopeFollower::UnregisterAudioComponent()
{
	if (!AudioComponent)
	{
		return;
	}

	AudioComponent->OnAudioSingleEnvelopeValue.RemoveDynamic(this, &UTMAudioEnvelopeFollower::HandleSingleEnvelopeValue);
	AudioComponent->OnAudioFinished.RemoveDynamic(this, &UTMAudioEnvelopeFollower::HandleAudioFinished);
	AudioComponent = nullptr;
}

float UTMAudioEnvelopeFollower::GetCurrentTimeSeconds() const
{
	if (const UObject* ContextObject = WorldContextObject.Get())
	{
		if (const UWorld* World = ContextObject->GetWorld())
		{
			return World->GetTimeSeconds();
		}
	}

	if (AudioComponent)
	{
		if (const UWorld* World = AudioComponent->GetWorld())
		{
			return World->GetTimeSeconds();
		}
	}

	return 0.0f;
}
