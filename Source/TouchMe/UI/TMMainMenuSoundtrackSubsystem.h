// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "TMMainMenuSoundtrackSubsystem.generated.h"

class UAudioComponent;
class USoundBase;
class UUserWidget;

UCLASS()
class TOUCHME_API UTMMainMenuSoundtrackSubsystem final : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	UTMMainMenuSoundtrackSubsystem();

	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

private:
	static bool IsIntroWidgetReady(const UUserWidget* Widget);
	static bool IsMainMenuWidgetReady(const UUserWidget* Widget);
	static bool IsLoadoutWidgetReady(const UUserWidget* Widget);
	static bool IsIntroWidgetInViewport(const UUserWidget* Widget);
	static bool GetIntroExitAnimationPlaybackTime(const UUserWidget* Widget, float& OutCurrentTime);
	static USoundBase* ResolveMainMenuSoundtrack(const UUserWidget* Widget);
	static USoundBase* ResolveLoadoutToggleSound();
	static bool IsMainMenuSoundtrackComponent(UAudioComponent* AudioComponent, UWorld* World);

	void ResetForWorld(UWorld* World);
	void StopActiveSoundtrack();
	void PlayFirstUserCreatedSound();
	void TickIntroExitSound(UWorld* World);
	void PlayIntroExitSound(UWorld* World);
	void FadeOutIntroExitSound();
	void PlayLoadoutToggleSound();
	void ApplySoundtrackDucking(bool bActive);
	void RestoreSoundtrackDucking();

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ActiveSoundtrackComponent;

	UPROPERTY()
	TObjectPtr<USoundBase> IntroExitSound;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> IntroExitSoundComponent;

	TMap<TWeakObjectPtr<UAudioComponent>, float> SoundtrackBaseVolumes;

	TWeakObjectPtr<UWorld> ActiveWorld;
	float TimeUntilNextScan = 0.0f;
	float IntroExitAnimationStartWorldTime = 0.0f;
	float IntroExitAnimationLastTime = 0.0f;
	bool bStartedInCurrentWorld = false;
	bool bPlayedFirstUserCreatedSound = false;
	bool bIntroExitAnimationStarted = false;
	bool bIntroExitAnimationWasPlaying = false;
	bool bIntroWidgetWasInViewport = false;
	bool bPlayedIntroExitSound = false;
	bool bIntroExitSoundFadeRequested = false;
	bool bMissingIntroExitSoundLogged = false;
	bool bHasObservedLoadoutVisibility = false;
	bool bLastLoadoutVisible = false;
	bool bSoundtrackDuckingActive = false;
};
