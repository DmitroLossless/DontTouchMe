// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMAudioEnvelopeFollower.h"

#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"

#if __has_include("LoudnessNRT.h")
#include "LoudnessNRT.h"
#elif __has_include("AudioSynesthesia/Public/LoudnessNRT.h")
#include "AudioSynesthesia/Public/LoudnessNRT.h"
#else
#error Unable to locate LoudnessNRT.h. Verify the AudioSynesthesia plugin is enabled.
#endif

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

bool UTMAudioEnvelopeFollower::PlayAndAnalyzeSound2D(UAudioComponent* InAudioComponent, ULoudnessNRT* InLoudnessAnalyzer)
{
	return AnalyzeAudioComponent(InAudioComponent, InLoudnessAnalyzer);
}

bool UTMAudioEnvelopeFollower::AnalyzeAudioComponent(UAudioComponent* InAudioComponent, ULoudnessNRT* InLoudnessAnalyzer)
{
	if (!InAudioComponent || !InLoudnessAnalyzer)
	{
		return false;
	}

	WorldContextObject = InAudioComponent;
	LoudnessAnalyzer = InLoudnessAnalyzer;

	UnregisterAudioComponent();
	RegisterAudioComponent(InAudioComponent);
	ResetAnalysisState();
	AnalysisStartTimeSeconds = GetCurrentTimeSeconds();
	bIsAnalyzing = true;

	if (UWorld* World = InAudioComponent->GetWorld())
	{
		World->GetTimerManager().SetTimer(
			AnalysisTimerHandle,
			this,
			&UTMAudioEnvelopeFollower::PollAudioAnalysis,
			AnalysisIntervalSeconds,
			true);
	}

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

void UTMAudioEnvelopeFollower::PollAudioAnalysis()
{
	if (!bIsAnalyzing || !AudioComponent || !LoudnessAnalyzer)
	{
		return;
	}

	const USoundWave* PlayingSoundWave = Cast<USoundWave>(AudioComponent->GetSound());
	if (!PlayingSoundWave)
	{
		return;
	}

	if (!AudioComponent->IsPlaying())
	{
		HandleAudioFinished();
		return;
	}

	CurrentPlaybackTimeSeconds = FMath::Max(GetCurrentTimeSeconds() - AnalysisStartTimeSeconds, 0.0f);

	if (PlayingSoundWave->Duration > 0.0f)
	{
		CurrentPlaybackTimeSeconds = FMath::Min(CurrentPlaybackTimeSeconds, PlayingSoundWave->Duration);
	}

	float LoudnessValue = 0.0f;
	LoudnessAnalyzer->GetNormalizedLoudnessAtTime(CurrentPlaybackTimeSeconds, LoudnessValue);
	LoudnessValue = FMath::Max(LoudnessValue, 0.0f);
	const float RiseOrFallAlpha = LoudnessValue >= SmoothedEnvelopeValue ? EnvelopeRiseAlpha : EnvelopeFallAlpha;

	LastEnvelopeValue = LoudnessValue;
	SmoothedEnvelopeValue = FMath::Lerp(SmoothedEnvelopeValue, LoudnessValue, RiseOrFallAlpha);

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
	if (AudioComponent)
	{
		if (UWorld* World = AudioComponent->GetWorld())
		{
			World->GetTimerManager().ClearTimer(AnalysisTimerHandle);
		}
	}

	bIsAnalyzing = false;
	UnregisterAudioComponent();
}

void UTMAudioEnvelopeFollower::ResetAnalysisState()
{
	CurrentPlaybackTimeSeconds = 0.0f;
	AnalysisStartTimeSeconds = 0.0f;
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

	AudioComponent->OnAudioFinished.AddDynamic(this, &UTMAudioEnvelopeFollower::HandleAudioFinished);
}

void UTMAudioEnvelopeFollower::UnregisterAudioComponent()
{
	if (!AudioComponent)
	{
		return;
	}

	if (UWorld* World = AudioComponent->GetWorld())
	{
		World->GetTimerManager().ClearTimer(AnalysisTimerHandle);
	}

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
