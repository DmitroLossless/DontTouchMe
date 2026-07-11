#include "TMFoliageImpulseSubsystem.h"

#include "TMFoliageCollisionPushTester.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/UObjectIterator.h"

#include <initializer_list>

DEFINE_LOG_CATEGORY_STATIC(LogTMFoliageImpulse, Log, All);

namespace
{
constexpr ECollisionChannel TMFoliageImpulseVegetationCollisionChannel = ECC_GameTraceChannel6;

static const FName TMFoliageImpulseMWPlayerPositionParameter(TEXT("MW_PlayerPosition"));
static const FName TMFoliageImpulseMWPlayerSpeedParameter(TEXT("MW_PlayerSpeed"));
static const FName TMFoliageImpulseMWBendPositionParameter(TEXT("MW_BendPos"));
static const FName TMFoliageImpulseOriginXMaterialParameter(TEXT("TM_FoliageImpulseOriginX"));
static const FName TMFoliageImpulseOriginYMaterialParameter(TEXT("TM_FoliageImpulseOriginY"));
static const FName TMFoliageImpulseOriginZMaterialParameter(TEXT("TM_FoliageImpulseOriginZ"));
static const FName TMFoliageImpulseMaxOffsetParameter(TEXT("TM_FoliageImpulseMaxOffset"));

constexpr int32 TMFoliageImpulseCustomDataFloatCount = 3;
constexpr int32 TMFoliageImpulseCustomDataAmplitudeIndex = 0;
constexpr int32 TMFoliageImpulseCustomDataDirectionXIndex = 1;
constexpr int32 TMFoliageImpulseCustomDataDirectionYIndex = 2;

static TAutoConsoleVariable<int32> CVarTMFoliageImpulse(
	TEXT("tm.FoliageImpulse"),
	1,
	TEXT("Enables temporary collision spheres that drive skeletal foliage physics near grenade detonations."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarTMFoliageImpulseAutoTrackGrenades(
	TEXT("tm.FoliageImpulse.AutoTrackGrenades"),
	1,
	TEXT("Automatically triggers foliage physics pulses when grenade actors are hidden or destroyed."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarTMFoliageImpulseDebug(
	TEXT("tm.FoliageImpulse.Debug"),
	0,
	TEXT("Logs foliage impulse tracking, temporary sphere overlaps, and MW material pulse updates."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarTMFoliageImpulseRadius(
	TEXT("tm.FoliageImpulse.Radius"),
	220.f,
	TEXT("Default maximum foliage impulse sphere radius in centimeters."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarTMFoliageImpulseStrength(
	TEXT("tm.FoliageImpulse.Strength"),
	75000.f,
	TEXT("Legacy impulse strength. Skeletal foliage collision pulses use Radius and Duration."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarTMFoliageImpulseDuration(
	TEXT("tm.FoliageImpulse.Duration"),
	0.5f,
	TEXT("Seconds for the temporary foliage collision sphere to expand and exist before being destroyed."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarTMFoliageImpulseMaxComponents(
	TEXT("tm.FoliageImpulse.MaxComponents"),
	220,
	TEXT("Maximum foliage components prepared for one explosion sphere."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarTMFoliageImpulseMaterialSpeedScale(
	TEXT("tm.FoliageImpulse.MaterialSpeedScale"),
	0.012f,
	TEXT("Converts grenade impulse strength into MW_PlayerSpeed material parameter units."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarTMFoliageImpulseMaterialBendDistance(
	TEXT("tm.FoliageImpulse.MaterialBendDistance"),
	240.f,
	TEXT("Maximum MW_BendPos offset from the explosion origin in centimeters."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarTMFoliageImpulseInitialRadiusDivisor(
	TEXT("tm.FoliageImpulse.InitialRadiusDivisor"),
	25.f,
	TEXT("Temporary foliage impulse spheres start at Radius divided by this value before expanding."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarTMFoliageImpulseMaterialFrequency(
	TEXT("tm.FoliageImpulse.MaterialFrequency"),
	8.f,
	TEXT("Oscillation frequency for the MW material pulse."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarTMFoliageImpulseMaterialDamping(
	TEXT("tm.FoliageImpulse.MaterialDamping"),
	2.8f,
	TEXT("Damping applied to the MW material pulse."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarTMFoliageImpulseMaterialRestThreshold(
	TEXT("tm.FoliageImpulse.MaterialRestThreshold"),
	0.015f,
	TEXT("Material pulse amplitude below which the MW_PlayerSpeed parameter is cleared."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarTMFoliageImpulseGrenadeScanInterval(
	TEXT("tm.FoliageImpulse.GrenadeScanInterval"),
	0.05f,
	TEXT("Seconds between scans for grenade actors to track."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarTMFoliageImpulseDuplicateWindow(
	TEXT("tm.FoliageImpulse.DuplicateWindow"),
	0.25f,
	TEXT("Seconds during which nearby duplicate foliage impulses are ignored."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarTMFoliageImpulseDuplicateDistance(
	TEXT("tm.FoliageImpulse.DuplicateDistance"),
	180.f,
	TEXT("Distance in centimeters used to suppress duplicate foliage impulses from the same detonation."),
	ECVF_Default);

bool TMImpulsePathContainsAny(const FString& Path, std::initializer_list<const TCHAR*> Tokens)
{
	for (const TCHAR* Token : Tokens)
	{
		if (Path.Contains(Token, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

bool IsDebugLoggingEnabled()
{
	return CVarTMFoliageImpulseDebug.GetValueOnGameThread() != 0;
}

bool TMImpulseShouldAffectStaticMesh(const UStaticMesh* StaticMesh)
{
	if (!StaticMesh)
	{
		return false;
	}

	const FString Path = StaticMesh->GetPathName();
	if (TMImpulsePathContainsAny(Path, {
		TEXT("Boulder"),
		TEXT("Rock"),
		TEXT("Stone"),
		TEXT("Cliff"),
		TEXT("Wall"),
		TEXT("Pillar"),
		TEXT("Weapon"),
		TEXT("GrenadeLauncher")
	}))
	{
		return false;
	}

	return TMImpulsePathContainsAny(Path, {
		TEXT("Bush"),
		TEXT("Plant"),
		TEXT("Plants"),
		TEXT("Shrub"),
		TEXT("Sapling"),
		TEXT("Saplings"),
		TEXT("Seedling"),
		TEXT("Seedlings"),
		TEXT("Branch"),
		TEXT("Branches"),
		TEXT("Twig"),
		TEXT("Twigs"),
		TEXT("Needle"),
		TEXT("Needles"),
		TEXT("Leaf"),
		TEXT("Leaves"),
		TEXT("Grass"),
		TEXT("Fern"),
		TEXT("Cover"),
		TEXT("TreeFirSmall"),
		TEXT("SmallTree"),
		TEXT("SmallTrees"),
		TEXT("MWConiferForest/Meshes/Plants"),
		TEXT("MWConiferForest/Meshes/Cover"),
		TEXT("MWConiferForest/Meshes/Trees/Fir/SM_CF_TreeFirSaplings"),
		TEXT("MWConiferForest/Meshes/Trees/Fir/SM_CF_TreeFirSeedlings")
	});
}

UStaticMesh* TMImpulseGetStaticMeshFromPrimitive(const UPrimitiveComponent* Component)
{
	if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
	{
		return StaticMeshComponent->GetStaticMesh();
	}

	if (const UInstancedStaticMeshComponent* InstancedStaticMeshComponent = Cast<UInstancedStaticMeshComponent>(Component))
	{
		return InstancedStaticMeshComponent->GetStaticMesh();
	}

	return nullptr;
}

bool TMImpulseEnsureSimpleCollisionOnStaticMesh(UStaticMesh* StaticMesh)
{
	if (!StaticMesh)
	{
		return false;
	}

	UBodySetup* BodySetup = StaticMesh->GetBodySetup();
	if (!BodySetup)
	{
		return false;
	}

	if (BodySetup->AggGeom.GetElementCount() > 0)
	{
		return true;
	}

	const FBoxSphereBounds Bounds = StaticMesh->GetBounds();
	if (Bounds.BoxExtent.IsNearlyZero())
	{
		return false;
	}

	FKBoxElem BoxElem;
	BoxElem.Center = Bounds.Origin;
	BoxElem.X = FMath::Max(2.f, Bounds.BoxExtent.X * 2.f);
	BoxElem.Y = FMath::Max(2.f, Bounds.BoxExtent.Y * 2.f);
	BoxElem.Z = FMath::Max(2.f, Bounds.BoxExtent.Z * 2.f);
	BodySetup->AggGeom.BoxElems.Add(BoxElem);
	BodySetup->CollisionTraceFlag = CTF_UseSimpleAndComplex;
	BodySetup->InvalidatePhysicsData();
	BodySetup->CreatePhysicsMeshes();
	return true;
}

void TMImpulsePrepareFoliageWPO(UPrimitiveComponent* Component)
{
	if (!Component)
	{
		return;
	}

	if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
	{
		StaticMeshComponent->SetEvaluateWorldPositionOffset(true);
		StaticMeshComponent->SetWorldPositionOffsetDisableDistance(0);
	}

	const int32 MaterialCount = Component->GetNumMaterials();
	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		UMaterialInstanceDynamic* DynamicMaterial = Cast<UMaterialInstanceDynamic>(Component->GetMaterial(MaterialIndex));
		if (!DynamicMaterial)
		{
			DynamicMaterial = Component->CreateDynamicMaterialInstance(MaterialIndex);
		}

		if (!DynamicMaterial)
		{
			continue;
		}

		DynamicMaterial->SetScalarParameterValue(TEXT("MW_InteractionBendIntensity"), 2.5f);
		DynamicMaterial->SetScalarParameterValue(TEXT("MW_InteractionPushIntensity"), 3.0f);
		DynamicMaterial->SetScalarParameterValue(TEXT("MW_InteractionBendPosIntensity"), 1.0f);
		DynamicMaterial->SetScalarParameterValue(TEXT("MW_NaniteWPODistanceLength"), 100000.f);
		DynamicMaterial->SetScalarParameterValue(TEXT("MW_NaniteWPODistanceOffset"), 100000.f);
		DynamicMaterial->SetScalarParameterValue(TMFoliageImpulseMaxOffsetParameter, 220.f);
	}

	Component->MarkRenderStateDirty();
}

void EnsureFoliageInstanceCustomData(UInstancedStaticMeshComponent* Component)
{
	if (!Component)
	{
		return;
	}

	Component->SetNumCustomDataFloats(FMath::Max(Component->NumCustomDataFloats, TMFoliageImpulseCustomDataFloatCount));
}

void SetFoliageInstanceCustomData(
	UInstancedStaticMeshComponent* Component,
	const int32 InstanceIndex,
	const float Amplitude,
	const FVector2D& Direction)
{
	if (!Component || InstanceIndex == INDEX_NONE)
	{
		return;
	}

	EnsureFoliageInstanceCustomData(Component);
	Component->SetCustomDataValue(InstanceIndex, TMFoliageImpulseCustomDataAmplitudeIndex, Amplitude, false);
	Component->SetCustomDataValue(InstanceIndex, TMFoliageImpulseCustomDataDirectionXIndex, Direction.X, false);
	Component->SetCustomDataValue(InstanceIndex, TMFoliageImpulseCustomDataDirectionYIndex, Direction.Y, true);
}

void ClearFoliageInstanceCustomData(UInstancedStaticMeshComponent* Component, const int32 InstanceIndex)
{
	if (!Component || InstanceIndex == INDEX_NONE)
	{
		return;
	}

	EnsureFoliageInstanceCustomData(Component);
	Component->SetCustomDataValue(InstanceIndex, TMFoliageImpulseCustomDataAmplitudeIndex, 0.f, false);
	Component->SetCustomDataValue(InstanceIndex, TMFoliageImpulseCustomDataDirectionXIndex, 0.f, false);
	Component->SetCustomDataValue(InstanceIndex, TMFoliageImpulseCustomDataDirectionYIndex, 0.f, true);
}

bool TMImpulseComponentBoundsIntersectSphere(const UPrimitiveComponent* Component, const FVector& Origin, const float Radius)
{
	if (!Component)
	{
		return false;
	}

	return Component->Bounds.GetBox().ComputeSquaredDistanceToPoint(Origin) <= FMath::Square(Radius);
}

float GetInitialImpulseSphereRadius(const float MaxRadius)
{
	const float Divisor = FMath::Max(1.f, CVarTMFoliageImpulseInitialRadiusDivisor.GetValueOnGameThread());
	return FMath::Max(1.f, MaxRadius / Divisor);
}

void ConfigureVegetationOnlySweepSphere(USphereComponent* SphereComponent)
{
	if (!SphereComponent)
	{
		return;
	}

	SphereComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SphereComponent->SetCollisionObjectType(TMFoliageImpulseVegetationCollisionChannel);
	SphereComponent->SetCollisionResponseToAllChannels(ECR_Ignore);

	SphereComponent->SetCollisionResponseToChannel(TMFoliageImpulseVegetationCollisionChannel, ECR_Block);
	SphereComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	SphereComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	SphereComponent->SetCollisionResponseToChannel(ECC_GameTraceChannel3, ECR_Ignore);
	SphereComponent->SetCollisionResponseToChannel(ECC_GameTraceChannel4, ECR_Ignore);
	SphereComponent->SetGenerateOverlapEvents(true);
	SphereComponent->SetCanEverAffectNavigation(false);
}

FVector MakePulseDirection(const FVector& Origin)
{
	const uint32 Seed = GetTypeHash(Origin.ToString());
	FRandomStream Stream(static_cast<int32>(Seed));
	FVector Direction(Stream.FRandRange(-1.f, 1.f), Stream.FRandRange(-1.f, 1.f), 0.f);
	if (Direction.Normalize())
	{
		return Direction;
	}

	return FVector::ForwardVector;
}
}

void UTMFoliageImpulseSubsystem::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!IsSubsystemEnabled())
	{
		return;
	}

	TickActiveImpulseSpheres(DeltaTime);
	TickActiveMaterialPulses(DeltaTime);
	TickActiveInstancePulses(DeltaTime);
	ScanForGrenadeActors(DeltaTime);
	UpdateTrackedGrenadeActors();

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;
	const float DuplicateWindow = FMath::Max(0.f, CVarTMFoliageImpulseDuplicateWindow.GetValueOnGameThread());
	for (int32 Index = RecentImpulses.Num() - 1; Index >= 0; --Index)
	{
		if (Now - RecentImpulses[Index].TimeSeconds > DuplicateWindow)
		{
			RecentImpulses.RemoveAtSwap(Index);
		}
	}
}

TStatId UTMFoliageImpulseSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTMFoliageImpulseSubsystem, STATGROUP_Tickables);
}

bool UTMFoliageImpulseSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

void UTMFoliageImpulseSubsystem::AddDefaultRadialImpulse(const FVector& Origin)
{
	AddRadialImpulse(
		Origin,
		CVarTMFoliageImpulseRadius.GetValueOnGameThread(),
		CVarTMFoliageImpulseStrength.GetValueOnGameThread(),
		CVarTMFoliageImpulseDuration.GetValueOnGameThread());
}

void UTMFoliageImpulseSubsystem::AddRadialImpulse(
	const FVector& Origin,
	const float Radius,
	const float ImpulseStrength,
	const float Duration)
{
	UWorld* World = GetWorld();
	if (!World
		|| World->GetNetMode() == NM_DedicatedServer
		|| !IsSubsystemEnabled()
		|| Radius <= KINDA_SMALL_NUMBER
		|| ImpulseStrength <= KINDA_SMALL_NUMBER
		|| Duration <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	if (IsDuplicateRecentImpulse(Origin))
	{
		if (IsDebugLoggingEnabled())
		{
			UE_LOG(LogTMFoliageImpulse, Log, TEXT("Suppressed duplicate foliage physics pulse near %s."), *Origin.ToCompactString());
		}
		return;
	}

	AddRecentImpulse(Origin);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = MakeUniqueObjectName(World, ATMFoliageCollisionPushTester::StaticClass(), TEXT("TM_FoliageGrenadeCollisionPush"));
	SpawnParameters.ObjectFlags = RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.bDeferConstruction = true;

	const FTransform SpawnTransform(FRotator::ZeroRotator, Origin);
	ATMFoliageCollisionPushTester* Tester = World->SpawnActor<ATMFoliageCollisionPushTester>(
		ATMFoliageCollisionPushTester::StaticClass(),
		SpawnTransform,
		SpawnParameters);
	if (!Tester)
	{
		return;
	}

	Tester->Radius = FMath::Max(1.f, Radius);
	Tester->InitialRadiusDivisor = FMath::Max(1.f, CVarTMFoliageImpulseInitialRadiusDivisor.GetValueOnGameThread());
	Tester->ExpansionDuration = FMath::Max(0.f, Duration);
	Tester->bAutoDestroyAfterExpansion = true;
	Tester->bLoopPulses = false;
	Tester->bCreatePhysicsProxyBodies = false;
	Tester->bUseSkeletalReplacements = true;
	Tester->bRemoveOriginalInstances = true;
	Tester->MaxSkeletalReplacements = FMath::Max(1, CVarTMFoliageImpulseMaxComponents.GetValueOnGameThread());
	Tester->SkeletalSimulationRootBone = TEXT("foliage_01");
	Tester->bDrawDebug = IsDebugLoggingEnabled();
	Tester->FinishSpawning(SpawnTransform);

	if (IsDebugLoggingEnabled())
	{
		UE_LOG(
			LogTMFoliageImpulse,
			Log,
			TEXT("Spawned foliage skeletal collision push at %s radius %.1f duration %.2f."),
			*Origin.ToCompactString(),
			Tester->Radius,
			Tester->ExpansionDuration);
	}
}

void UTMFoliageImpulseSubsystem::SpawnImpulseSphere(
	const FVector& Origin,
	const float Radius,
	const float ImpulseStrength,
	const float Duration)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<TWeakObjectPtr<UPrimitiveComponent>> CandidateComponents;
	CandidateComponents.Reserve(64);

	for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
	{
		UPrimitiveComponent* Component = *It;
		if (!Component || Component->GetWorld() != World || !ShouldAffectFoliageComponent(Component, Origin, Radius))
		{
			continue;
		}

		PrepareFoliageCollision(Component);
		CandidateComponents.Add(Component);

		if (CandidateComponents.Num() >= FMath::Max(1, CVarTMFoliageImpulseMaxComponents.GetValueOnGameThread()))
		{
			break;
		}
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = MakeUniqueObjectName(World, AActor::StaticClass(), TEXT("TM_FoliageImpulseSphere"));
	SpawnParameters.ObjectFlags = RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* SphereActor = World->SpawnActor<AActor>(AActor::StaticClass(), Origin, FRotator::ZeroRotator, SpawnParameters);
	if (!SphereActor)
	{
		return;
	}

	SphereActor->SetActorHiddenInGame(true);
	SphereActor->SetActorEnableCollision(true);
	SphereActor->SetLifeSpan(Duration + 0.1f);

	USphereComponent* SphereComponent = NewObject<USphereComponent>(SphereActor, TEXT("TMFoliageImpulseSphere"));
	if (!SphereComponent)
	{
		SphereActor->Destroy();
		return;
	}

	const float InitialRadius = GetInitialImpulseSphereRadius(Radius);
	SphereComponent->SetMobility(EComponentMobility::Movable);
	SphereComponent->InitSphereRadius(InitialRadius);
	SphereComponent->SetWorldLocation(Origin);
	ConfigureVegetationOnlySweepSphere(SphereComponent);

	SphereActor->SetRootComponent(SphereComponent);
	SphereActor->AddInstanceComponent(SphereComponent);
	SphereComponent->RegisterComponent();
	SphereComponent->UpdateOverlaps();

	FActiveImpulseSphere ActiveSphere;
	ActiveSphere.Actor = SphereActor;
	ActiveSphere.SphereComponent = SphereComponent;
	ActiveSphere.Origin = Origin;
	ActiveSphere.Radius = Radius;
	ActiveSphere.ImpulseStrength = ImpulseStrength;
	ActiveSphere.DurationSeconds = Duration;
	ActiveSphere.CandidateComponents = MoveTemp(CandidateComponents);

	if (IsDebugLoggingEnabled())
	{
		UE_LOG(
			LogTMFoliageImpulse,
			Log,
			TEXT("Spawned foliage detection sphere at %s radius %.1f duration %.2f prepared components %d."),
			*Origin.ToCompactString(),
			Radius,
			Duration,
			ActiveSphere.CandidateComponents.Num());
	}

	ProcessImpulseSphere(ActiveSphere, InitialRadius);
	ActiveImpulseSpheres.Add(MoveTemp(ActiveSphere));
}

void UTMFoliageImpulseSubsystem::TickActiveImpulseSpheres(const float DeltaTime)
{
	for (int32 Index = ActiveImpulseSpheres.Num() - 1; Index >= 0; --Index)
	{
		FActiveImpulseSphere& ImpulseSphere = ActiveImpulseSpheres[Index];
		USphereComponent* SphereComponent = ImpulseSphere.SphereComponent.Get();
		AActor* SphereActor = ImpulseSphere.Actor.Get();
		if (!SphereComponent || !SphereActor)
		{
			ActiveImpulseSpheres.RemoveAtSwap(Index);
			continue;
		}

		ImpulseSphere.ElapsedSeconds += FMath::Max(0.f, DeltaTime);
		const float Alpha = FMath::Clamp(ImpulseSphere.ElapsedSeconds / FMath::Max(KINDA_SMALL_NUMBER, ImpulseSphere.DurationSeconds), 0.f, 1.f);
		const float InitialRadius = GetInitialImpulseSphereRadius(ImpulseSphere.Radius);
		const float CurrentRadius = FMath::Lerp(InitialRadius, ImpulseSphere.Radius, Alpha);
		SphereComponent->SetSphereRadius(CurrentRadius, true);
		SphereComponent->SetWorldLocation(ImpulseSphere.Origin, false, nullptr, ETeleportType::TeleportPhysics);
		SphereComponent->UpdateOverlaps();

		ProcessImpulseSphere(ImpulseSphere, CurrentRadius);

		if (ImpulseSphere.ElapsedSeconds >= ImpulseSphere.DurationSeconds)
		{
			DestroyImpulseSphere(ImpulseSphere);
			ActiveImpulseSpheres.RemoveAtSwap(Index);
		}
	}
}

void UTMFoliageImpulseSubsystem::ProcessImpulseSphere(FActiveImpulseSphere& ImpulseSphere, const float CurrentRadius)
{
	USphereComponent* SphereComponent = ImpulseSphere.SphereComponent.Get();
	if (!SphereComponent)
	{
		return;
	}

	TArray<UPrimitiveComponent*> OverlappingComponents;
	SphereComponent->GetOverlappingComponents(OverlappingComponents);

	int32 NewlyProcessedCount = 0;
	auto TryProcessComponent = [this, &ImpulseSphere, CurrentRadius, &NewlyProcessedCount](UPrimitiveComponent* Component)
	{
		if (!Component
			|| Component == ImpulseSphere.SphereComponent.Get()
			|| ImpulseSphere.ProcessedComponents.Contains(Component)
			|| !ShouldAffectFoliageComponent(Component, ImpulseSphere.Origin, ImpulseSphere.Radius))
		{
			return;
		}

		PrepareFoliageCollision(Component);
		AddInstancePulsesForComponent(Component, ImpulseSphere.Origin, CurrentRadius);
		ImpulseSphere.ProcessedComponents.Add(Component);
		++NewlyProcessedCount;
	};

	for (UPrimitiveComponent* Component : OverlappingComponents)
	{
		TryProcessComponent(Component);
	}

	const float CurrentRadiusSquared = FMath::Square(CurrentRadius);
	for (const TWeakObjectPtr<UPrimitiveComponent>& CandidateComponent : ImpulseSphere.CandidateComponents)
	{
		UPrimitiveComponent* Component = CandidateComponent.Get();
		if (!Component || ImpulseSphere.ProcessedComponents.Contains(Component))
		{
			continue;
		}

		if (Component->Bounds.GetBox().ComputeSquaredDistanceToPoint(ImpulseSphere.Origin) <= CurrentRadiusSquared)
		{
			TryProcessComponent(Component);
		}
	}

	if (NewlyProcessedCount > 0 && !ImpulseSphere.bStartedMaterialPulse)
	{
		ImpulseSphere.bStartedMaterialPulse = true;
		StartMaterialPulse(ImpulseSphere.Origin, ImpulseSphere.ImpulseStrength);
	}

	if (NewlyProcessedCount > 0 && IsDebugLoggingEnabled())
	{
		UE_LOG(
			LogTMFoliageImpulse,
			Log,
			TEXT("Foliage detection sphere at %s radius %.1f processed %d components; total %d."),
			*ImpulseSphere.Origin.ToCompactString(),
			CurrentRadius,
			NewlyProcessedCount,
			ImpulseSphere.ProcessedComponents.Num());
	}
}

void UTMFoliageImpulseSubsystem::DestroyImpulseSphere(FActiveImpulseSphere& ImpulseSphere) const
{
	if (USphereComponent* SphereComponent = ImpulseSphere.SphereComponent.Get())
	{
		SphereComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SphereComponent->DestroyComponent();
	}

	if (AActor* SphereActor = ImpulseSphere.Actor.Get())
	{
		SphereActor->Destroy();
	}
}

void UTMFoliageImpulseSubsystem::StartMaterialPulse(const FVector& Origin, const float ImpulseStrength)
{
	FActiveMaterialPulse Pulse;
	Pulse.Origin = Origin;
	Pulse.Direction = MakePulseDirection(Origin);
	Pulse.Strength = ImpulseStrength;
	Pulse.ElapsedSeconds = 0.f;
	ActiveMaterialPulses.Add(Pulse);

	ApplyMaterialPulse(Pulse, 1.f);

	if (IsDebugLoggingEnabled())
	{
		UE_LOG(
			LogTMFoliageImpulse,
			Log,
			TEXT("Started MW foliage material pulse at %s strength %.1f."),
			*Origin.ToCompactString(),
			ImpulseStrength);
	}
}

void UTMFoliageImpulseSubsystem::TickActiveMaterialPulses(const float DeltaTime)
{
	bool bAnyActivePulse = false;
	for (int32 Index = ActiveMaterialPulses.Num() - 1; Index >= 0; --Index)
	{
		FActiveMaterialPulse& Pulse = ActiveMaterialPulses[Index];
		Pulse.ElapsedSeconds += FMath::Max(0.f, DeltaTime);

		const float Frequency = FMath::Max(0.1f, CVarTMFoliageImpulseMaterialFrequency.GetValueOnGameThread());
		const float Damping = FMath::Max(0.f, CVarTMFoliageImpulseMaterialDamping.GetValueOnGameThread());
		const float Envelope = FMath::Exp(-Damping * Pulse.ElapsedSeconds);
		const float SignedAmplitude = FMath::Cos(Pulse.ElapsedSeconds * Frequency * UE_TWO_PI) * Envelope;
		const float RestThreshold = FMath::Max(0.f, CVarTMFoliageImpulseMaterialRestThreshold.GetValueOnGameThread());

		if (FMath::Abs(SignedAmplitude) <= RestThreshold)
		{
			ActiveMaterialPulses.RemoveAtSwap(Index);
			continue;
		}

		ApplyMaterialPulse(Pulse, SignedAmplitude);
		bAnyActivePulse = true;
	}

	if (!bAnyActivePulse && ActiveMaterialPulses.IsEmpty())
	{
		ClearMaterialPulse();
	}
}

void UTMFoliageImpulseSubsystem::TickActiveInstancePulses(const float DeltaTime)
{
	const float Frequency = FMath::Max(0.1f, CVarTMFoliageImpulseMaterialFrequency.GetValueOnGameThread());
	const float Damping = FMath::Max(0.f, CVarTMFoliageImpulseMaterialDamping.GetValueOnGameThread());
	const float RestThreshold = FMath::Max(0.f, CVarTMFoliageImpulseMaterialRestThreshold.GetValueOnGameThread());

	for (int32 Index = ActiveInstancePulses.Num() - 1; Index >= 0; --Index)
	{
		FActiveInstancePulse& Pulse = ActiveInstancePulses[Index];
		UInstancedStaticMeshComponent* Component = Pulse.Component.Get();
		if (!Component || !Component->IsValidInstance(Pulse.InstanceIndex))
		{
			ActiveInstancePulses.RemoveAtSwap(Index);
			continue;
		}

		Pulse.ElapsedSeconds += FMath::Max(0.f, DeltaTime);
		const float Envelope = FMath::Exp(-Damping * Pulse.ElapsedSeconds);
		const float SignedAmplitude = FMath::Cos(Pulse.ElapsedSeconds * Frequency * UE_TWO_PI) * Envelope * Pulse.Weight;

		if (FMath::Abs(SignedAmplitude) <= RestThreshold)
		{
			ClearFoliageInstanceCustomData(Component, Pulse.InstanceIndex);
			ActiveInstancePulses.RemoveAtSwap(Index);
			continue;
		}

		SetFoliageInstanceCustomData(Component, Pulse.InstanceIndex, SignedAmplitude, Pulse.Direction);
	}
}

void UTMFoliageImpulseSubsystem::AddInstancePulsesForComponent(
	UPrimitiveComponent* Component,
	const FVector& Origin,
	const float Radius)
{
	UInstancedStaticMeshComponent* InstancedStaticMeshComponent = Cast<UInstancedStaticMeshComponent>(Component);
	if (!InstancedStaticMeshComponent || Radius <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	EnsureFoliageInstanceCustomData(InstancedStaticMeshComponent);

	FTransform InstanceTransform;
	const float RadiusSquared = FMath::Square(Radius);
	const int32 InstanceCount = InstancedStaticMeshComponent->GetInstanceCount();
	for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; ++InstanceIndex)
	{
		if (!InstancedStaticMeshComponent->GetInstanceTransform(InstanceIndex, InstanceTransform, true))
		{
			continue;
		}

		FVector ToInstance = InstanceTransform.GetLocation() - Origin;
		ToInstance.Z = 0.f;
		const float DistanceSquared = ToInstance.SizeSquared();
		if (DistanceSquared > RadiusSquared)
		{
			continue;
		}

		const float Distance = FMath::Sqrt(DistanceSquared);
		FVector2D Direction(1.f, 0.f);
		if (Distance > 1.f)
		{
			Direction = FVector2D(ToInstance.X / Distance, ToInstance.Y / Distance);
		}

		const float Weight = FMath::Clamp(1.f - Distance / Radius, 0.f, 1.f);
		if (Weight <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		FActiveInstancePulse* ExistingPulse = ActiveInstancePulses.FindByPredicate(
			[InstancedStaticMeshComponent, InstanceIndex](const FActiveInstancePulse& Pulse)
			{
				return Pulse.Component.Get() == InstancedStaticMeshComponent && Pulse.InstanceIndex == InstanceIndex;
			});

		if (!ExistingPulse)
		{
			ExistingPulse = &ActiveInstancePulses.AddDefaulted_GetRef();
			ExistingPulse->Component = InstancedStaticMeshComponent;
			ExistingPulse->InstanceIndex = InstanceIndex;
		}

		ExistingPulse->Direction = Direction;
		ExistingPulse->Weight = FMath::Max(ExistingPulse->Weight, Weight);
		ExistingPulse->ElapsedSeconds = 0.f;
		SetFoliageInstanceCustomData(InstancedStaticMeshComponent, InstanceIndex, ExistingPulse->Weight, Direction);
	}
}

void UTMFoliageImpulseSubsystem::ApplyMaterialPulse(const FActiveMaterialPulse& Pulse, const float SignedAmplitude)
{
	UWorld* World = GetWorld();
	UMaterialParameterCollection* Collection = GetMWControllerCollection();
	if (!World || !Collection)
	{
		return;
	}

	UMaterialParameterCollectionInstance* CollectionInstance = World->GetParameterCollectionInstance(Collection);
	if (!CollectionInstance)
	{
		return;
	}

	const float SpeedScale = FMath::Max(0.f, CVarTMFoliageImpulseMaterialSpeedScale.GetValueOnGameThread());
	const float BendDistance = FMath::Max(0.f, CVarTMFoliageImpulseMaterialBendDistance.GetValueOnGameThread());
	const float Speed = SignedAmplitude * Pulse.Strength * SpeedScale;
	const FVector BendPosition = Pulse.Origin + Pulse.Direction * (SignedAmplitude * BendDistance);

	CollectionInstance->SetVectorParameterValue(TMFoliageImpulseMWPlayerPositionParameter, FLinearColor(Pulse.Origin.X, Pulse.Origin.Y, Pulse.Origin.Z, 1.f));
	CollectionInstance->SetVectorParameterValue(TMFoliageImpulseMWBendPositionParameter, FLinearColor(BendPosition.X, BendPosition.Y, BendPosition.Z, 1.f));
	CollectionInstance->SetScalarParameterValue(TMFoliageImpulseMWPlayerSpeedParameter, Speed);
	CollectionInstance->SetScalarParameterValue(TMFoliageImpulseOriginXMaterialParameter, Pulse.Origin.X);
	CollectionInstance->SetScalarParameterValue(TMFoliageImpulseOriginYMaterialParameter, Pulse.Origin.Y);
	CollectionInstance->SetScalarParameterValue(TMFoliageImpulseOriginZMaterialParameter, Pulse.Origin.Z);
}

void UTMFoliageImpulseSubsystem::ClearMaterialPulse()
{
	UWorld* World = GetWorld();
	UMaterialParameterCollection* Collection = GetMWControllerCollection();
	if (!World || !Collection)
	{
		return;
	}

	UMaterialParameterCollectionInstance* CollectionInstance = World->GetParameterCollectionInstance(Collection);
	if (!CollectionInstance)
	{
		return;
	}

	CollectionInstance->SetScalarParameterValue(TMFoliageImpulseMWPlayerSpeedParameter, 0.f);
}

UMaterialParameterCollection* UTMFoliageImpulseSubsystem::GetMWControllerCollection()
{
	if (UMaterialParameterCollection* Collection = MWControllerCollection.Get())
	{
		return Collection;
	}

	UMaterialParameterCollection* Collection = LoadObject<UMaterialParameterCollection>(
		nullptr,
		TEXT("/Game/MWCommon/_Legacy2/Materials/MASTER/Par/MPC_MW_Controller.MPC_MW_Controller"));
	MWControllerCollection = Collection;
	return Collection;
}

void UTMFoliageImpulseSubsystem::ScanForGrenadeActors(const float DeltaTime)
{
	if (!IsAutoGrenadeTrackingEnabled())
	{
		TrackedGrenadeActors.Empty();
		GrenadeScanAccumulatorSeconds = 0.f;
		return;
	}

	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	GrenadeScanAccumulatorSeconds += FMath::Max(0.f, DeltaTime);
	const float ScanInterval = FMath::Max(0.01f, CVarTMFoliageImpulseGrenadeScanInterval.GetValueOnGameThread());
	if (GrenadeScanAccumulatorSeconds < ScanInterval)
	{
		return;
	}
	GrenadeScanAccumulatorSeconds = 0.f;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsGrenadeImpulseActor(Actor) || TrackedGrenadeActors.Contains(Actor))
		{
			continue;
		}

		FTrackedGrenadeActor State;
		State.Actor = Actor;
		State.LastLocation = Actor->GetActorLocation();
		State.DebugName = Actor->GetPathName();
		State.DebugClassPath = Actor->GetClass() ? Actor->GetClass()->GetPathName() : FString();
		State.bWasHidden = Actor->IsHidden();
		TrackedGrenadeActors.Add(Actor, State);

		if (IsDebugLoggingEnabled())
		{
			UE_LOG(
				LogTMFoliageImpulse,
				Log,
				TEXT("Tracking grenade actor %s class %s at %s."),
				*State.DebugName,
				*State.DebugClassPath,
				*State.LastLocation.ToCompactString());
		}
	}
}

void UTMFoliageImpulseSubsystem::UpdateTrackedGrenadeActors()
{
	for (auto It = TrackedGrenadeActors.CreateIterator(); It; ++It)
	{
		FTrackedGrenadeActor& State = It.Value();
		AActor* Actor = State.Actor.Get();
		if (!IsValid(Actor))
		{
			if (IsDebugLoggingEnabled())
			{
				UE_LOG(
					LogTMFoliageImpulse,
					Log,
					TEXT("Tracked grenade actor destroyed: %s class %s last location %s."),
					*State.DebugName,
					*State.DebugClassPath,
					*State.LastLocation.ToCompactString());
			}
			TriggerTrackedGrenadeImpulse(State);
			It.RemoveCurrent();
			continue;
		}

		State.LastLocation = Actor->GetActorLocation();
		const bool bHidden = Actor->IsHidden();
		if (!State.bTriggered && bHidden && !State.bWasHidden)
		{
			if (IsDebugLoggingEnabled())
			{
				UE_LOG(
					LogTMFoliageImpulse,
					Log,
					TEXT("Tracked grenade actor hidden: %s class %s at %s."),
					*State.DebugName,
					*State.DebugClassPath,
					*State.LastLocation.ToCompactString());
			}
			TriggerTrackedGrenadeImpulse(State);
		}
		State.bWasHidden = bHidden;
	}
}

void UTMFoliageImpulseSubsystem::TriggerTrackedGrenadeImpulse(FTrackedGrenadeActor& State)
{
	if (State.bTriggered)
	{
		return;
	}

	State.bTriggered = true;
	if (IsDebugLoggingEnabled())
	{
		UE_LOG(
			LogTMFoliageImpulse,
			Log,
			TEXT("Triggering foliage physics pulse from grenade actor %s at %s."),
			*State.DebugName,
			*State.LastLocation.ToCompactString());
	}
	AddDefaultRadialImpulse(State.LastLocation);
}

bool UTMFoliageImpulseSubsystem::IsDuplicateRecentImpulse(const FVector& Origin) const
{
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;
	const float DuplicateWindow = FMath::Max(0.f, CVarTMFoliageImpulseDuplicateWindow.GetValueOnGameThread());
	const float DuplicateDistanceSquared = FMath::Square(FMath::Max(0.f, CVarTMFoliageImpulseDuplicateDistance.GetValueOnGameThread()));

	for (const FRecentImpulse& RecentImpulse : RecentImpulses)
	{
		if (Now - RecentImpulse.TimeSeconds <= DuplicateWindow
			&& FVector::DistSquared(Origin, RecentImpulse.Origin) <= DuplicateDistanceSquared)
		{
			return true;
		}
	}

	return false;
}

void UTMFoliageImpulseSubsystem::AddRecentImpulse(const FVector& Origin)
{
	const UWorld* World = GetWorld();
	FRecentImpulse RecentImpulse;
	RecentImpulse.Origin = Origin;
	RecentImpulse.TimeSeconds = World ? World->GetTimeSeconds() : 0.f;
	RecentImpulses.Add(RecentImpulse);
}

bool UTMFoliageImpulseSubsystem::IsSubsystemEnabled()
{
	return CVarTMFoliageImpulse.GetValueOnGameThread() != 0;
}

bool UTMFoliageImpulseSubsystem::IsAutoGrenadeTrackingEnabled()
{
	return IsSubsystemEnabled() && CVarTMFoliageImpulseAutoTrackGrenades.GetValueOnGameThread() != 0;
}

bool UTMFoliageImpulseSubsystem::IsGrenadeImpulseActor(const AActor* Actor)
{
	if (!Actor || Actor->HasAnyFlags(RF_ClassDefaultObject))
	{
		return false;
	}

	const UClass* ActorClass = Actor->GetClass();
	if (!ActorClass)
	{
		return false;
	}

	const FString ClassPath = ActorClass->GetPathName();
	const FString ActorPath = Actor->GetPathName();
	const FString CombinedPath = ClassPath + TEXT(" ") + ActorPath;

	if (TMImpulsePathContainsAny(CombinedPath, {
		TEXT("GrenadeLauncher"),
		TEXT("Grenade_launcher"),
		TEXT("Launcher"),
		TEXT("CameraShake"),
		TEXT("NiagaraActor"),
		TEXT("Emitter"),
		TEXT("Audio"),
		TEXT("Sound"),
		TEXT("Cue"),
		TEXT("Widget"),
		TEXT("Image"),
		TEXT("Material"),
		TEXT("Texture"),
		TEXT("Skeleton"),
		TEXT("AnimBP"),
		TEXT("GrenadeContent"),
		TEXT("Map/")
	}))
	{
		return false;
	}

	return TMImpulsePathContainsAny(CombinedPath, {
		TEXT("/GrenadePack/Blueprints/BP_Grenade"),
		TEXT("/GrenadePack/Blueprints/BP_LTGrenade"),
		TEXT("/MP_System_V3/Game/Weapons/Explosives/Frag/BP_Frag"),
		TEXT("/MP_System_V3/Game/Blueprints/Core/BP_Throwable"),
		TEXT("BP_Grenade_Niagara"),
		TEXT("BP_LTGrenade_Niagara"),
		TEXT("BP_PyroGrenade"),
		TEXT("BP_Grenade_Advanced"),
		TEXT("BP_Grenade_Simple"),
		TEXT("BP_Frag"),
		TEXT("BP_Throwable"),
		TEXT("Grenade_Thrown"),
		TEXT("Grenade_launched"),
		TEXT("ThrownGrenade"),
		TEXT("ThrowableGrenade"),
		TEXT("FragGrenade")
	});
}

bool UTMFoliageImpulseSubsystem::ShouldAffectFoliageComponent(
	const UPrimitiveComponent* Component,
	const FVector& Origin,
	const float Radius)
{
	if (!Component
		|| !Component->GetWorld()
		|| !Component->IsRegistered()
		|| !Component->IsVisible()
		|| Component->bHiddenInGame
		|| !TMImpulseComponentBoundsIntersectSphere(Component, Origin, Radius))
	{
		return false;
	}

	if (const UInstancedStaticMeshComponent* InstancedStaticMeshComponent = Cast<UInstancedStaticMeshComponent>(Component))
	{
		if (InstancedStaticMeshComponent->GetInstanceCount() <= 0)
		{
			return false;
		}
	}

	if (TMImpulseShouldAffectStaticMesh(TMImpulseGetStaticMeshFromPrimitive(Component)))
	{
		return true;
	}

	const FString ComponentPath = Component->GetPathName();
	return TMImpulsePathContainsAny(ComponentPath, {
		TEXT("Bush"),
		TEXT("Plant"),
		TEXT("Shrub"),
		TEXT("Sapling"),
		TEXT("Seedling"),
		TEXT("Branch"),
		TEXT("Twig"),
		TEXT("Grass"),
		TEXT("Fern"),
		TEXT("Foliage"),
		TEXT("Cover")
	});
}

bool UTMFoliageImpulseSubsystem::PrepareFoliageCollision(UPrimitiveComponent* Component)
{
	if (!Component)
	{
		return false;
	}

	const bool bHadCollision = Component->GetCollisionEnabled() != ECollisionEnabled::NoCollision;
	const bool bHasMeshCollision = TMImpulseEnsureSimpleCollisionOnStaticMesh(TMImpulseGetStaticMeshFromPrimitive(Component));
	TMImpulsePrepareFoliageWPO(Component);
	Component->SetCollisionObjectType(TMFoliageImpulseVegetationCollisionChannel);
	if (!bHadCollision)
	{
		Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Component->SetCollisionResponseToAllChannels(ECR_Ignore);
	}

	Component->SetCollisionResponseToChannel(TMFoliageImpulseVegetationCollisionChannel, ECR_Block);
	Component->SetGenerateOverlapEvents(true);
	if (bHasMeshCollision)
	{
		Component->RecreatePhysicsState();
	}
	Component->UpdateOverlaps();

	if (!bHadCollision && IsDebugLoggingEnabled())
	{
		UE_LOG(LogTMFoliageImpulse, Log, TEXT("Enabled query collision for foliage component %s."), *Component->GetPathName());
	}

	return true;
}
