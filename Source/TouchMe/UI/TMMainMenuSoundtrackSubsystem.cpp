// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMMainMenuSoundtrackSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Components/AudioComponent.h"
#include "Components/Widget.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace
{
	const TCHAR* DefaultMainMenuSoundtrackPath = TEXT("/Game/Sound/MainCulto.MainCulto");
	const TCHAR* MainMenuSoundtrackAssetName = TEXT("MainCulto");
	const TCHAR* DefaultLoadoutToggleSoundPath = TEXT("/Game/Battle_Royale_Game/Cues/Collects/Collect_Item_Point_Boost_Simple_Metallic_Beep_Zip_Bass_HIt_2_Deep_Cue.Collect_Item_Point_Boost_Simple_Metallic_Beep_Zip_Bass_HIt_2_Deep_Cue");
	const TCHAR* FirstUserCreatedSoundPaths[] =
	{
		TEXT("/Game/Battle_Royale_Game/Cues/Foley/Foley_Consume_Apple_Use_1_Fruit_Health_Wet_Slime_Goo_Crunch_Cue.Foley_Consume_Apple_Use_1_Fruit_Health_Wet_Slime_Goo_Crunch_Cue"),
		TEXT("/Game/Battle_Royale_Game/Cues/Foley/Foley_Consume_Apple_Use_2_Fruit_Health_Wet_Slime_Goo_Crunch_Cue.Foley_Consume_Apple_Use_2_Fruit_Health_Wet_Slime_Goo_Crunch_Cue"),
		TEXT("/Game/Battle_Royale_Game/Cues/Foley/Foley_Consume_Apple_Use_3_Fruit_Health_Wet_Slime_Goo_Crunch_Cue.Foley_Consume_Apple_Use_3_Fruit_Health_Wet_Slime_Goo_Crunch_Cue"),
		TEXT("/Game/Battle_Royale_Game/Cues/Foley/Foley_Consume_Apple_Use_4_Fruit_Health_Wet_Slime_Goo_Crunch_Cue.Foley_Consume_Apple_Use_4_Fruit_Health_Wet_Slime_Goo_Crunch_Cue"),
		TEXT("/Game/Battle_Royale_Game/Cues/Foley/Foley_Consume_Apple_Use_5_Fruit_Health_Wet_Slime_Goo_Crunch_Cue.Foley_Consume_Apple_Use_5_Fruit_Health_Wet_Slime_Goo_Crunch_Cue")
	};
	constexpr float MainMenuSoundtrackScanInterval = 0.05f;
	constexpr float LoadoutSoundtrackDuckingVolumeScale = 0.28f;
	constexpr float LoadoutSoundtrackLowPassFrequency = 650.0f;
	constexpr float MainMenuSoundtrackRestoredLowPassFrequency = 10000.0f;

	bool IsMainMenuWorld(UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_DedicatedServer)
		{
			return false;
		}

		const FString MapName = World->GetMapName();
		const FString WorldPath = World->GetPathName();
		if (MapName.Contains(TEXT("Map_MainMenu"), ESearchCase::IgnoreCase)
			|| WorldPath.Contains(TEXT("/MP_System_V3/Maps/Map_MainMenu"), ESearchCase::IgnoreCase)
			|| WorldPath.Contains(TEXT("MainMenu"), ESearchCase::IgnoreCase))
		{
			return true;
		}

		const AGameModeBase* GameMode = UGameplayStatics::GetGameMode(World);
		const UClass* GameModeClass = GameMode ? GameMode->GetClass() : nullptr;
		const FString GameModePath = GameModeClass ? GameModeClass->GetPathName() : FString();
		return GameModePath.Contains(TEXT("/MainMenuPawn/GM_Menu."), ESearchCase::IgnoreCase)
			|| GameModePath.Contains(TEXT("GM_Menu_C"), ESearchCase::IgnoreCase);
	}
}

void UTMMainMenuSoundtrackSubsystem::Deinitialize()
{
	StopActiveSoundtrack();
	ActiveWorld.Reset();
	TimeUntilNextScan = 0.0f;
	bStartedInCurrentWorld = false;
	bPlayedFirstUserCreatedSound = false;
	bHasObservedLoadoutVisibility = false;
	bLastLoadoutVisible = false;
	bSoundtrackDuckingActive = false;
	SoundtrackBaseVolumes.Reset();

	Super::Deinitialize();
}

void UTMMainMenuSoundtrackSubsystem::Tick(const float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (ActiveWorld.Get() != World)
	{
		ResetForWorld(World);
	}

	TimeUntilNextScan -= DeltaTime;
	if (TimeUntilNextScan > 0.0f)
	{
		return;
	}

	TimeUntilNextScan = MainMenuSoundtrackScanInterval;

	if (!IsMainMenuWorld(World))
	{
		return;
	}

	bool bLoadoutVisible = false;
	for (TObjectIterator<UUserWidget> WidgetIt; WidgetIt; ++WidgetIt)
	{
		UUserWidget* Widget = *WidgetIt;
		if (!IsValid(Widget) || Widget->GetWorld() != World)
		{
			continue;
		}

		if (!bPlayedFirstUserCreatedSound && IsIntroWidgetReady(Widget))
		{
			PlayFirstUserCreatedSound();
		}

		if (!bStartedInCurrentWorld && IsMainMenuWidgetReady(Widget))
		{
			bStartedInCurrentWorld = true;

			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMMainMenuSoundtrack] W_MainMenu became visible; using BP-owned soundtrack playback."));
		}

		bLoadoutVisible = bLoadoutVisible || IsLoadoutWidgetReady(Widget);
	}

	if (!bHasObservedLoadoutVisibility)
	{
		bHasObservedLoadoutVisibility = true;
		bLastLoadoutVisible = bLoadoutVisible;
		ApplySoundtrackDucking(bLoadoutVisible);
		if (bLoadoutVisible)
		{
			PlayLoadoutToggleSound();
		}
		return;
	}

	if (bLoadoutVisible != bLastLoadoutVisible)
	{
		bLastLoadoutVisible = bLoadoutVisible;
		PlayLoadoutToggleSound();
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMLoadoutToggleSound] Played loadout %s sound."),
			bLoadoutVisible ? TEXT("enter") : TEXT("exit"));
	}

	ApplySoundtrackDucking(bLoadoutVisible);
}

TStatId UTMMainMenuSoundtrackSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTMMainMenuSoundtrackSubsystem, STATGROUP_Tickables);
}

bool UTMMainMenuSoundtrackSubsystem::IsTickable() const
{
	return !HasAnyFlags(RF_ClassDefaultObject);
}

bool UTMMainMenuSoundtrackSubsystem::IsIntroWidgetReady(const UUserWidget* Widget)
{
	if (!Widget || !Widget->GetClass()->GetName().Contains(TEXT("W_Intro")))
	{
		return false;
	}

	if (!Widget->IsVisible())
	{
		return false;
	}

	const ESlateVisibility Visibility = Widget->GetVisibility();
	if (Visibility == ESlateVisibility::Collapsed || Visibility == ESlateVisibility::Hidden)
	{
		return false;
	}

	if (Widget->IsInViewport())
	{
		return true;
	}

	const FVector2D DrawnSize = Widget->GetCachedGeometry().GetLocalSize();
	return DrawnSize.X > 16.0f && DrawnSize.Y > 16.0f;
}

bool UTMMainMenuSoundtrackSubsystem::IsMainMenuWidgetReady(const UUserWidget* Widget)
{
	if (!Widget || !Widget->GetClass()->GetName().Contains(TEXT("W_MainMenu")))
	{
		return false;
	}

	if (!Widget->IsVisible())
	{
		return false;
	}

	const ESlateVisibility Visibility = Widget->GetVisibility();
	if (Visibility == ESlateVisibility::Collapsed || Visibility == ESlateVisibility::Hidden)
	{
		return false;
	}

	if (Widget->IsInViewport())
	{
		return true;
	}

	const FVector2D DrawnSize = Widget->GetCachedGeometry().GetLocalSize();
	return DrawnSize.X > 16.0f && DrawnSize.Y > 16.0f;
}

bool UTMMainMenuSoundtrackSubsystem::IsLoadoutWidgetReady(const UUserWidget* Widget)
{
	if (!Widget)
	{
		return false;
	}

	const FString ClassName = Widget->GetClass()->GetName();
	if (!ClassName.Contains(TEXT("W_Loadout")) && !ClassName.Contains(TEXT("W_Attachments")))
	{
		return false;
	}

	if (!Widget->IsVisible())
	{
		return false;
	}

	const ESlateVisibility Visibility = Widget->GetVisibility();
	if (Visibility == ESlateVisibility::Collapsed || Visibility == ESlateVisibility::Hidden)
	{
		return false;
	}

	if (Widget->IsInViewport())
	{
		return true;
	}

	const FVector2D DrawnSize = Widget->GetCachedGeometry().GetLocalSize();
	return DrawnSize.X > 16.0f && DrawnSize.Y > 16.0f;
}

USoundBase* UTMMainMenuSoundtrackSubsystem::ResolveMainMenuSoundtrack(const UUserWidget* Widget)
{
	static const FName SoundtrackPropertyNames[] =
	{
		TEXT("Soundtrack"),
		TEXT("SoundTrack")
	};

	for (const FName PropertyName : SoundtrackPropertyNames)
	{
		const FObjectPropertyBase* ObjectProperty = Widget
			? FindFProperty<FObjectPropertyBase>(Widget->GetClass(), PropertyName)
			: nullptr;
		if (!ObjectProperty)
		{
			continue;
		}

		if (USoundBase* Soundtrack = Cast<USoundBase>(ObjectProperty->GetObjectPropertyValue_InContainer(Widget)))
		{
			return Soundtrack;
		}
	}

	return LoadObject<USoundBase>(nullptr, DefaultMainMenuSoundtrackPath);
}

USoundBase* UTMMainMenuSoundtrackSubsystem::ResolveLoadoutToggleSound()
{
	return LoadObject<USoundBase>(nullptr, DefaultLoadoutToggleSoundPath);
}

bool UTMMainMenuSoundtrackSubsystem::IsMainMenuSoundtrackComponent(
	UAudioComponent* AudioComponent,
	UWorld* World)
{
	if (!IsValid(AudioComponent) || AudioComponent->GetWorld() != World || !AudioComponent->IsPlaying())
	{
		return false;
	}

	const USoundBase* Sound = AudioComponent->GetSound();
	if (!Sound)
	{
		return false;
	}

	const FString SoundPath = Sound->GetPathName();
	return SoundPath.Contains(MainMenuSoundtrackAssetName, ESearchCase::IgnoreCase);
}

void UTMMainMenuSoundtrackSubsystem::ResetForWorld(UWorld* World)
{
	RestoreSoundtrackDucking();
	StopActiveSoundtrack();
	ActiveWorld = World;
	TimeUntilNextScan = 0.0f;
	bStartedInCurrentWorld = false;
	bPlayedFirstUserCreatedSound = false;
	bHasObservedLoadoutVisibility = false;
	bLastLoadoutVisible = false;
	bSoundtrackDuckingActive = false;
}

void UTMMainMenuSoundtrackSubsystem::StopActiveSoundtrack()
{
	if (IsValid(ActiveSoundtrackComponent))
	{
		ActiveSoundtrackComponent->Stop();
	}

	ActiveSoundtrackComponent = nullptr;
}

void UTMMainMenuSoundtrackSubsystem::PlayFirstUserCreatedSound()
{
	UWorld* World = GetWorld();
	if (bPlayedFirstUserCreatedSound || !World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	constexpr int32 SoundCount = UE_ARRAY_COUNT(FirstUserCreatedSoundPaths);
	const int32 SoundIndex = FMath::RandRange(0, SoundCount - 1);
	USoundBase* FirstUserCreatedSound = LoadObject<USoundBase>(nullptr, FirstUserCreatedSoundPaths[SoundIndex]);
	if (!FirstUserCreatedSound)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[TMMainMenuSoundtrack] Missing first-user-created sound: %s"),
			FirstUserCreatedSoundPaths[SoundIndex]);
		return;
	}

	bPlayedFirstUserCreatedSound = true;
	UGameplayStatics::PlaySound2D(World, FirstUserCreatedSound);
	UE_LOG(
		LogTemp,
		Display,
		TEXT("[TMMainMenuSoundtrack] Played first-user-created sound: %s"),
		*FirstUserCreatedSound->GetName());
}

void UTMMainMenuSoundtrackSubsystem::PlayLoadoutToggleSound()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	USoundBase* LoadoutToggleSound = ResolveLoadoutToggleSound();
	if (!LoadoutToggleSound)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TMLoadoutToggleSound] Missing loadout toggle sound asset."));
		return;
	}

	UGameplayStatics::PlaySound2D(World, LoadoutToggleSound);
}

void UTMMainMenuSoundtrackSubsystem::ApplySoundtrackDucking(const bool bActive)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (!bActive)
	{
		RestoreSoundtrackDucking();
		return;
	}

	int32 AppliedCount = 0;
	for (TObjectIterator<UAudioComponent> ComponentIt; ComponentIt; ++ComponentIt)
	{
		UAudioComponent* AudioComponent = *ComponentIt;
		if (!IsMainMenuSoundtrackComponent(AudioComponent, World))
		{
			continue;
		}

		TWeakObjectPtr<UAudioComponent> ComponentKey(AudioComponent);
		float& BaseVolume = SoundtrackBaseVolumes.FindOrAdd(ComponentKey);
		if (BaseVolume <= UE_KINDA_SMALL_NUMBER)
		{
			BaseVolume = AudioComponent->VolumeMultiplier;
		}

		const float DuckedVolume = FMath::Max(0.0f, BaseVolume * LoadoutSoundtrackDuckingVolumeScale);
		AudioComponent->SetLowPassFilterEnabled(true);
		AudioComponent->SetLowPassFilterFrequency(LoadoutSoundtrackLowPassFrequency);
		AudioComponent->SetVolumeMultiplier(DuckedVolume);
		++AppliedCount;
	}

	if (AppliedCount > 0 && !bSoundtrackDuckingActive)
	{
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMMainMenuSoundtrack] Applied loadout ducking to %d soundtrack component(s). VolumeScale=%.2f LowPass=%.0f"),
			AppliedCount,
			LoadoutSoundtrackDuckingVolumeScale,
			LoadoutSoundtrackLowPassFrequency);
	}

	bSoundtrackDuckingActive = AppliedCount > 0;
}

void UTMMainMenuSoundtrackSubsystem::RestoreSoundtrackDucking()
{
	if (SoundtrackBaseVolumes.IsEmpty())
	{
		bSoundtrackDuckingActive = false;
		return;
	}

	int32 RestoredCount = 0;
	for (auto It = SoundtrackBaseVolumes.CreateIterator(); It; ++It)
	{
		UAudioComponent* AudioComponent = It.Key().Get();
		if (!IsValid(AudioComponent))
		{
			It.RemoveCurrent();
			continue;
		}

		AudioComponent->SetLowPassFilterFrequency(MainMenuSoundtrackRestoredLowPassFrequency);
		AudioComponent->SetLowPassFilterEnabled(false);
		AudioComponent->SetVolumeMultiplier(It.Value());
		++RestoredCount;
		It.RemoveCurrent();
	}

	if (RestoredCount > 0 && bSoundtrackDuckingActive)
	{
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMMainMenuSoundtrack] Restored loadout ducking on %d soundtrack component(s)."),
			RestoredCount);
	}

	bSoundtrackDuckingActive = false;
}
