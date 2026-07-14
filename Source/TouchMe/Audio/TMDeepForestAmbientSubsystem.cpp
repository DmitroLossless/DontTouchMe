// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMDeepForestAmbientSubsystem.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const TCHAR* DeepForestAmbientSoundPath = TEXT("/Game/Audio/Ambient/SFX_AmbientRainforest_Cue.SFX_AmbientRainforest_Cue");
	constexpr float DeepForestAmbientScanInterval = 0.25f;
	constexpr float DeepForestAmbientVolume = 0.42f;
	constexpr float DeepForestAmbientFadeInSeconds = 3.0f;
	constexpr float DeepForestAmbientFadeOutSeconds = 1.25f;
}

UTMDeepForestAmbientSubsystem::UTMDeepForestAmbientSubsystem()
{
	static ConstructorHelpers::FObjectFinder<USoundBase> AmbientSoundFinder(DeepForestAmbientSoundPath);
	if (AmbientSoundFinder.Succeeded())
	{
		DeepForestAmbientSound = AmbientSoundFinder.Object;
	}
}

void UTMDeepForestAmbientSubsystem::Deinitialize()
{
	StopAmbient(true);
	ActiveWorld.Reset();
	TimeUntilNextScan = 0.0f;
	bMissingSoundLogged = false;

	Super::Deinitialize();
}

void UTMDeepForestAmbientSubsystem::Tick(const float DeltaTime)
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

	TimeUntilNextScan = DeepForestAmbientScanInterval;

	if (!IsDeepForestWorld(World))
	{
		StopAmbient(false);
		return;
	}

	if (!IsValid(AmbientComponent) || !AmbientComponent->IsPlaying())
	{
		StartAmbient(World);
	}
}

TStatId UTMDeepForestAmbientSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTMDeepForestAmbientSubsystem, STATGROUP_Tickables);
}

bool UTMDeepForestAmbientSubsystem::IsTickable() const
{
	return !HasAnyFlags(RF_ClassDefaultObject);
}

bool UTMDeepForestAmbientSubsystem::IsDeepForestWorld(const UWorld* World)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	const EWorldType::Type WorldType = World->WorldType;
	if (WorldType != EWorldType::Game && WorldType != EWorldType::PIE && WorldType != EWorldType::GamePreview)
	{
		return false;
	}

	const FString MapName = World->GetMapName();
	const FString WorldPath = World->GetPathName();
	return MapName.Contains(TEXT("DeepForest"), ESearchCase::IgnoreCase)
		|| WorldPath.Contains(TEXT("/MWConiferForest/Maps/DeepForest"), ESearchCase::IgnoreCase);
}

USoundBase* UTMDeepForestAmbientSubsystem::ResolveDeepForestAmbientSound() const
{
	if (DeepForestAmbientSound)
	{
		return DeepForestAmbientSound;
	}

	return LoadObject<USoundBase>(nullptr, DeepForestAmbientSoundPath);
}

void UTMDeepForestAmbientSubsystem::ResetForWorld(UWorld* World)
{
	StopAmbient(true);
	ActiveWorld = World;
	TimeUntilNextScan = 0.0f;
}

void UTMDeepForestAmbientSubsystem::StartAmbient(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	USoundBase* AmbientSound = ResolveDeepForestAmbientSound();
	if (!AmbientSound)
	{
		if (!bMissingSoundLogged)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TMDeepForestAmbient] Missing ambient sound: %s"), DeepForestAmbientSoundPath);
			bMissingSoundLogged = true;
		}
		return;
	}

	StopAmbient(true);

	AmbientComponent = UGameplayStatics::CreateSound2D(
		World,
		AmbientSound,
		DeepForestAmbientVolume,
		1.0f,
		0.0f,
		nullptr,
		false,
		true);

	if (!AmbientComponent)
	{
		return;
	}

	AmbientComponent->bIsUISound = false;
	AmbientComponent->bStopWhenOwnerDestroyed = false;
	AmbientComponent->FadeIn(DeepForestAmbientFadeInSeconds, 1.0f);

	UE_LOG(
		LogTemp,
		Display,
		TEXT("[TMDeepForestAmbient] Started %s on %s."),
		*AmbientSound->GetName(),
		*World->GetMapName());
}

void UTMDeepForestAmbientSubsystem::StopAmbient(const bool bImmediate)
{
	if (!IsValid(AmbientComponent))
	{
		AmbientComponent = nullptr;
		return;
	}

	if (bImmediate)
	{
		AmbientComponent->Stop();
	}
	else if (AmbientComponent->IsPlaying())
	{
		AmbientComponent->FadeOut(DeepForestAmbientFadeOutSeconds, 0.0f);
	}

	AmbientComponent = nullptr;
}
