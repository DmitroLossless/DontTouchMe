// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMAudioEnvelopeFollower.h"

#include "Components/AudioComponent.h"
#include "HAL/IConsoleManager.h"
#include "Sound/SoundWave.h"

#if __has_include("LoudnessNRT.h")
#include "LoudnessNRT.h"
#elif __has_include("AudioSynesthesia/Public/LoudnessNRT.h")
#include "AudioSynesthesia/Public/LoudnessNRT.h"
#else
#error Unable to locate LoudnessNRT.h. Verify the AudioSynesthesia plugin is enabled.
#endif

DEFINE_LOG_CATEGORY_STATIC(LogTMAudioEnvelopeFollower, Log, All);

static TMap<TWeakObjectPtr<UAudioComponent>, TWeakObjectPtr<UTMAudioEnvelopeFollower>> GMainMenuSyntheticBeatFollowers;

static TAutoConsoleVariable<int32> CVarMainMenuBeatAnimationGate(
	TEXT("tm.MainMenu.BeatAnimationGate"),
	1,
	TEXT("When enabled, W_MainMenu beat animation OnBeat callbacks observe lockout and optional phrase gating."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMainMenuBeatAnimationLockout(
	TEXT("tm.MainMenu.BeatAnimationLockout"),
	10.0f,
	TEXT("Rest seconds after the W_MainMenu beat animation pattern before it may repeat."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarMainMenuBeatAnimationSyntheticPattern(
	TEXT("tm.MainMenu.BeatAnimationSyntheticPattern"),
	1,
	TEXT("When enabled, W_MainMenu OnBeat callbacks are driven by an animation-only timer instead of detected audio peaks."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarMainMenuBeatAnimationPatternHits(
	TEXT("tm.MainMenu.BeatAnimationPatternHits"),
	3,
	TEXT("Number of eighth-note hits in the W_MainMenu beat animation pattern; used to calculate the repeat interval."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMainMenuBeatAnimationEighthNoteDelay(
	TEXT("tm.MainMenu.BeatAnimationEighthNoteDelay"),
	0.25f,
	TEXT("Seconds between W_MainMenu beat animation hits inside the synthetic pattern."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMainMenuBeatAnimationInitialDelay(
	TEXT("tm.MainMenu.BeatAnimationInitialDelay"),
	0.0f,
	TEXT("Initial delay before the first synthetic W_MainMenu beat animation callback."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarMainMenuBeatAnimationPhraseBars(
	TEXT("tm.MainMenu.BeatAnimationPhraseBars"),
	4,
	TEXT("Bars per phrase for W_MainMenu beat animation callbacks."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarMainMenuBeatAnimationPhraseStart(
	TEXT("tm.MainMenu.BeatAnimationPhraseStart"),
	0,
	TEXT("When enabled, W_MainMenu beat animation callbacks fire only on the first beat of a phrase after lockout."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarMainMenuBeatAnimationPhraseEnd(
	TEXT("tm.MainMenu.BeatAnimationPhraseEnd"),
	0,
	TEXT("When enabled, W_MainMenu beat animation callbacks fire only on the last beat of a phrase after lockout."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarMainMenuBeatAnimationPair(
	TEXT("tm.MainMenu.BeatAnimationPair"),
	0,
	TEXT("When enabled, each W_MainMenu beat animation callback emits a second callback after an eighth-note delay."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMainMenuBeatAnimationPairFallbackDelay(
	TEXT("tm.MainMenu.BeatAnimationPairFallbackDelay"),
	0.25f,
	TEXT("Fallback seconds between paired W_MainMenu beat animation callbacks before tempo is estimated."),
	ECVF_Default);

static bool IsMainMenuBeatAnimationGateEnabled()
{
	return CVarMainMenuBeatAnimationGate.GetValueOnGameThread() != 0;
}

static bool ShouldUseMainMenuBeatAnimationPhraseEnd()
{
	return CVarMainMenuBeatAnimationPhraseEnd.GetValueOnGameThread() != 0;
}

static bool ShouldUseMainMenuBeatAnimationPhraseStart()
{
	return CVarMainMenuBeatAnimationPhraseStart.GetValueOnGameThread() != 0;
}

static bool ShouldEmitMainMenuBeatAnimationPair()
{
	return CVarMainMenuBeatAnimationPair.GetValueOnGameThread() != 0;
}

static bool ShouldUseSyntheticMainMenuBeatAnimationPattern()
{
	return CVarMainMenuBeatAnimationSyntheticPattern.GetValueOnGameThread() != 0;
}

static bool ShouldUseSyntheticMainMenuBeatAnimationPatternForAudio(UAudioComponent* InAudioComponent)
{
	if (!ShouldUseSyntheticMainMenuBeatAnimationPattern() || !InAudioComponent)
	{
		return false;
	}

	const USoundBase* Sound = InAudioComponent->GetSound();
	return Sound && Sound->GetPathName().Contains(TEXT("MainCulto"), ESearchCase::IgnoreCase);
}

static float GetMainMenuBeatAnimationLockout()
{
	return FMath::Max(0.0f, CVarMainMenuBeatAnimationLockout.GetValueOnGameThread());
}

static int32 GetMainMenuBeatAnimationPatternHits()
{
	return FMath::Max(1, CVarMainMenuBeatAnimationPatternHits.GetValueOnGameThread());
}

static float GetMainMenuBeatAnimationEighthNoteDelay()
{
	return FMath::Max(0.01f, CVarMainMenuBeatAnimationEighthNoteDelay.GetValueOnGameThread());
}

static float GetMainMenuBeatAnimationInitialDelay()
{
	return FMath::Max(0.0f, CVarMainMenuBeatAnimationInitialDelay.GetValueOnGameThread());
}

static float GetSyntheticMainMenuBeatAnimationInterval()
{
	const int32 PatternHits = GetMainMenuBeatAnimationPatternHits();
	return GetMainMenuBeatAnimationLockout() + (PatternHits - 1) * GetMainMenuBeatAnimationEighthNoteDelay();
}

static float GetMainMenuBeatAnimationPairFallbackDelay()
{
	return FMath::Max(0.01f, CVarMainMenuBeatAnimationPairFallbackDelay.GetValueOnGameThread());
}

static int32 GetMainMenuBeatAnimationPhraseBars()
{
	return FMath::Max(1, CVarMainMenuBeatAnimationPhraseBars.GetValueOnGameThread());
}

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

	if (ShouldUseSyntheticMainMenuBeatAnimationPatternForAudio(InAudioComponent)
		&& bIsAnalyzing
		&& AudioComponent == InAudioComponent
		&& LoudnessAnalyzer == InLoudnessAnalyzer)
	{
		EnsureSyntheticBeatAnimationTimer();
		return true;
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

	EnsureSyntheticBeatAnimationTimer();

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

int32 UTMAudioEnvelopeFollower::GetBeatCounter() const
{
	return BeatCounter;
}

float UTMAudioEnvelopeFollower::GetLastBeatTimeSeconds() const
{
	return LastBeatTimeSeconds;
}

float UTMAudioEnvelopeFollower::GetLastBeatStrength() const
{
	return LastBeatStrength;
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

	if (bHasPlaybackPercentTime && PlayingSoundWave->Duration > 0.0f)
	{
		CurrentPlaybackTimeSeconds = FMath::Clamp(PlaybackPercentTimeSeconds, 0.0f, PlayingSoundWave->Duration);
	}
	else
	{
		CurrentPlaybackTimeSeconds = FMath::Max(GetCurrentTimeSeconds() - AnalysisStartTimeSeconds, 0.0f);

		if (PlayingSoundWave->Duration > 0.0f)
		{
			CurrentPlaybackTimeSeconds = FMath::Min(CurrentPlaybackTimeSeconds, PlayingSoundWave->Duration);
		}
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
	LastBeatStrength = BeatStrength;
	if (!ShouldUseSyntheticMainMenuBeatAnimationPatternForAudio(AudioComponent) && ShouldBroadcastOnBeat(BeatCounter, CurrentTimeSeconds))
	{
		const bool bHadBeatListeners = OnBeat.IsBound();
		OnBeat.Broadcast(BeatCounter, BeatStrength, CurrentTimeSeconds);
		if (bHadBeatListeners)
		{
			MarkOnBeatBroadcast(CurrentTimeSeconds);
		}
	}

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
	LastBeatStrength = 0.0f;
	NextOnBeatBroadcastTimeSeconds = -1.0f;
	PlaybackPercentTimeSeconds = 0.0f;
	bHasPlaybackPercentTime = false;
	PendingPairedOnBeatIndex = 0;
	PendingPairedOnBeatStrength = 0.0f;
	SyntheticBeatAnimationCounter = 0;
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
	AudioPlaybackPercentHandle = AudioComponent->OnAudioPlaybackPercentNative.AddUObject(
		this,
		&UTMAudioEnvelopeFollower::HandleAudioPlaybackPercent);
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

	if (AudioPlaybackPercentHandle.IsValid())
	{
		AudioComponent->OnAudioPlaybackPercentNative.Remove(AudioPlaybackPercentHandle);
		AudioPlaybackPercentHandle.Reset();
	}
	if (UWorld* World = AudioComponent->GetWorld())
	{
		World->GetTimerManager().ClearTimer(PairedOnBeatTimerHandle);
		World->GetTimerManager().ClearTimer(SyntheticBeatAnimationTimerHandle);
	}

	const TWeakObjectPtr<UAudioComponent> AudioComponentKey(AudioComponent);
	if (const TWeakObjectPtr<UTMAudioEnvelopeFollower>* RegisteredFollower = GMainMenuSyntheticBeatFollowers.Find(AudioComponentKey))
	{
		if (RegisteredFollower->Get() == this)
		{
			GMainMenuSyntheticBeatFollowers.Remove(AudioComponentKey);
		}
	}

	AudioComponent->OnAudioFinished.RemoveDynamic(this, &UTMAudioEnvelopeFollower::HandleAudioFinished);
	AudioComponent = nullptr;
}

void UTMAudioEnvelopeFollower::EnsureSyntheticBeatAnimationTimer()
{
	if (!ShouldUseSyntheticMainMenuBeatAnimationPatternForAudio(AudioComponent))
	{
		return;
	}

	UWorld* World = AudioComponent->GetWorld();
	if (!World || World->GetTimerManager().IsTimerActive(SyntheticBeatAnimationTimerHandle))
	{
		return;
	}

	const TWeakObjectPtr<UAudioComponent> AudioComponentKey(AudioComponent);
	if (const TWeakObjectPtr<UTMAudioEnvelopeFollower>* RegisteredFollower = GMainMenuSyntheticBeatFollowers.Find(AudioComponentKey))
	{
		if (UTMAudioEnvelopeFollower* ExistingFollower = RegisteredFollower->Get())
		{
			if (ExistingFollower != this)
			{
				ExistingFollower->StopAnalyzing(false);
			}
		}
	}
	GMainMenuSyntheticBeatFollowers.FindOrAdd(AudioComponentKey) = this;

	World->GetTimerManager().SetTimer(
		SyntheticBeatAnimationTimerHandle,
		this,
		&UTMAudioEnvelopeFollower::BroadcastSyntheticBeatAnimation,
		GetSyntheticMainMenuBeatAnimationInterval(),
		true,
		GetMainMenuBeatAnimationInitialDelay());
}

void UTMAudioEnvelopeFollower::BroadcastSyntheticBeatAnimation()
{
	if (!bIsAnalyzing || !OnBeat.IsBound())
	{
		return;
	}

	++SyntheticBeatAnimationCounter;
	if (BeatCounter < SyntheticBeatAnimationCounter)
	{
		BeatCounter = SyntheticBeatAnimationCounter;
	}

	const float CurrentTimeSeconds = GetCurrentTimeSeconds();
	constexpr float SyntheticBeatStrength = 1.0f;
	LastBeatTimeSeconds = CurrentTimeSeconds;
	LastBeatStrength = SyntheticBeatStrength;
	OnBeat.Broadcast(SyntheticBeatAnimationCounter, SyntheticBeatStrength, CurrentTimeSeconds);

	UE_LOG(
		LogTMAudioEnvelopeFollower,
		Display,
		TEXT("Released synthetic menu beat animation pattern: Pattern=%d Hits=%d Eighth=%.3fs Rest=%.2fs NextInterval=%.2fs"),
		SyntheticBeatAnimationCounter,
		GetMainMenuBeatAnimationPatternHits(),
		GetMainMenuBeatAnimationEighthNoteDelay(),
		GetMainMenuBeatAnimationLockout(),
		GetSyntheticMainMenuBeatAnimationInterval());
}

void UTMAudioEnvelopeFollower::HandleAudioPlaybackPercent(
	const UAudioComponent* InAudioComponent,
	const USoundWave* PlayingSoundWave,
	const float PlaybackPercent)
{
	if (InAudioComponent != AudioComponent || !PlayingSoundWave || PlayingSoundWave->Duration <= 0.0f)
	{
		return;
	}

	PlaybackPercentTimeSeconds = FMath::Clamp(PlaybackPercent, 0.0f, 1.0f) * PlayingSoundWave->Duration;
	bHasPlaybackPercentTime = true;
}

void UTMAudioEnvelopeFollower::SchedulePairedOnBeat(const int32 InBeatIndex, const float Strength)
{
	if (!ShouldEmitMainMenuBeatAnimationPair() || !AudioComponent)
	{
		return;
	}

	UWorld* World = AudioComponent->GetWorld();
	if (!World)
	{
		return;
	}

	const float EighthNoteDelay = EstimatedBeatInterval > 0.0f
		? FMath::Clamp(EstimatedBeatInterval * 0.5f, 0.01f, MaxBeatInterval)
		: GetMainMenuBeatAnimationPairFallbackDelay();

	PendingPairedOnBeatIndex = InBeatIndex;
	PendingPairedOnBeatStrength = Strength;
	World->GetTimerManager().SetTimer(
		PairedOnBeatTimerHandle,
		this,
		&UTMAudioEnvelopeFollower::BroadcastPairedOnBeat,
		EighthNoteDelay,
		false);

	UE_LOG(
		LogTMAudioEnvelopeFollower,
		Display,
		TEXT("Scheduled paired menu beat animation callback: Beat=%d Delay=%.3fs Interval=%.3fs"),
		InBeatIndex,
		EighthNoteDelay,
		EstimatedBeatInterval);
}

void UTMAudioEnvelopeFollower::BroadcastPairedOnBeat()
{
	if (!bIsAnalyzing || !OnBeat.IsBound() || PendingPairedOnBeatIndex <= 0)
	{
		return;
	}

	const float CurrentTimeSeconds = GetCurrentTimeSeconds();
	OnBeat.Broadcast(PendingPairedOnBeatIndex, PendingPairedOnBeatStrength, CurrentTimeSeconds);

	UE_LOG(
		LogTMAudioEnvelopeFollower,
		Display,
		TEXT("Released paired menu beat animation callback: Beat=%d Strength=%.2f Time=%.2f"),
		PendingPairedOnBeatIndex,
		PendingPairedOnBeatStrength,
		CurrentTimeSeconds);
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

bool UTMAudioEnvelopeFollower::ShouldBroadcastOnBeat(const int32 InBeatIndex, const float CurrentTimeSeconds) const
{
	if (!IsMainMenuBeatAnimationGateEnabled())
	{
		return true;
	}

	if (NextOnBeatBroadcastTimeSeconds >= 0.0f && CurrentTimeSeconds + KINDA_SMALL_NUMBER < NextOnBeatBroadcastTimeSeconds)
	{
		return false;
	}

	const int32 BeatsPerPhrase = FMath::Max(1, BeatsPerBar) * GetMainMenuBeatAnimationPhraseBars();
	if (ShouldUseMainMenuBeatAnimationPhraseStart())
	{
		if (InBeatIndex <= 0 || BeatsPerPhrase <= 0 || ((InBeatIndex - 1) % BeatsPerPhrase) != 0)
		{
			return false;
		}
	}
	else if (ShouldUseMainMenuBeatAnimationPhraseEnd())
	{
		if (InBeatIndex <= 0 || BeatsPerPhrase <= 0 || (InBeatIndex % BeatsPerPhrase) != 0)
		{
			return false;
		}
	}

	return true;
}

void UTMAudioEnvelopeFollower::MarkOnBeatBroadcast(const float CurrentTimeSeconds)
{
	if (!IsMainMenuBeatAnimationGateEnabled())
	{
		return;
	}

	const float LockoutSeconds = GetMainMenuBeatAnimationLockout();
	NextOnBeatBroadcastTimeSeconds = CurrentTimeSeconds + LockoutSeconds;
	const int32 BeatsPerPhrase = FMath::Max(1, BeatsPerBar) * GetMainMenuBeatAnimationPhraseBars();
	const int32 BeatInPhrase = BeatsPerPhrase > 0 ? ((BeatCounter - 1) % BeatsPerPhrase) + 1 : BeatCounter;
	const int32 PhraseIndex = BeatsPerPhrase > 0 ? ((BeatCounter - 1) / BeatsPerPhrase) + 1 : 0;

	UE_LOG(
		LogTMAudioEnvelopeFollower,
		Display,
		TEXT("Released menu beat animation callback: Beat=%d Phrase=%d BeatInPhrase=%d PhraseBars=%d Lockout=%.2fs NextTime=%.2f Playback=%.2f"),
		BeatCounter,
		PhraseIndex,
		BeatInPhrase,
		GetMainMenuBeatAnimationPhraseBars(),
		LockoutSeconds,
		NextOnBeatBroadcastTimeSeconds,
		CurrentPlaybackTimeSeconds);
}
