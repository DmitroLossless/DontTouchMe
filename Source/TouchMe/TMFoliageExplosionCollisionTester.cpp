#include "TMFoliageExplosionCollisionTester.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/UObjectIterator.h"

#include <initializer_list>

namespace
{
constexpr ECollisionChannel TMExplosionVegetationCollisionChannel = ECC_GameTraceChannel6;

static const FName MWPlayerPositionParameter(TEXT("MW_PlayerPosition"));
static const FName MWPlayerSpeedParameter(TEXT("MW_PlayerSpeed"));
static const FName MWBendPositionParameter(TEXT("MW_BendPos"));
static const FName TMFoliageImpulseOriginXParameter(TEXT("TM_FoliageImpulseOriginX"));
static const FName TMFoliageImpulseOriginYParameter(TEXT("TM_FoliageImpulseOriginY"));
static const FName TMFoliageImpulseOriginZParameter(TEXT("TM_FoliageImpulseOriginZ"));

bool TMExplosionPathContainsAny(const FString& Path, std::initializer_list<const TCHAR*> Tokens)
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

bool TMExplosionShouldAffectStaticMesh(const UStaticMesh* StaticMesh)
{
	if (!StaticMesh)
	{
		return false;
	}

	const FString Path = StaticMesh->GetPathName();
	if (TMExplosionPathContainsAny(Path, {
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

	return TMExplosionPathContainsAny(Path, {
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

UStaticMesh* TMExplosionGetStaticMeshFromPrimitive(const UPrimitiveComponent* Component)
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

bool TMExplosionEnsureSimpleCollisionOnStaticMesh(UStaticMesh* StaticMesh)
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

void TMExplosionPrepareFoliageWPO(UPrimitiveComponent* Component)
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
	}

	Component->MarkRenderStateDirty();
}
}

ATMFoliageExplosionCollisionTester::ATMFoliageExplosionCollisionTester()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bIsEditorOnlyActor = false;

	SphereComponent = CreateDefaultSubobject<USphereComponent>(TEXT("ExplosionCollisionSphere"));
	RootComponent = SphereComponent;
	ConfigureSphere();
}

void ATMFoliageExplosionCollisionTester::BeginPlay()
{
	Super::BeginPlay();
	ElapsedSeconds = 0.f;
	ConfigureSphere();
	RefreshAffectedFoliage();
	HoldMaterialBend();
}

void ATMFoliageExplosionCollisionTester::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ConfigureSphere();
}

#if WITH_EDITOR
bool ATMFoliageExplosionCollisionTester::ShouldTickIfViewportsOnly() const
{
	return bExpandInEditor;
}
#endif

void ATMFoliageExplosionCollisionTester::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ElapsedSeconds += FMath::Max(0.f, DeltaSeconds);
	ConfigureSphere();
	RefreshAffectedFoliage();
	HoldMaterialBend();

	if (bDrawDebugSphere && GetWorld())
	{
		DrawDebugSphere(GetWorld(), GetActorLocation(), GetCurrentSphereRadius(), 32, DebugColor, false, 0.f, 0, 3.f);
	}

	if (bAutoDestroyAfterExpansion
		&& ExpansionDuration > KINDA_SMALL_NUMBER
		&& ElapsedSeconds >= ExpansionDuration)
	{
		Destroy();
	}
}

void ATMFoliageExplosionCollisionTester::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearMaterialBend();
	Super::EndPlay(EndPlayReason);
}

void ATMFoliageExplosionCollisionTester::RefreshAffectedFoliage()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ConfigureSphere();

	const FVector Origin = GetActorLocation();
	for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
	{
		UPrimitiveComponent* Component = *It;
		if (!Component || Component->GetWorld() != World || !ShouldAffectFoliageComponent(Component, Origin, Radius))
		{
			continue;
		}

		PrepareFoliageCollision(Component);
	}

	if (SphereComponent)
	{
		SphereComponent->UpdateOverlaps();
	}
}

void ATMFoliageExplosionCollisionTester::ConfigureSphere()
{
	if (!SphereComponent)
	{
		return;
	}

	const float CurrentRadius = GetCurrentSphereRadius();
	SphereComponent->SetMobility(EComponentMobility::Movable);
	SphereComponent->InitSphereRadius(CurrentRadius);
	SphereComponent->SetSphereRadius(CurrentRadius, false);
	SphereComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SphereComponent->SetCollisionObjectType(TMExplosionVegetationCollisionChannel);
	SphereComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	SphereComponent->SetCollisionResponseToChannel(TMExplosionVegetationCollisionChannel, ECR_Block);
	SphereComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	SphereComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	SphereComponent->SetCollisionResponseToChannel(ECC_GameTraceChannel3, ECR_Ignore);
	SphereComponent->SetCollisionResponseToChannel(ECC_GameTraceChannel4, ECR_Ignore);
	SphereComponent->SetGenerateOverlapEvents(true);
	SphereComponent->SetCanEverAffectNavigation(false);
	SphereComponent->SetHiddenInGame(true);
}

float ATMFoliageExplosionCollisionTester::GetCurrentSphereRadius() const
{
	const float MaxRadius = FMath::Max(1.f, Radius);
	const UWorld* World = GetWorld();
	if (!World || (World->WorldType == EWorldType::Editor && !bExpandInEditor) || ExpansionDuration <= KINDA_SMALL_NUMBER)
	{
		return MaxRadius;
	}

	const float Divisor = FMath::Max(1.f, InitialRadiusDivisor);
	const float InitialRadius = FMath::Max(1.f, MaxRadius / Divisor);
	const float Alpha = FMath::Clamp(ElapsedSeconds / ExpansionDuration, 0.f, 1.f);
	return FMath::Lerp(InitialRadius, MaxRadius, Alpha);
}

void ATMFoliageExplosionCollisionTester::HoldMaterialBend()
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

	FVector Direction = PullDirection;
	Direction.Z = 0.f;
	if (!Direction.Normalize())
	{
		Direction = FVector::ForwardVector;
	}

	const FVector Origin = GetActorLocation();
	const FVector BendPosition = Origin + Direction * FMath::Max(0.f, BendDistance);
	const float Speed = FMath::Max(0.f, Strength * MaterialSpeedScale);

	CollectionInstance->SetVectorParameterValue(MWPlayerPositionParameter, FLinearColor(Origin.X, Origin.Y, Origin.Z, 1.f));
	CollectionInstance->SetVectorParameterValue(MWBendPositionParameter, FLinearColor(BendPosition.X, BendPosition.Y, BendPosition.Z, 1.f));
	CollectionInstance->SetScalarParameterValue(MWPlayerSpeedParameter, Speed);
	CollectionInstance->SetScalarParameterValue(TMFoliageImpulseOriginXParameter, Origin.X);
	CollectionInstance->SetScalarParameterValue(TMFoliageImpulseOriginYParameter, Origin.Y);
	CollectionInstance->SetScalarParameterValue(TMFoliageImpulseOriginZParameter, Origin.Z);
}

void ATMFoliageExplosionCollisionTester::ClearMaterialBend()
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

	CollectionInstance->SetScalarParameterValue(MWPlayerSpeedParameter, 0.f);
}

UMaterialParameterCollection* ATMFoliageExplosionCollisionTester::GetMWControllerCollection()
{
	if (MWControllerCollection)
	{
		return MWControllerCollection;
	}

	UMaterialParameterCollection* Collection = LoadObject<UMaterialParameterCollection>(
		nullptr,
		TEXT("/Game/MWCommon/_Legacy2/Materials/MASTER/Par/MPC_MW_Controller.MPC_MW_Controller"));
	MWControllerCollection = Collection;
	return Collection;
}

bool ATMFoliageExplosionCollisionTester::ShouldAffectFoliageComponent(
	const UPrimitiveComponent* Component,
	const FVector& Origin,
	const float Radius)
{
	if (!Component
		|| !Component->GetWorld()
		|| !Component->IsRegistered()
		|| !Component->IsVisible()
		|| Component->bHiddenInGame
		|| Component->Bounds.GetBox().ComputeSquaredDistanceToPoint(Origin) > FMath::Square(Radius))
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

	if (TMExplosionShouldAffectStaticMesh(TMExplosionGetStaticMeshFromPrimitive(Component)))
	{
		return true;
	}

	const FString ComponentPath = Component->GetPathName();
	return TMExplosionPathContainsAny(ComponentPath, {
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

bool ATMFoliageExplosionCollisionTester::PrepareFoliageCollision(UPrimitiveComponent* Component)
{
	if (!Component)
	{
		return false;
	}

	const bool bHadCollision = Component->GetCollisionEnabled() != ECollisionEnabled::NoCollision;
	const bool bHasMeshCollision = TMExplosionEnsureSimpleCollisionOnStaticMesh(TMExplosionGetStaticMeshFromPrimitive(Component));
	TMExplosionPrepareFoliageWPO(Component);
	Component->SetCollisionObjectType(TMExplosionVegetationCollisionChannel);
	if (!bHadCollision)
	{
		Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Component->SetCollisionResponseToAllChannels(ECR_Ignore);
	}

	Component->SetCollisionResponseToChannel(TMExplosionVegetationCollisionChannel, ECR_Block);
	Component->SetGenerateOverlapEvents(true);
	if (bHasMeshCollision)
	{
		Component->RecreatePhysicsState();
	}
	Component->UpdateOverlaps();
	return true;
}
