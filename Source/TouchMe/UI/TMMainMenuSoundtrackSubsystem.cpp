// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMMainMenuSoundtrackSubsystem.h"

#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/AudioComponent.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace
{
	const TCHAR* DefaultMainMenuSoundtrackPath = TEXT("/Game/Sound/MainCulto.MainCulto");
	const TCHAR* MainMenuSoundtrackAssetName = TEXT("MainCulto");
	const TCHAR* DefaultLoadoutToggleSoundPath = TEXT("/Game/Battle_Royale_Game/Cues/Collects/Collect_Item_Point_Boost_Simple_Metallic_Beep_Zip_Bass_HIt_2_Deep_Cue.Collect_Item_Point_Boost_Simple_Metallic_Beep_Zip_Bass_HIt_2_Deep_Cue");
	const TCHAR* IntroExitSoundPath = TEXT("/Game/Battle_Royale_Game/Cues/Open_Doors_Chests/Open_Weapon_Set_Trap_1_Glitch_Mechanical_Metal_Industrial_Buzz_Hiss_Unlock_Cue.Open_Weapon_Set_Trap_1_Glitch_Mechanical_Metal_Industrial_Buzz_Hiss_Unlock_Cue");
	const FName IntroExitAnimationName(TEXT("Fade_Bars"));
	constexpr float IntroExitShrinkStartTime = 5.0f;
	constexpr float IntroExitSoundFadeOutTime = 0.3f;
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
	const FLinearColor MainMenuArrowRed(1.0f, 0.0f, 0.0f, 1.0f);
	constexpr float MainMenuReturnArrowFallbackSize = 28.0f;

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

	UWidget* FindWidgetByName(UUserWidget* Widget, const FName WidgetName)
	{
		return Widget && Widget->WidgetTree ? Widget->WidgetTree->FindWidget(WidgetName) : nullptr;
	}

	void SetNamedWidgetVisibility(
		UUserWidget* Widget,
		const FName WidgetName,
		const ESlateVisibility Visibility)
	{
		UWidget* TargetWidget = FindWidgetByName(Widget, WidgetName);
		if (TargetWidget && TargetWidget->GetVisibility() != Visibility)
		{
			TargetWidget->SetVisibility(Visibility);
		}
	}

	bool IsMainMenuMultiplayerBarActive(UUserWidget* Widget)
	{
		if (!Widget || !Widget->GetClass()->GetName().Contains(TEXT("W_MainMenu")))
		{
			return false;
		}

		const UWidgetSwitcher* MainMenuSwitcher = Cast<UWidgetSwitcher>(
			FindWidgetByName(Widget, TEXT("MainMenu_Switcher")));
		const UWidget* ActiveWidget = MainMenuSwitcher ? MainMenuSwitcher->GetActiveWidget() : nullptr;
		return ActiveWidget && ActiveWidget->GetName().Contains(TEXT("MultiplayerBar"), ESearchCase::IgnoreCase);
	}

	float ResolveMainMenuReturnArrowSize(UUserWidget* Widget)
	{
		const UTextBlock* HeaderText = Cast<UTextBlock>(FindWidgetByName(Widget, TEXT("HeaderText")));
		if (!HeaderText)
		{
			return MainMenuReturnArrowFallbackSize;
		}

		return FMath::Clamp(static_cast<float>(HeaderText->GetFont().Size), 22.0f, 34.0f);
	}

	void ApplyMainMenuReturnArrowStyle(UUserWidget* Widget)
	{
		UButton* ReturnButton = Cast<UButton>(FindWidgetByName(Widget, TEXT("B_Return_1")));
		UImage* ReturnImage = Cast<UImage>(FindWidgetByName(Widget, TEXT("I_Return_1")));
		if (!ReturnImage)
		{
			return;
		}

		FSlateBrush ReturnBrush = ReturnImage->GetBrush();
		const float ArrowSize = ResolveMainMenuReturnArrowSize(Widget);
		ReturnBrush.ImageSize = FVector2D(ArrowSize, ArrowSize);
		ReturnBrush.TintColor = FSlateColor(ReturnButton && ReturnButton->IsHovered()
			? FLinearColor::White
			: MainMenuArrowRed);

		ReturnImage->SetBrush(ReturnBrush);
		ReturnImage->SetColorAndOpacity(FLinearColor::White);
		ReturnImage->SetRenderTransform(FWidgetTransform());
		ReturnImage->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	}

	void ApplyMainMenuGoVisualCleanup(UUserWidget* Widget)
	{
		if (!Widget || !Widget->GetClass()->GetName().Contains(TEXT("W_MainMenu")))
		{
			return;
		}

		const bool bMultiplayerBarActive = IsMainMenuMultiplayerBarActive(Widget);
		SetNamedWidgetVisibility(
			Widget,
			TEXT("B_Return_1"),
			ESlateVisibility::Visible);
		ApplyMainMenuReturnArrowStyle(Widget);
		SetNamedWidgetVisibility(
			Widget,
			TEXT("B_Multiplayer_R"),
			bMultiplayerBarActive ? ESlateVisibility::Hidden : ESlateVisibility::Visible);
		SetNamedWidgetVisibility(
			Widget,
			TEXT("B_Loadout_Hide"),
			bMultiplayerBarActive ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		SetNamedWidgetVisibility(
			Widget,
			TEXT("B_Settings_Hide"),
			bMultiplayerBarActive ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
}

UTMMainMenuSoundtrackSubsystem::UTMMainMenuSoundtrackSubsystem()
{
	static ConstructorHelpers::FObjectFinder<USoundBase> IntroExitSoundFinder(IntroExitSoundPath);
	if (IntroExitSoundFinder.Succeeded())
	{
		IntroExitSound = IntroExitSoundFinder.Object;
	}
}

void UTMMainMenuSoundtrackSubsystem::Deinitialize()
{
	StopActiveSoundtrack();
	ActiveWorld.Reset();
	TimeUntilNextScan = 0.0f;
	IntroExitAnimationStartWorldTime = 0.0f;
	IntroExitAnimationLastTime = 0.0f;
	bStartedInCurrentWorld = false;
	bPlayedFirstUserCreatedSound = false;
	bIntroExitAnimationStarted = false;
	bIntroExitAnimationWasPlaying = false;
	bIntroWidgetWasInViewport = false;
	bPlayedIntroExitSound = false;
	bIntroExitSoundFadeRequested = false;
	bMissingIntroExitSoundLogged = false;
	bHasObservedLoadoutVisibility = false;
	bLastLoadoutVisible = false;
	bSoundtrackDuckingActive = false;
	SoundtrackBaseVolumes.Reset();
	IntroExitSoundComponent = nullptr;

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

	TickIntroExitSound(World);

	bool bLoadoutVisible = false;
	for (TObjectIterator<UUserWidget> WidgetIt; WidgetIt; ++WidgetIt)
	{
		UUserWidget* Widget = *WidgetIt;
		if (!IsValid(Widget) || Widget->GetWorld() != World)
		{
			continue;
		}

		ApplyMainMenuGoVisualCleanup(Widget);

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

bool UTMMainMenuSoundtrackSubsystem::IsIntroWidgetInViewport(const UUserWidget* Widget)
{
	if (!Widget || !Widget->GetClass()->GetName().Contains(TEXT("W_Intro")))
	{
		return false;
	}

	return Widget->IsInViewport();
}

bool UTMMainMenuSoundtrackSubsystem::GetIntroExitAnimationPlaybackTime(
	const UUserWidget* Widget,
	float& OutCurrentTime)
{
	if (!Widget || !Widget->GetClass()->GetName().Contains(TEXT("W_Intro")))
	{
		return false;
	}

	for (TFieldIterator<FObjectPropertyBase> PropertyIt(Widget->GetClass()); PropertyIt; ++PropertyIt)
	{
		const FObjectPropertyBase* ObjectProperty = *PropertyIt;
		if (!ObjectProperty
			|| !ObjectProperty->PropertyClass
			|| !ObjectProperty->PropertyClass->IsChildOf(UWidgetAnimation::StaticClass()))
		{
			continue;
		}

		const FString PropertyName = ObjectProperty->GetName();
		if (!PropertyName.Equals(IntroExitAnimationName.ToString(), ESearchCase::IgnoreCase)
			&& !PropertyName.Contains(IntroExitAnimationName.ToString(), ESearchCase::IgnoreCase))
		{
			continue;
		}

		const UWidgetAnimation* Animation = Cast<UWidgetAnimation>(ObjectProperty->GetObjectPropertyValue_InContainer(Widget));
		if (Animation && Widget->IsAnimationPlaying(Animation))
		{
			OutCurrentTime = Widget->GetAnimationCurrentTime(Animation);
			return true;
		}
	}

	return false;
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
	IntroExitAnimationStartWorldTime = 0.0f;
	IntroExitAnimationLastTime = 0.0f;
	bStartedInCurrentWorld = false;
	bPlayedFirstUserCreatedSound = false;
	bIntroExitAnimationStarted = false;
	bIntroExitAnimationWasPlaying = false;
	bIntroWidgetWasInViewport = false;
	bPlayedIntroExitSound = false;
	bIntroExitSoundFadeRequested = false;
	bMissingIntroExitSoundLogged = false;
	bHasObservedLoadoutVisibility = false;
	bLastLoadoutVisible = false;
	bSoundtrackDuckingActive = false;
	IntroExitSoundComponent = nullptr;
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

void UTMMainMenuSoundtrackSubsystem::TickIntroExitSound(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	bool bIntroExitAnimationPlaying = false;
	bool bIntroWidgetInViewport = false;
	bool bMainMenuVisible = false;
	float IntroExitAnimationCurrentTime = 0.0f;
	for (TObjectIterator<UUserWidget> WidgetIt; WidgetIt; ++WidgetIt)
	{
		const UUserWidget* Widget = *WidgetIt;
		if (!IsValid(Widget) || Widget->GetWorld() != World)
		{
			continue;
		}

		bIntroWidgetInViewport = bIntroWidgetInViewport || IsIntroWidgetInViewport(Widget);

		float WidgetAnimationTime = 0.0f;
		if (GetIntroExitAnimationPlaybackTime(Widget, WidgetAnimationTime))
		{
			bIntroExitAnimationPlaying = true;
			IntroExitAnimationCurrentTime = FMath::Max(IntroExitAnimationCurrentTime, WidgetAnimationTime);
		}

		bMainMenuVisible = bMainMenuVisible || IsMainMenuWidgetReady(Widget);
		if (bIntroExitAnimationPlaying && bMainMenuVisible)
		{
			break;
		}
	}

	const bool bIntroWidgetRemovedFromViewport = bIntroWidgetWasInViewport && !bIntroWidgetInViewport;
	bIntroWidgetWasInViewport = bIntroWidgetWasInViewport || bIntroWidgetInViewport;

	if (!bPlayedIntroExitSound && bIntroExitAnimationPlaying)
	{
		if (!bIntroExitAnimationStarted)
		{
			bIntroExitAnimationStarted = true;
			IntroExitAnimationStartWorldTime = World->GetTimeSeconds() - IntroExitAnimationCurrentTime;
		}

		const float EstimatedAnimationTime = FMath::Max(
			IntroExitAnimationCurrentTime,
			World->GetTimeSeconds() - IntroExitAnimationStartWorldTime);

		if (EstimatedAnimationTime >= IntroExitShrinkStartTime
			&& (!bIntroExitAnimationWasPlaying || IntroExitAnimationLastTime < IntroExitShrinkStartTime))
		{
			PlayIntroExitSound(World);
		}

		IntroExitAnimationLastTime = EstimatedAnimationTime;
	}
	else if (!bPlayedIntroExitSound
		&& bIntroExitAnimationStarted
		&& IntroExitAnimationLastTime < IntroExitShrinkStartTime
		&& bMainMenuVisible)
	{
		PlayIntroExitSound(World);
	}

	bIntroExitAnimationWasPlaying = bIntroExitAnimationPlaying;

	if (bIntroWidgetRemovedFromViewport)
	{
		FadeOutIntroExitSound();
	}
}

void UTMMainMenuSoundtrackSubsystem::PlayIntroExitSound(UWorld* World)
{
	if (bPlayedIntroExitSound || !World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	USoundBase* Sound = IntroExitSound ? IntroExitSound.Get() : LoadObject<USoundBase>(nullptr, IntroExitSoundPath);
	if (!Sound)
	{
		if (!bMissingIntroExitSoundLogged)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TMIntroExitSound] Missing intro exit sound: %s"), IntroExitSoundPath);
			bMissingIntroExitSoundLogged = true;
		}
		return;
	}

	bPlayedIntroExitSound = true;
	bIntroExitSoundFadeRequested = false;
	IntroExitSoundComponent = UGameplayStatics::SpawnSound2D(World, Sound);
	UE_LOG(LogTemp, Display, TEXT("[TMIntroExitSound] Played %s on Fade_Bars shrink."), *Sound->GetName());
}

void UTMMainMenuSoundtrackSubsystem::FadeOutIntroExitSound()
{
	if (bIntroExitSoundFadeRequested)
	{
		return;
	}

	bIntroExitSoundFadeRequested = true;
	if (IsValid(IntroExitSoundComponent) && IntroExitSoundComponent->IsPlaying())
	{
		IntroExitSoundComponent->FadeOut(IntroExitSoundFadeOutTime, 0.0f);
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMIntroExitSound] Fading out intro transition over %.2fs."),
			IntroExitSoundFadeOutTime);
	}
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
