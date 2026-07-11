#include "TMFoliageCollisionPushTester.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Animation/SkeletalMeshActor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectIterator.h"

#include <initializer_list>

DEFINE_LOG_CATEGORY_STATIC(LogTMFoliageCollisionPushTester, Log, All);

namespace
{
constexpr ECollisionChannel TMVegetationCollisionChannel = ECC_GameTraceChannel6;

struct FFoliageSkeletalReplacement
{
	const TCHAR* StaticMeshPath;
	const TCHAR* SkeletalMeshPath;
};

static const FFoliageSkeletalReplacement GFoliageSkeletalReplacements[] = {
	{
		TEXT("/Game/MWConiferForest/Meshes/Trees/Fir/SM_CF_TreeFirSaplings_A.SM_CF_TreeFirSaplings_A"),
		TEXT("/Game/TouchMe/FoliagePhysics/SK_CF_TreeFirSaplings_A_Physics.SK_CF_TreeFirSaplings_A_Physics")
	},
	{
		TEXT("/Game/MWConiferForest/Meshes/Trees/Fir/SM_CF_TreeFirSaplings_B.SM_CF_TreeFirSaplings_B"),
		TEXT("/Game/TouchMe/FoliagePhysics/SK_CF_TreeFirSaplings_B_Physics.SK_CF_TreeFirSaplings_B_Physics")
	},
	{
		TEXT("/Game/MWConiferForest/Meshes/Trees/Fir/SM_CF_TreeFirSaplings_C.SM_CF_TreeFirSaplings_C"),
		TEXT("/Game/TouchMe/FoliagePhysics/SK_CF_TreeFirSaplings_C_Physics.SK_CF_TreeFirSaplings_C_Physics")
	},
	{
		TEXT("/Game/MWConiferForest/Meshes/Cover/SM_CF_CoverBranchesA.SM_CF_CoverBranchesA"),
		TEXT("/Game/TouchMe/FoliagePhysics/SK_CF_CoverBranchesA_Physics.SK_CF_CoverBranchesA_Physics")
	},
	{
		TEXT("/Game/MWConiferForest/Meshes/Cover/SM_CF_CoverBranchesB.SM_CF_CoverBranchesB"),
		TEXT("/Game/TouchMe/FoliagePhysics/SK_CF_CoverBranchesB_Physics.SK_CF_CoverBranchesB_Physics")
	},
	{
		TEXT("/Game/MWConiferForest/Meshes/Cover/SM_CF_CoverBranchesC.SM_CF_CoverBranchesC"),
		TEXT("/Game/TouchMe/FoliagePhysics/SK_CF_CoverBranchesC_Physics.SK_CF_CoverBranchesC_Physics")
	},
	{
		TEXT("/Game/MWConiferForest/Meshes/Plants/SM_CF_PlantsGroundA1.SM_CF_PlantsGroundA1"),
		TEXT("/Game/TouchMe/FoliagePhysics/SK_CF_PlantsGroundA1_Physics.SK_CF_PlantsGroundA1_Physics")
	},
	{
		TEXT("/Game/MWConiferForest/Meshes/Plants/SM_CF_PlantsGroundA2.SM_CF_PlantsGroundA2"),
		TEXT("/Game/TouchMe/FoliagePhysics/SK_CF_PlantsGroundA2_Physics.SK_CF_PlantsGroundA2_Physics")
	},
	{
		TEXT("/Game/MWConiferForest/Meshes/Plants/SM_CF_PlantsGroundA3.SM_CF_PlantsGroundA3"),
		TEXT("/Game/TouchMe/FoliagePhysics/SK_CF_PlantsGroundA3_Physics.SK_CF_PlantsGroundA3_Physics")
	},
	{
		TEXT("/Game/MWConiferForest/Meshes/Plants/SM_CF_PlantFernA1.SM_CF_PlantFernA1"),
		TEXT("/Game/TouchMe/FoliagePhysics/SK_CF_PlantFernA1_Physics.SK_CF_PlantFernA1_Physics")
	},
};

bool PathContainsAny(const FString& Path, std::initializer_list<const TCHAR*> Tokens)
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

bool ShouldAffectStaticMesh(const UStaticMesh* StaticMesh)
{
	if (!StaticMesh)
	{
		return false;
	}

	const FString Path = StaticMesh->GetPathName();
	if (PathContainsAny(Path, {
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

	return PathContainsAny(Path, {
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
		TEXT("MWConiferForest/Meshes/Plants"),
		TEXT("MWConiferForest/Meshes/Cover"),
		TEXT("MWConiferForest/Meshes/Trees/Fir/SM_CF_TreeFirSaplings"),
		TEXT("MWConiferForest/Meshes/Trees/Fir/SM_CF_TreeFirSeedlings")
	});
}

UStaticMesh* GetStaticMeshFromPrimitive(const UPrimitiveComponent* Component)
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

bool ComponentBoundsIntersectSphere(const UPrimitiveComponent* Component, const FVector& Origin, const float Radius)
{
	return Component && Component->Bounds.GetBox().ComputeSquaredDistanceToPoint(Origin) <= FMath::Square(Radius);
}
}

ATMFoliageCollisionPushTester::ATMFoliageCollisionPushTester()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bIsEditorOnlyActor = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	PushSphere = CreateDefaultSubobject<USphereComponent>(TEXT("VegetationPushSphere"));
	PushSphere->SetupAttachment(SceneRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMeshFinder.Succeeded())
	{
		ProxySphereMesh = SphereMeshFinder.Object;
	}

	SkeletalReplacementImpactFX.Add(TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/BallisticsVFX/Particles/Impacts/LegacyFX/Small-Medium-Large/Vegetation/NS_Vegetation_impact_large.NS_Vegetation_impact_large"))));
	SkeletalReplacementImpactFX.Add(TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/BallisticsVFX/Particles/Impacts/LegacyFX/Small-Medium-Large/Vegetation/NS_Vegetation_impact_med.NS_Vegetation_impact_med"))));
	SkeletalReplacementImpactFX.Add(TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/BallisticsVFX/Particles/Impacts/LegacyFX/Small-Medium-Large/Vegetation/NS_Vegetation_impact_small.NS_Vegetation_impact_small"))));

	ConfigurePusherSphere();
}

void ATMFoliageCollisionPushTester::BeginPlay()
{
	Super::BeginPlay();

	ElapsedSeconds = 0.f;
	RespawnCooldownSeconds = 0.f;
	bPulseActive = true;
	ConfigurePusherSphere();
	if (bUseSkeletalReplacements)
	{
		CreateSkeletalReplacements();
	}

	if (bCreatePhysicsProxyBodies)
	{
		RebuildProxyBodies();
	}
}

void ATMFoliageCollisionPushTester::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ConfigurePusherSphere();
}

void ATMFoliageCollisionPushTester::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float ClampedDeltaSeconds = FMath::Max(0.f, DeltaSeconds);
	if (bLoopPulses)
	{
		const float CycleDuration = FMath::Max(KINDA_SMALL_NUMBER, FMath::Max(0.f, ExpansionDuration) + FMath::Max(0.f, RespawnDelay));
		ElapsedSeconds = FMath::Fmod(ElapsedSeconds + ClampedDeltaSeconds, CycleDuration);
		bPulseActive = ExpansionDuration <= KINDA_SMALL_NUMBER || ElapsedSeconds <= ExpansionDuration;
		ConfigurePusherSphere();

		if (bPulseActive && bDrawDebug && GetWorld())
		{
			DrawDebugSphere(GetWorld(), GetActorLocation(), GetCurrentSphereRadius(), 32, FColor::Cyan, false, 0.f, 0, 2.5f);
		}

		return;
	}

	ElapsedSeconds += ClampedDeltaSeconds;
	ConfigurePusherSphere();

	if (bPulseActive && bDrawDebug && GetWorld())
	{
		DrawDebugSphere(GetWorld(), GetActorLocation(), GetCurrentSphereRadius(), 32, FColor::Cyan, false, 0.f, 0, 2.5f);
		for (const UStaticMeshComponent* ProxyBody : ProxyBodies)
		{
			if (ProxyBody)
			{
				DrawDebugSphere(GetWorld(), ProxyBody->GetComponentLocation(), ProxyBodyRadius, 12, FColor::Green, false, 0.f, 0, 1.5f);
			}
		}
	}

	if (ExpansionDuration > KINDA_SMALL_NUMBER
		&& ElapsedSeconds >= ExpansionDuration)
	{
		if (bAutoDestroyAfterExpansion)
		{
			Destroy();
		}
	}
}

void ATMFoliageCollisionPushTester::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyProxyBodies();
	Super::EndPlay(EndPlayReason);
}

void ATMFoliageCollisionPushTester::ConfigurePusherSphere()
{
	if (!PushSphere)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (bLoopPulses && World && World->WorldType != EWorldType::Editor && !bPulseActive)
	{
		PushSphere->SetMobility(EComponentMobility::Movable);
		PushSphere->SetRelativeLocation(FVector::ZeroVector);
		PushSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PushSphere->SetGenerateOverlapEvents(false);
		PushSphere->SetCanEverAffectNavigation(false);
		PushSphere->SetHiddenInGame(true);
		return;
	}

	const float CurrentRadius = GetCurrentSphereRadius();
	PushSphere->SetMobility(EComponentMobility::Movable);
	PushSphere->SetRelativeLocation(FVector::ZeroVector);
	PushSphere->InitSphereRadius(CurrentRadius);
	PushSphere->SetSphereRadius(CurrentRadius, true);
	PushSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PushSphere->SetCollisionObjectType(TMVegetationCollisionChannel);
	PushSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	PushSphere->SetCollisionResponseToChannel(TMVegetationCollisionChannel, ECR_Block);
	PushSphere->SetGenerateOverlapEvents(true);
	PushSphere->SetCanEverAffectNavigation(false);
	PushSphere->SetHiddenInGame(true);
}

void ATMFoliageCollisionPushTester::ConfigureVegetationPhysicsBody(UPrimitiveComponent* Component) const
{
	if (!Component)
	{
		return;
	}

	Component->SetMobility(EComponentMobility::Movable);
	Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Component->SetCollisionObjectType(TMVegetationCollisionChannel);
	Component->SetCollisionResponseToAllChannels(ECR_Ignore);
	Component->SetCollisionResponseToChannel(TMVegetationCollisionChannel, ECR_Block);
	Component->SetGenerateOverlapEvents(true);
	Component->SetCanEverAffectNavigation(false);
}

void ATMFoliageCollisionPushTester::RebuildProxyBodies()
{
	DestroyProxyBodies();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector Origin = GetActorLocation();
	for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
	{
		UPrimitiveComponent* Component = *It;
		if (!Component
			|| Component->GetWorld() != World
			|| Component == PushSphere
			|| ProxyBodies.Num() >= MaxProxyBodies
			|| !Component->IsRegistered()
			|| !Component->IsVisible()
			|| Component->bHiddenInGame
			|| !ComponentBoundsIntersectSphere(Component, Origin, Radius)
			|| !ShouldAffectStaticMesh(GetStaticMeshFromPrimitive(Component)))
		{
			continue;
		}

		CollectProxyBodiesForComponent(Component, Origin);
		if (ProxyBodies.Num() >= MaxProxyBodies)
		{
			break;
		}
	}
}

void ATMFoliageCollisionPushTester::CreateSkeletalReplacements()
{
	UWorld* World = GetWorld();
	if (!World || MaxSkeletalReplacements <= 0)
	{
		return;
	}

	const FVector Origin = GetActorLocation();
	for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
	{
		UPrimitiveComponent* Component = *It;
		if (!Component
			|| Component->GetWorld() != World
			|| Component == PushSphere
			|| SkeletalReplacementActors.Num() >= MaxSkeletalReplacements
			|| !Component->IsRegistered()
			|| !Component->IsVisible()
			|| Component->bHiddenInGame
			|| !ComponentBoundsIntersectSphere(Component, Origin, Radius))
		{
			continue;
		}

		const UStaticMesh* StaticMesh = GetStaticMeshFromPrimitive(Component);
		if (!StaticMesh || !FindSkeletalReplacementMesh(StaticMesh))
		{
			continue;
		}

		if (UInstancedStaticMeshComponent* InstancedStaticMeshComponent = Cast<UInstancedStaticMeshComponent>(Component))
		{
			TArray<int32> ReplacedInstanceIndices;
			FTransform InstanceTransform;
			const int32 InstanceCount = InstancedStaticMeshComponent->GetInstanceCount();
			for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount && SkeletalReplacementActors.Num() < MaxSkeletalReplacements; ++InstanceIndex)
			{
				if (!InstancedStaticMeshComponent->GetInstanceTransform(InstanceIndex, InstanceTransform, true))
				{
					continue;
				}

				if (FVector::DistSquared2D(InstanceTransform.GetLocation(), Origin) > FMath::Square(Radius))
				{
					continue;
				}

				if (CreateSkeletalReplacement(InstanceTransform, StaticMesh))
				{
					ReplacedInstanceIndices.Add(InstanceIndex);
				}
			}

			if (bRemoveOriginalInstances)
			{
				ReplacedInstanceIndices.Sort(TGreater<int32>());
				for (const int32 InstanceIndex : ReplacedInstanceIndices)
				{
					InstancedStaticMeshComponent->RemoveInstance(InstanceIndex);
				}
			}

			continue;
		}

		if (CreateSkeletalReplacement(Component->GetComponentTransform(), StaticMesh) && bRemoveOriginalInstances)
		{
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetHiddenInGame(true);
			Component->SetVisibility(false, true);
		}
	}
}

bool ATMFoliageCollisionPushTester::CreateSkeletalReplacement(const FTransform& WorldTransform, const UStaticMesh* SourceStaticMesh)
{
	UWorld* World = GetWorld();
	USkeletalMesh* SkeletalMesh = FindSkeletalReplacementMesh(SourceStaticMesh);
	if (!World || !SkeletalMesh)
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.Name = MakeUniqueObjectName(World, ASkeletalMeshActor::StaticClass(), TEXT("TM_FoliageSkeletalReplacement"));

	ASkeletalMeshActor* ReplacementActor = World->SpawnActor<ASkeletalMeshActor>(
		ASkeletalMeshActor::StaticClass(),
		WorldTransform,
		SpawnParameters);
	if (!ReplacementActor)
	{
		return false;
	}

#if WITH_EDITOR
	ReplacementActor->SetActorLabel(TEXT("TM_FoliageSkeletalReplacement"));
#endif
	USkeletalMeshComponent* SkeletalMeshComponent = ReplacementActor->GetSkeletalMeshComponent();
	if (!SkeletalMeshComponent)
	{
		ReplacementActor->Destroy();
		return false;
	}

	SkeletalMeshComponent->SetSkeletalMeshAsset(SkeletalMesh);
	SkeletalMeshComponent->SetMobility(EComponentMobility::Movable);
	SkeletalMeshComponent->SetComponentTickEnabled(true);
	SkeletalMeshComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	SkeletalMeshComponent->bPauseAnims = false;
	SkeletalMeshComponent->bNoSkeletonUpdate = false;
	SkeletalMeshComponent->SetUpdateAnimationInEditor(true);
	SkeletalMeshComponent->SetEnablePhysicsBlending(true);
	SkeletalMeshComponent->SetUpdateKinematicFromSimulation(false);
	ConfigureVegetationPhysicsBody(SkeletalMeshComponent);
	SkeletalMeshComponent->SetCanEverAffectNavigation(false);
	SkeletalMeshComponent->SetEnableGravity(false);
	SkeletalMeshComponent->RecreatePhysicsState();
	SkeletalMeshComponent->SetSimulatePhysics(false);
	SkeletalMeshComponent->SetBodySimulatePhysics(TEXT("foliage_00"), false);
	SkeletalMeshComponent->SetAllBodiesBelowSimulatePhysics(SkeletalSimulationRootBone, true, true);
	SkeletalMeshComponent->SetAllBodiesBelowPhysicsBlendWeight(SkeletalSimulationRootBone, 1.f, false, true);
	SkeletalMeshComponent->WakeAllRigidBodies();

	SkeletalReplacementActors.Add(ReplacementActor);
	SpawnRandomSkeletalReplacementImpactFX(WorldTransform);
	return true;
}

void ATMFoliageCollisionPushTester::SpawnRandomSkeletalReplacementImpactFX(const FTransform& WorldTransform)
{
	UWorld* World = GetWorld();
	if (!bSpawnSkeletalReplacementImpactFX
		|| !World
		|| World->GetNetMode() == NM_DedicatedServer
		|| SkeletalReplacementImpactFX.IsEmpty())
	{
		if (bSpawnSkeletalReplacementImpactFX && World && SkeletalReplacementImpactFX.IsEmpty())
		{
			UE_LOG(
				LogTMFoliageCollisionPushTester,
				Warning,
				TEXT("Skipped foliage skeletal replacement impact FX at %s: no Niagara systems configured."),
				*WorldTransform.GetLocation().ToCompactString());
		}
		return;
	}

	TArray<UNiagaraSystem*> LoadedSystems;
	LoadedSystems.Reserve(SkeletalReplacementImpactFX.Num());
	for (const TSoftObjectPtr<UNiagaraSystem>& ImpactFX : SkeletalReplacementImpactFX)
	{
		if (UNiagaraSystem* NiagaraSystem = ImpactFX.LoadSynchronous())
		{
			LoadedSystems.Add(NiagaraSystem);
		}
	}

	if (LoadedSystems.IsEmpty())
	{
		UE_LOG(
			LogTMFoliageCollisionPushTester,
			Warning,
			TEXT("Skipped foliage skeletal replacement impact FX at %s: configured Niagara systems failed to load."),
			*WorldTransform.GetLocation().ToCompactString());
		return;
	}

	UNiagaraSystem* NiagaraSystem = LoadedSystems[FMath::RandHelper(LoadedSystems.Num())];
	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		this,
		NiagaraSystem,
		WorldTransform.GetLocation(),
		WorldTransform.GetRotation().Rotator(),
		SkeletalReplacementImpactFXScale,
		true,
		true,
		ENCPoolMethod::AutoRelease);

	UE_LOG(
		LogTMFoliageCollisionPushTester,
		Log,
		TEXT("Spawned foliage skeletal replacement impact FX %s at %s."),
		*NiagaraSystem->GetPathName(),
		*WorldTransform.GetLocation().ToCompactString());
}

void ATMFoliageCollisionPushTester::CollectProxyBodiesForComponent(UPrimitiveComponent* Component, const FVector& Origin)
{
	if (!Component || ProxyBodies.Num() >= MaxProxyBodies)
	{
		return;
	}

	if (UInstancedStaticMeshComponent* InstancedStaticMeshComponent = Cast<UInstancedStaticMeshComponent>(Component))
	{
		FTransform InstanceTransform;
		const int32 InstanceCount = InstancedStaticMeshComponent->GetInstanceCount();
		for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount && ProxyBodies.Num() < MaxProxyBodies; ++InstanceIndex)
		{
			if (!InstancedStaticMeshComponent->GetInstanceTransform(InstanceIndex, InstanceTransform, true))
			{
				continue;
			}

			const FVector InstanceLocation = InstanceTransform.GetLocation();
			if (FVector::DistSquared2D(InstanceLocation, Origin) > FMath::Square(Radius))
			{
				continue;
			}

			const FBoxSphereBounds MeshBounds = InstancedStaticMeshComponent->GetStaticMesh()
				? InstancedStaticMeshComponent->GetStaticMesh()->GetBounds()
				: FBoxSphereBounds(EForceInit::ForceInit);
			const float BodyHeight = FMath::Clamp(MeshBounds.BoxExtent.Z * 0.45f, 35.f, 180.f);
			CreateProxyBody(InstanceLocation + FVector(0.f, 0.f, BodyHeight));
		}
		return;
	}

	CreateProxyBody(Component->Bounds.Origin);
}

void ATMFoliageCollisionPushTester::CreateProxyBody(const FVector& Location)
{
	if (!ProxySphereMesh || ProxyBodies.Num() >= MaxProxyBodies)
	{
		return;
	}

	UStaticMeshComponent* ProxyBody = NewObject<UStaticMeshComponent>(this);
	if (!ProxyBody)
	{
		return;
	}

	ProxyBody->SetStaticMesh(ProxySphereMesh);
	ProxyBody->SetWorldLocation(Location);
	const float SphereMeshRadius = 50.f;
	const float Scale = FMath::Max(1.f, ProxyBodyRadius) / SphereMeshRadius;
	ProxyBody->SetWorldScale3D(FVector(Scale));
	ProxyBody->SetHiddenInGame(!bShowProxyMeshes);
	ProxyBody->SetVisibility(bShowProxyMeshes, true);
	ProxyBody->ComponentTags.Add(FName(*FString::Printf(
		TEXT("TMInitialLocation=%.3f,%.3f,%.3f"),
		Location.X,
		Location.Y,
		Location.Z)));
	ConfigureVegetationPhysicsBody(ProxyBody);
	ProxyBody->RegisterComponent();
	ProxyBody->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepWorldTransform);
	AddInstanceComponent(ProxyBody);

	ProxyBody->SetSimulatePhysics(true);
	ProxyBody->SetEnableGravity(false);
	ProxyBody->SetLinearDamping(ProxyLinearDamping);
	ProxyBody->SetAngularDamping(ProxyAngularDamping);
	ProxyBody->SetMassOverrideInKg(NAME_None, FMath::Max(0.01f, ProxyBodyMassKg), true);
	ProxyBody->WakeAllRigidBodies();

	ProxyBodies.Add(ProxyBody);
}

void ATMFoliageCollisionPushTester::DestroyProxyBodies()
{
	for (UStaticMeshComponent* ProxyBody : ProxyBodies)
	{
		if (ProxyBody)
		{
			ProxyBody->SetSimulatePhysics(false);
			ProxyBody->DestroyComponent();
		}
	}
	ProxyBodies.Reset();
}

USkeletalMesh* ATMFoliageCollisionPushTester::FindSkeletalReplacementMesh(const UStaticMesh* StaticMesh) const
{
	if (!StaticMesh)
	{
		return nullptr;
	}

	const FString StaticMeshPath = StaticMesh->GetPathName();
	for (const FFoliageSkeletalReplacement& Replacement : GFoliageSkeletalReplacements)
	{
		if (StaticMeshPath.Equals(Replacement.StaticMeshPath, ESearchCase::IgnoreCase))
		{
			USkeletalMesh* SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, Replacement.SkeletalMeshPath);
			return SkeletalMesh && SkeletalMesh->GetPhysicsAsset() ? SkeletalMesh : nullptr;
		}
	}

	return nullptr;
}

float ATMFoliageCollisionPushTester::GetCurrentSphereRadius() const
{
	const float MaxRadius = FMath::Max(1.f, Radius);
	const UWorld* World = GetWorld();
	if (!World || World->WorldType == EWorldType::Editor || ExpansionDuration <= KINDA_SMALL_NUMBER)
	{
		return MaxRadius;
	}

	const float Divisor = FMath::Max(1.f, InitialRadiusDivisor);
	const float InitialRadius = FMath::Max(1.f, MaxRadius / Divisor);
	const float Alpha = FMath::Clamp(ElapsedSeconds / ExpansionDuration, 0.f, 1.f);
	return FMath::Lerp(InitialRadius, MaxRadius, Alpha);
}
