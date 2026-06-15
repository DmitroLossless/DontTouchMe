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
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/UnrealType.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TMGameplayStatics)

#if WITH_ACCESSIBILITY
#include "Framework/Application/SlateApplication.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Accessibility/SlateAccessibleMessageHandler.h"
#endif

#define LOCTEXT_NAMESPACE "TMGameplayStatics"

namespace TMGameplayStatics
{
	bool GetEnumLikePropertyValue(const UObject* Object, const FName PropertyName, int64& OutValue, FString& OutDisplayName)
	{
		OutValue = 0;
		OutDisplayName.Reset();

		if (!Object)
		{
			return false;
		}

		const FProperty* Property = Object->GetClass()->FindPropertyByName(PropertyName);
		if (!Property)
		{
			return false;
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
		const UEnum* Enum = nullptr;

		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			OutValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
			Enum = EnumProperty->GetEnum();
		}
		else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			OutValue = ByteProperty->GetPropertyValue(ValuePtr);
			Enum = ByteProperty->Enum;
		}
		else
		{
			return false;
		}

		if (Enum)
		{
			OutDisplayName = Enum->GetDisplayNameTextByValue(OutValue).ToString();
			if (OutDisplayName.IsEmpty())
			{
				OutDisplayName = Enum->GetNameStringByValue(OutValue);
			}
		}

		return true;
	}

	bool SetEnumLikePropertyValue(UObject* Object, const FName PropertyName, const int64 Value)
	{
		if (!Object)
		{
			return false;
		}

		FProperty* Property = Object->GetClass()->FindPropertyByName(PropertyName);
		if (!Property)
		{
			return false;
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, Value);
			return true;
		}

		if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			ByteProperty->SetPropertyValue(ValuePtr, static_cast<uint8>(Value));
			return true;
		}

		return false;
	}

	FString NormalizeOverlayEnumText(FString Text)
	{
		FString RightSide;
		if (Text.Split(TEXT("::"), nullptr, &RightSide, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			Text = RightSide;
		}

		Text = Text.ToUpper();
		Text.ReplaceInline(TEXT(" "), TEXT(""));
		Text.ReplaceInline(TEXT("_"), TEXT(""));
		Text.ReplaceInline(TEXT("-"), TEXT(""));
		return Text;
	}

	void AddOverlayAlias(TArray<FString>& Aliases, const FString& Alias)
	{
		const FString NormalizedAlias = NormalizeOverlayEnumText(Alias);
		if (!NormalizedAlias.IsEmpty())
		{
			Aliases.AddUnique(NormalizedAlias);
		}
	}

	bool HasOverlayAlias(const TArray<FString>& Aliases, const TCHAR* Alias)
	{
		const FString NormalizedAlias = NormalizeOverlayEnumText(Alias);
		return Aliases.Contains(NormalizedAlias);
	}

	TArray<FString> BuildOverlayStateAliases(const int64 PoseValue, const FString& PoseDisplayName)
	{
		TArray<FString> Aliases;
		AddOverlayAlias(Aliases, PoseDisplayName);

		switch (PoseValue)
		{
		case 0:
			AddOverlayAlias(Aliases, TEXT("DEFAULT"));
			break;
		case 1:
			AddOverlayAlias(Aliases, TEXT("RIFLE"));
			break;
		case 2:
			AddOverlayAlias(Aliases, TEXT("PISTOL_1H"));
			break;
		case 3:
			AddOverlayAlias(Aliases, TEXT("PISTOL_2H"));
			break;
		case 4:
			AddOverlayAlias(Aliases, TEXT("BOW"));
			break;
		case 5:
			AddOverlayAlias(Aliases, TEXT("TORCH"));
			break;
		case 6:
			AddOverlayAlias(Aliases, TEXT("BINOCULARS"));
			break;
		case 7:
			AddOverlayAlias(Aliases, TEXT("BOX"));
			break;
		case 8:
			AddOverlayAlias(Aliases, TEXT("BARREL"));
			break;
		case 9:
			AddOverlayAlias(Aliases, TEXT("INJURED"));
			break;
		case 10:
			AddOverlayAlias(Aliases, TEXT("HANDS_TIED"));
			break;
		default:
			break;
		}

		if (HasOverlayAlias(Aliases, TEXT("PISTOL1H")))
		{
			AddOverlayAlias(Aliases, TEXT("PISTOL_ONE_HANDED"));
		}
		if (HasOverlayAlias(Aliases, TEXT("PISTOL2H")))
		{
			AddOverlayAlias(Aliases, TEXT("PISTOL_TWO_HANDED"));
		}
		bool bHasBinocularAlias = false;
		for (const FString& Alias : Aliases)
		{
			if (Alias.Contains(TEXT("BINOCULAR")))
			{
				bHasBinocularAlias = true;
				break;
			}
		}
		if (bHasBinocularAlias)
		{
			AddOverlayAlias(Aliases, TEXT("BINOCULARS"));
		}

		return Aliases;
	}

	bool SetEnumLikePropertyValueByAliases(UObject* Object, const FName PropertyName, const TArray<FString>& Aliases)
	{
		if (!Object || Aliases.IsEmpty())
		{
			return false;
		}

		FProperty* Property = Object->GetClass()->FindPropertyByName(PropertyName);
		if (!Property)
		{
			return false;
		}

		UEnum* Enum = nullptr;
		FNumericProperty* NumericProperty = nullptr;
		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);

		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			Enum = EnumProperty->GetEnum();
			NumericProperty = EnumProperty->GetUnderlyingProperty();
		}
		else if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			Enum = ByteProperty->Enum;
			NumericProperty = ByteProperty;
		}

		if (!Enum || !NumericProperty)
		{
			return false;
		}

		for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
		{
			if (Enum->HasMetaData(TEXT("Hidden"), Index))
			{
				continue;
			}

			const FString Name = NormalizeOverlayEnumText(Enum->GetNameStringByIndex(Index));
			const FString DisplayName = NormalizeOverlayEnumText(Enum->GetDisplayNameTextByIndex(Index).ToString());
			if (Aliases.Contains(Name) || Aliases.Contains(DisplayName))
			{
				NumericProperty->SetIntPropertyValue(ValuePtr, Enum->GetValueByIndex(Index));
				return true;
			}
		}

		return false;
	}

	TSubclassOf<UAnimInstance> LoadMPSOverlayAnimClass(const int64 PoseValue, const FString& PoseDisplayName)
	{
		const FString NormalizedPose = PoseDisplayName.Replace(TEXT(" "), TEXT("")).ToUpper();

		const TCHAR* ClassPath = TEXT("/Game/Test/ABP_OverlayPose_Default.ABP_OverlayPose_Default_C");
		if (PoseValue == 1 || NormalizedPose.Contains(TEXT("RIFLE")))
		{
			ClassPath = TEXT("/Game/Test/ABP_Overlay_Rifle.ABP_Overlay_Rifle_C");
		}
		else if (PoseValue == 2 || NormalizedPose.Contains(TEXT("PISTOL1H")))
		{
			ClassPath = TEXT("/Game/Test/ABP_Overlay_Pistol1H.ABP_Overlay_Pistol1H_C");
		}
		else if (PoseValue == 3 || NormalizedPose.Contains(TEXT("PISTOL2H")))
		{
			ClassPath = TEXT("/Game/Test/ABP_Overlay_Pistol2H.ABP_Overlay_Pistol2H_C");
		}
		else if (PoseValue == 4 || NormalizedPose.Contains(TEXT("BOW")))
		{
			ClassPath = TEXT("/Game/Test/ABP_Overlay_Bow.ABP_Overlay_Bow_C");
		}
		else if (PoseValue == 5 || NormalizedPose.Contains(TEXT("TORCH")))
		{
			ClassPath = TEXT("/Game/Test/ABP_Overlay_Torch.ABP_Overlay_Torch_C");
		}
		else if (PoseValue == 6 || NormalizedPose.Contains(TEXT("BINOCULAR")))
		{
			ClassPath = TEXT("/Game/Test/ABP_Overlay_Binoculars.ABP_Overlay_Binoculars_C");
		}
		else if (PoseValue == 7 || NormalizedPose.Contains(TEXT("BOX")))
		{
			ClassPath = TEXT("/Game/Test/ABP_Overlay_Box.ABP_Overlay_Box_C");
		}
		else if (PoseValue == 8 || NormalizedPose.Contains(TEXT("BARREL")))
		{
			ClassPath = TEXT("/Game/Test/ABP_Overlay_Barrel.ABP_Overlay_Barrel_C");
		}
		else if (PoseValue == 9 || NormalizedPose.Contains(TEXT("INJURED")))
		{
			ClassPath = TEXT("/Game/Test/ABP_Overlay_Injured.ABP_Overlay_Injured_C");
		}
		else if (PoseValue == 10 || NormalizedPose.Contains(TEXT("HANDSTIED")))
		{
			ClassPath = TEXT("/Game/Test/ABP_Overlay_HandsTied.ABP_Overlay_HandsTied_C");
		}

		return LoadClass<UAnimInstance>(nullptr, ClassPath);
	}

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

bool UTMGameplayStatics::ApplyMPSOverlayPose(ACharacter* Character, UObject* ActiveWeapon)
{
	if (!Character)
	{
		return false;
	}

	int64 PoseValue = 0;
	FString PoseDisplayName;
	TMGameplayStatics::GetEnumLikePropertyValue(ActiveWeapon, TEXT("DT_OverlayPose"), PoseValue, PoseDisplayName);
	TMGameplayStatics::SetEnumLikePropertyValue(Character, TEXT("OverlayPose"), PoseValue);
	const TArray<FString> OverlayStateAliases = TMGameplayStatics::BuildOverlayStateAliases(PoseValue, PoseDisplayName);
	TMGameplayStatics::SetEnumLikePropertyValueByAliases(Character, TEXT("OverlayState"), OverlayStateAliases);

	USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (!Mesh)
	{
		return false;
	}

	const TSubclassOf<UAnimInstance> OverlayAnimClass = TMGameplayStatics::LoadMPSOverlayAnimClass(PoseValue, PoseDisplayName);
	if (!OverlayAnimClass)
	{
		return false;
	}

	if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
	{
		TMGameplayStatics::SetEnumLikePropertyValueByAliases(AnimInstance, TEXT("OverlayState"), OverlayStateAliases);
	}

	Mesh->LinkAnimClassLayers(OverlayAnimClass);
	return true;
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

