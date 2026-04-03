// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMGameplayStatics.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/OverlapResult.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "EngineLogs.h"
#include "Misc/PackageName.h"
#include "Kismet/GameplayStaticsTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/EngineVersion.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/KismetSystemLibrary.h"
#include "SceneView.h"
#include "Components/PrimitiveComponent.h"
#include "Math/InverseRotationMatrix.h"
#include "UObject/Package.h"
#include "Engine/CollisionProfile.h"
#include "ParticleHelper.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LocalPlayer.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "AudioDevice.h"
#include "SaveGameSystem.h"
#include "DVRStreaming.h"
#include "PlatformFeatures.h"
#include "GameFramework/Character.h"
#include "Sound/DialogueWave.h"
#include "GameFramework/SaveGame.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Components/DecalComponent.h"
#include "Components/ForceFeedbackComponent.h"
#include "Logging/MessageLog.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "PhysicsEngine/BodySetup.h"
#include "Misc/EngineVersion.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Sound/SoundCue.h"
#include "Audio/ActorSoundParameterInterface.h"
#include "Audio/AudioTraceUtil.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/ProjectileMovementComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TMGameplayStatics)

#if WITH_ACCESSIBILITY
#include "Framework/Application/SlateApplication.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Accessibility/SlateAccessibleMessageHandler.h"
#endif

#define LOCTEXT_NAMESPACE "TMGameplayStatics"

namespace TMGameplayStatics
{
	AActor* GetActorOwnerFromWorldContextObject(UObject* WorldContextObject)
	{
		if (AActor* Actor = Cast<AActor>(WorldContextObject))
		{
			return Actor;
		}
		return WorldContextObject->GetTypedOuter<AActor>();
	}
	const AActor* GetActorOwnerFromWorldContextObject(const UObject* WorldContextObject)
	{
		if (const AActor* Actor = Cast<const AActor>(WorldContextObject))
		{
			return Actor;
		}
		return WorldContextObject->GetTypedOuter<AActor>();
	}
}

//////////////////////////////////////////////////////////////////////////
// UAtomGameplayStatics

UTMGameplayStatics::UTMGameplayStatics(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UTMGameplayStatics::PlaySoundAtLocationDistanced(
	const UObject* WorldContextObject,
	USoundBase* Sound,
	FVector Location,
	FRotator Rotation,
	float VolumeMultiplier,
	float PitchMultiplier,
	float StartTime,
	USoundAttenuation* AttenuationSettings,
	USoundConcurrency* ConcurrencySettings,
	const AActor* OwningActor,
	const UInitialActiveSoundParams* InitialParams)
{
	QUICK_SCOPE_CYCLE_COUNTER(UTMGameplayStatics_PlaySoundAtLocationDistanced);

	if (!Sound || !GEngine || !GEngine->UseSound())
	{
		return;
	}

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (!World || !World->bAllowAudioPlayback || World->IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	FVector ListenerLocation = FVector::ZeroVector;

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(WorldContextObject, 0))
	{
		FVector ViewLoc;
		FRotator ViewRot;
		PC->GetPlayerViewPoint(ViewLoc, ViewRot);
		ListenerLocation = ViewLoc;
	}
	else
	{
		ListenerLocation = Location;
	}

	const float DistanceCm = FVector::Distance(ListenerLocation, Location);
	float DelaySeconds = DistanceCm / 34300.f;
	DelaySeconds = FMath::Clamp(DelaySeconds, 0.f, 1.5f);

	auto PlayNow = [=]()
		{
			if (!World) return;

			if (FAudioDeviceHandle AudioDevice = World->GetAudioDevice())
			{
				TArray<FAudioParameter> Params;
				if (InitialParams)
				{
					Params.Append(InitialParams->AudioParams);
				}

				const AActor* ActiveSoundOwner =
					OwningActor ? OwningActor : TMGameplayStatics::GetActorOwnerFromWorldContextObject(WorldContextObject);

				UActorSoundParameterInterface::Fill(ActiveSoundOwner, Params);

				AudioDevice->PlaySoundAtLocation(
					Sound, World,
					VolumeMultiplier, PitchMultiplier, StartTime,
					Location, Rotation,
					AttenuationSettings, ConcurrencySettings,
					MoveTemp(Params),
					ActiveSoundOwner);
			}
		};

	if (DelaySeconds <= KINDA_SMALL_NUMBER)
	{
		PlayNow();
		return;
	}

	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(PlayNow), DelaySeconds, false);
}

UAudioComponent* UTMGameplayStatics::SpawnSoundAtLocationDistanced(const UObject* WorldContextObject, USoundBase* Sound, FVector Location, FRotator Rotation, float VolumeMultiplier, float PitchMultiplier, float StartTime, USoundAttenuation* AttenuationSettings, USoundConcurrency* ConcurrencySettings, bool bAutoDestroy)
{
	QUICK_SCOPE_CYCLE_COUNTER(UAtomGameplayStatics_SpawnSoundAtLocation);

	if (!Sound || !GEngine || !GEngine->UseSound())
	{
		return nullptr;
	}

	UWorld* ThisWorld = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (!ThisWorld || !ThisWorld->bAllowAudioPlayback || ThisWorld->IsNetMode(NM_DedicatedServer))
	{
		return nullptr;
	}

	const bool bIsInGameWorld = ThisWorld->IsGameWorld();

	// Derive an owner from the WorldContextObject
	AActor* WorldContextOwner = TMGameplayStatics::GetActorOwnerFromWorldContextObject(const_cast<UObject*>(WorldContextObject));

	FAudioDevice::FCreateComponentParams Params(ThisWorld, WorldContextOwner);
	Params.SetLocation(Location);
	Params.AttenuationSettings = AttenuationSettings;
	
	if (ConcurrencySettings)
	{
		Params.ConcurrencySet.Add(ConcurrencySettings);
	}

	UAudioComponent* AudioComponent = FAudioDevice::CreateComponent(Sound, Params);

	if (AudioComponent)
	{
		AudioComponent->SetWorldLocationAndRotation(Location, Rotation);
		AudioComponent->SetVolumeMultiplier(VolumeMultiplier);
		AudioComponent->SetPitchMultiplier(PitchMultiplier);
		AudioComponent->bAllowSpatialization	= Params.ShouldUseAttenuation();
		AudioComponent->bIsUISound				= !bIsInGameWorld;
		AudioComponent->bAutoDestroy			= bAutoDestroy;
		AudioComponent->SubtitlePriority		= Sound->GetSubtitlePriority();
		AudioComponent->bStopWhenOwnerDestroyed = false;
		AudioComponent->Play(StartTime);
	}

	return AudioComponent;
}

void UTMGameplayStatics::MarketSoundRoom(bool enable)
{
	
}

AActor* UTMGameplayStatics::Shoot(
	const UObject* WorldContextObject,
	TSubclassOf<AActor> ProjectileClass,
	FVector Start,
	FVector Direction,
	float Distance,
	float ProjectileSpeed,
	TEnumAsByte<ECollisionChannel> TraceChannel,
	AActor* Owner,
	APawn* Instigator,
	USoundBase* ShootSound,
	USoundAttenuation* AttenuationSettings,
	USoundConcurrency* ConcurrencySettings)
{
	if (!WorldContextObject || !ProjectileClass || Distance <= 0.f)
	{
		return nullptr;
	}

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (!World)
	{
		return nullptr;
	}

	const FVector TraceDirection = Direction.GetSafeNormal();
	if (TraceDirection.IsNearlyZero())
	{
		return nullptr;
	}

	const FRotator SpawnRotation = TraceDirection.Rotation();

	if (ShootSound)
	{
		SpawnSoundAtLocationDistanced(
			WorldContextObject,
			ShootSound,
			Start,
			SpawnRotation,
			1.f,
			1.f,
			0.f,
			AttenuationSettings,
			ConcurrencySettings,
			true);
	}

	const FVector End = Start + TraceDirection * Distance;

	FHitResult HitResult;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(UTMGameplayStatics_Shoot), true);
	QueryParams.AddIgnoredActor(Owner);
	QueryParams.AddIgnoredActor(Instigator);

	if (World->LineTraceSingleByChannel(HitResult, Start, End, TraceChannel, QueryParams))
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	SpawnParams.Instigator = Instigator;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* Projectile = World->SpawnActor<AActor>(ProjectileClass, End, SpawnRotation, SpawnParams);
	if (!Projectile)
	{
		return nullptr;
	}

	if (UProjectileMovementComponent* ProjectileMovement = Projectile->FindComponentByClass<UProjectileMovementComponent>())
	{
		if (ProjectileSpeed > 0.f)
		{
			ProjectileMovement->InitialSpeed = ProjectileSpeed;
			ProjectileMovement->MaxSpeed = FMath::Max(ProjectileMovement->MaxSpeed, ProjectileSpeed);
		}

		ProjectileMovement->Velocity = TraceDirection * (ProjectileSpeed > 0.f ? ProjectileSpeed : ProjectileMovement->InitialSpeed);
		ProjectileMovement->Activate(true);
	}

	return Projectile;
}

#undef LOCTEXT_NAMESPACE

