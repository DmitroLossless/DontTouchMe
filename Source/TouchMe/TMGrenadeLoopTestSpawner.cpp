#include "TMGrenadeLoopTestSpawner.h"

#include "Components/ActorComponent.h"
#include "Components/AudioComponent.h"
#include "Components/DecalComponent.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NiagaraComponent.h"
#include "Particles/ParticleSystemComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogTMGrenadeLoopTestSpawner, Log, All);

namespace
{
template <SIZE_T TokenCount>
bool TextContainsAnyToken(const FString& Text, const TCHAR* const (&Tokens)[TokenCount])
{
	for (const TCHAR* Token : Tokens)
	{
		if (Text.Contains(Token, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

FString ActorIdentityText(const AActor* Actor)
{
	if (!Actor)
	{
		return FString();
	}

	const UClass* ActorClass = Actor->GetClass();
	return FString::Printf(
		TEXT("%s %s %s"),
		*Actor->GetName(),
		*Actor->GetPathName(),
		ActorClass ? *ActorClass->GetPathName() : TEXT(""));
}

FString ComponentIdentityText(const UActorComponent* Component)
{
	if (!Component)
	{
		return FString();
	}

	const UClass* ComponentClass = Component->GetClass();
	const AActor* Owner = Component->GetOwner();
	return FString::Printf(
		TEXT("%s %s %s %s"),
		*Component->GetName(),
		*Component->GetPathName(),
		ComponentClass ? *ComponentClass->GetPathName() : TEXT(""),
		Owner ? *ActorIdentityText(Owner) : TEXT(""));
}
}

ATMGrenadeLoopTestSpawner::ATMGrenadeLoopTestSpawner()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bIsEditorOnlyActor = false;

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	static ConstructorHelpers::FClassFinder<AActor> GrenadeClassFinder(TEXT("/Game/BallisticsVFX/FXSpawnerBlueprints/Projectiles/Grenade_Thrown"));
	if (GrenadeClassFinder.Succeeded())
	{
		GrenadeClass = GrenadeClassFinder.Class;
	}
}

void ATMGrenadeLoopTestSpawner::BeginPlay()
{
	Super::BeginPlay();

	ClearCycleTracking();
	const UWorld* World = GetWorld();
	NextSpawnTimeSeconds = World ? World->GetTimeSeconds() + FMath::Max(0.f, InitialDelay) : 0.f;
}

void ATMGrenadeLoopTestSpawner::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UWorld* World = GetWorld();
	if (!bEnabled || !World || !IsRuntimeWorld())
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	if (!bCycleActive)
	{
		if (Now >= NextSpawnTimeSeconds)
		{
			StartNextGrenade();
		}
		return;
	}

	CollectBornObjects();
	if (AreTrackedObjectsRemoved())
	{
		if (AllRemovedSinceSeconds < 0.f)
		{
			AllRemovedSinceSeconds = Now;
		}

		if (Now - AllRemovedSinceSeconds >= FMath::Max(0.f, CleanupStableSeconds))
		{
			ClearCycleTracking();
			NextSpawnTimeSeconds = Now + FMath::Max(0.f, RespawnDelay);
		}
	}
	else
	{
		AllRemovedSinceSeconds = -1.f;
	}

	if (bDrawDebug)
	{
		DrawDebugSphere(World, GetActorLocation() + SpawnOffset, DebugMarkerRadius, 12, FColor::Orange, false, 0.f, 0, 1.5f);
	}
}

void ATMGrenadeLoopTestSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bDestroyTrackedObjectsOnEndPlay)
	{
		for (const TWeakObjectPtr<AActor>& TrackedActor : TrackedActors)
		{
			if (AActor* Actor = TrackedActor.Get())
			{
				if (Actor != this)
				{
					Actor->Destroy();
				}
			}
		}
	}

	ClearCycleTracking();
	Super::EndPlay(EndPlayReason);
}

bool ATMGrenadeLoopTestSpawner::ShouldTickIfViewportsOnly() const
{
	return false;
}

void ATMGrenadeLoopTestSpawner::StartNextGrenade()
{
	UWorld* World = GetWorld();
	if (!World || !GrenadeClass)
	{
		NextSpawnTimeSeconds = World ? World->GetTimeSeconds() + 1.f : 0.f;
		UE_LOG(LogTMGrenadeLoopTestSpawner, Warning, TEXT("Grenade loop spawner has no grenade class."));
		return;
	}

	SnapshotExistingObjects();

	const FVector SpawnLocation = GetActorLocation() + SpawnOffset;
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = MakeUniqueObjectName(World, GrenadeClass.Get(), TEXT("TM_LoopedGrenade"));
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* Grenade = World->SpawnActor<AActor>(GrenadeClass.Get(), SpawnLocation, SpawnRotation, SpawnParameters);
	if (!Grenade)
	{
		NextSpawnTimeSeconds = World->GetTimeSeconds() + 1.f;
		UE_LOG(LogTMGrenadeLoopTestSpawner, Warning, TEXT("Failed to spawn grenade class %s."), *GetNameSafe(GrenadeClass.Get()));
		return;
	}

	ActiveGrenade = Grenade;
	KnownBornActors.Add(TObjectKey<AActor>(Grenade));
	TrackedActors.Add(Grenade);
	++SpawnedGrenadeCount;
	bCycleActive = true;
	AllRemovedSinceSeconds = -1.f;

	CollectBornObjects();

	UE_LOG(
		LogTMGrenadeLoopTestSpawner,
		Log,
		TEXT("Spawned loop grenade %s at %s. Cycle %d."),
		*Grenade->GetPathName(),
		*SpawnLocation.ToCompactString(),
		SpawnedGrenadeCount);
}

void ATMGrenadeLoopTestSpawner::SnapshotExistingObjects()
{
	BaselineActors.Reset();
	KnownBornActors.Reset();
	TrackedActors.Reset();

	BaselineComponents.Reset();
	KnownBornComponents.Reset();
	TrackedComponents.Reset();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (*It)
		{
			BaselineActors.Add(TObjectKey<AActor>(*It));
		}
	}

	for (TObjectIterator<UActorComponent> It; It; ++It)
	{
		UActorComponent* Component = *It;
		if (Component && Component->GetWorld() == World)
		{
			BaselineComponents.Add(TObjectKey<UActorComponent>(Component));
		}
	}
}

void ATMGrenadeLoopTestSpawner::CollectBornObjects()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor == this)
		{
			continue;
		}

		const TObjectKey<AActor> ActorKey(Actor);
		if (BaselineActors.Contains(ActorKey) || KnownBornActors.Contains(ActorKey) || !ShouldTrackBornActor(Actor))
		{
			continue;
		}

		KnownBornActors.Add(ActorKey);
		TrackedActors.Add(Actor);
	}

	for (TObjectIterator<UActorComponent> It; It; ++It)
	{
		UActorComponent* Component = *It;
		if (!Component || Component->GetWorld() != World)
		{
			continue;
		}

		const TObjectKey<UActorComponent> ComponentKey(Component);
		if (BaselineComponents.Contains(ComponentKey) || KnownBornComponents.Contains(ComponentKey) || !ShouldTrackBornComponent(Component))
		{
			continue;
		}

		KnownBornComponents.Add(ComponentKey);
		TrackedComponents.Add(Component);
	}
}

bool ATMGrenadeLoopTestSpawner::AreTrackedObjectsRemoved() const
{
	for (const TWeakObjectPtr<AActor>& TrackedActor : TrackedActors)
	{
		if (!IsTrackedActorRemoved(TrackedActor))
		{
			return false;
		}
	}

	for (const TWeakObjectPtr<UActorComponent>& TrackedComponent : TrackedComponents)
	{
		if (!IsTrackedComponentRemoved(TrackedComponent))
		{
			return false;
		}
	}

	return true;
}

void ATMGrenadeLoopTestSpawner::ClearCycleTracking()
{
	BaselineActors.Reset();
	KnownBornActors.Reset();
	TrackedActors.Reset();
	BaselineComponents.Reset();
	KnownBornComponents.Reset();
	TrackedComponents.Reset();
	ActiveGrenade.Reset();
	AllRemovedSinceSeconds = -1.f;
	bCycleActive = false;
}

bool ATMGrenadeLoopTestSpawner::ShouldTrackBornActor(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}

	if (Actor == ActiveGrenade.Get() || Actor->GetOwner() == ActiveGrenade.Get() || Actor->GetOwner() == this)
	{
		return true;
	}

	if (!bTrackOnlyLikelyGrenadeObjects)
	{
		return true;
	}

	static const TCHAR* Tokens[] = {
		TEXT("Grenade"),
		TEXT("Explosive"),
		TEXT("Explosion"),
		TEXT("Niagara"),
		TEXT("Emitter"),
		TEXT("Particle"),
		TEXT("Decal"),
		TEXT("FoliageGrenadeCollisionPush"),
		TEXT("FoliageCollisionPushTester")
	};

	return TextContainsAnyToken(ActorIdentityText(Actor), Tokens);
}

bool ATMGrenadeLoopTestSpawner::ShouldTrackBornComponent(const UActorComponent* Component) const
{
	if (!Component)
	{
		return false;
	}

	const AActor* ComponentOwner = Component->GetOwner();
	if (ComponentOwner == ActiveGrenade.Get() || ComponentOwner == this || (ComponentOwner && ComponentOwner->GetOwner() == ActiveGrenade.Get()))
	{
		return true;
	}

	if (!bTrackOnlyLikelyGrenadeObjects)
	{
		return true;
	}

	static const TCHAR* Tokens[] = {
		TEXT("Grenade"),
		TEXT("Explosive"),
		TEXT("Explosion"),
		TEXT("Niagara"),
		TEXT("Particle"),
		TEXT("Audio"),
		TEXT("Decal"),
		TEXT("FoliageGrenadeCollisionPush"),
		TEXT("FoliageCollisionPushTester")
	};

	return TextContainsAnyToken(ComponentIdentityText(Component), Tokens);
}

bool ATMGrenadeLoopTestSpawner::IsTrackedActorRemoved(const TWeakObjectPtr<AActor>& Actor) const
{
	const AActor* ResolvedActor = Actor.Get();
	if (!IsValid(ResolvedActor) || ResolvedActor->IsActorBeingDestroyed() || ResolvedActor->IsPendingKillPending())
	{
		return true;
	}

	return bTreatHiddenActorsAsRemoved && ResolvedActor->IsHidden();
}

bool ATMGrenadeLoopTestSpawner::IsTrackedComponentRemoved(const TWeakObjectPtr<UActorComponent>& Component) const
{
	const UActorComponent* ResolvedComponent = Component.Get();
	if (!IsValid(ResolvedComponent))
	{
		return true;
	}

	const AActor* ComponentOwner = ResolvedComponent->GetOwner();
	if (!IsValid(ComponentOwner)
		|| ComponentOwner->IsActorBeingDestroyed()
		|| ComponentOwner->IsPendingKillPending()
		|| (bTreatHiddenActorsAsRemoved && ComponentOwner->IsHidden()))
	{
		return true;
	}

	if (const UNiagaraComponent* NiagaraComponent = Cast<UNiagaraComponent>(ResolvedComponent))
	{
		return NiagaraComponent->IsComplete() || !NiagaraComponent->IsActive();
	}

	if (UParticleSystemComponent* ParticleSystemComponent = Cast<UParticleSystemComponent>(const_cast<UActorComponent*>(ResolvedComponent)))
	{
		return ParticleSystemComponent->HasCompleted() || !ParticleSystemComponent->IsActive();
	}

	if (const UAudioComponent* AudioComponent = Cast<UAudioComponent>(ResolvedComponent))
	{
		return !AudioComponent->IsPlaying();
	}

	if (const UDecalComponent* DecalComponent = Cast<UDecalComponent>(ResolvedComponent))
	{
		return !DecalComponent->IsActive();
	}

	return !ResolvedComponent->IsRegistered();
}

bool ATMGrenadeLoopTestSpawner::IsRuntimeWorld() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	return World->WorldType == EWorldType::Game
		|| World->WorldType == EWorldType::PIE
		|| World->WorldType == EWorldType::GamePreview;
}
