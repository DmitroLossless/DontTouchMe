#include "TMFoliageExplosionCollisionTesterLoop.h"

#include "TMFoliageExplosionCollisionTester.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"

ATMFoliageExplosionCollisionTesterLoop::ATMFoliageExplosionCollisionTesterLoop()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bIsEditorOnlyActor = false;

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;
}

void ATMFoliageExplosionCollisionTesterLoop::BeginPlay()
{
	Super::BeginPlay();
	RespawnCooldownSeconds = 0.f;
	SpawnPulse();
}

void ATMFoliageExplosionCollisionTesterLoop::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!CanRunLoop())
	{
		return;
	}

	RespawnCooldownSeconds = FMath::Max(0.f, RespawnCooldownSeconds - FMath::Max(0.f, DeltaSeconds));
	if (ActivePulse.IsValid() || RespawnCooldownSeconds > 0.f)
	{
		return;
	}

	SpawnPulse();
}

void ATMFoliageExplosionCollisionTesterLoop::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ATMFoliageExplosionCollisionTester* Pulse = ActivePulse.Get())
	{
		Pulse->Destroy();
	}

	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
bool ATMFoliageExplosionCollisionTesterLoop::ShouldTickIfViewportsOnly() const
{
	return bRunInEditor;
}
#endif

ATMFoliageExplosionCollisionTester* ATMFoliageExplosionCollisionTesterLoop::SpawnPulse()
{
	if (!CanRunLoop())
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.Name = MakeUniqueObjectName(World, ATMFoliageExplosionCollisionTester::StaticClass(), TEXT("TM_FoliageExplosionCollisionPulse"));
	SpawnParameters.ObjectFlags = RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ATMFoliageExplosionCollisionTester* Pulse = World->SpawnActor<ATMFoliageExplosionCollisionTester>(
		ATMFoliageExplosionCollisionTester::StaticClass(),
		GetActorLocation(),
		GetActorRotation(),
		SpawnParameters);
	if (!Pulse)
	{
		return nullptr;
	}

	Pulse->Radius = FMath::Max(1.f, Radius);
	Pulse->Strength = FMath::Max(0.f, Strength);
	Pulse->BendDistance = FMath::Max(0.f, BendDistance);
	Pulse->InitialRadiusDivisor = FMath::Max(1.f, InitialRadiusDivisor);
	Pulse->ExpansionDuration = FMath::Max(0.f, ExpansionDuration);
	Pulse->MaterialSpeedScale = FMath::Max(0.f, MaterialSpeedScale);
	Pulse->PullDirection = PullDirection;
	Pulse->bDrawDebugSphere = bDrawDebugSphere;
	Pulse->DebugColor = DebugColor;
	Pulse->bAutoDestroyAfterExpansion = true;
	Pulse->bExpandInEditor = bRunInEditor;
	Pulse->RefreshAffectedFoliage();

	ActivePulse = Pulse;
	RespawnCooldownSeconds = FMath::Max(0.f, ExpansionDuration) + FMath::Max(0.f, RespawnDelay);
	return Pulse;
}

bool ATMFoliageExplosionCollisionTesterLoop::CanRunLoop() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (World->WorldType == EWorldType::Editor)
	{
		return bRunInEditor;
	}

	return World->WorldType == EWorldType::PIE
		|| World->WorldType == EWorldType::Game
		|| World->WorldType == EWorldType::GamePreview;
}
