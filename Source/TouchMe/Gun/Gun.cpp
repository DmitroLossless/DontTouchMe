// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gun.h"

#include "FakeGunAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Particles/ParticleSystem.h"
#include "../Projectile/ProjectileImpactData.h"

const FName AGun::MainSkeletalMeshComponentName(TEXT("Item"));

AGun::AGun()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	FakeSkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FakeSkeletalMeshComponent"));
	FakeSkeletalMeshComponent->SetupAttachment(SceneRoot);
	FakeSkeletalMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FakeSkeletalMeshComponent->SetGenerateOverlapEvents(false);
	FakeSkeletalMeshComponent->SetVisibility(false);
}

void AGun::BeginPlay()
{
	Super::BeginPlay();
	ApplyFakeMode();
}

void AGun::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bFakeModeApplied)
	{
		RestoreFromFakeMode();
	}

	Super::EndPlay(EndPlayReason);
}

void AGun::SetFakeMode(const bool bEnabled)
{
	bFakeMode = bEnabled;
	ApplyFakeMode();
}

UFakeGunAnimInstance* AGun::GetFakeAnimInstance() const
{
	return FakeSkeletalMeshComponent
		? Cast<UFakeGunAnimInstance>(FakeSkeletalMeshComponent->GetAnimInstance())
		: nullptr;
}

void AGun::ApplyFakeMode()
{
	USkeletalMeshComponent* MainMesh = ResolveMainSkeletalMesh();
	if (!MainMesh)
	{
		RestoreFromFakeMode();
		return;
	}

	const bool bCanApplyFakeMode = bFakeMode
		&& IsValid(FakeSkeletalMesh)
		&& FakeAnimInstanceClass != nullptr;

	if (bCanApplyFakeMode)
	{
		if (!bFakeModeApplied)
		{
			bMainMeshWasVisible = MainMesh->IsVisible();
			MainMeshPreviousAnimTickOption = static_cast<uint8>(MainMesh->VisibilityBasedAnimTickOption);
		}

		MainMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		MainMesh->SetVisibility(false, false);

		FakeSkeletalMeshComponent->AttachToComponent(
			MainMesh,
			FAttachmentTransformRules::SnapToTargetIncludingScale);
		FakeSkeletalMeshComponent->SetRelativeTransform(FakeSkeletalMeshOffset);
		FakeSkeletalMeshComponent->SetSkeletalMeshAsset(FakeSkeletalMesh);
		FakeSkeletalMeshComponent->SetAnimInstanceClass(FakeAnimInstanceClass);
		FakeSkeletalMeshComponent->SetVisibility(true, false);

		bFakeModeApplied = true;
		return;
	}

	RestoreFromFakeMode();
}

void AGun::RestoreFromFakeMode()
{
	FakeSkeletalMeshComponent->SetVisibility(false, false);
	FakeSkeletalMeshComponent->SetAnimInstanceClass(nullptr);
	FakeSkeletalMeshComponent->SetSkeletalMeshAsset(nullptr);
	FakeSkeletalMeshComponent->AttachToComponent(
		SceneRoot,
		FAttachmentTransformRules::SnapToTargetIncludingScale);

	if (bFakeModeApplied)
	{
		if (USkeletalMeshComponent* MainMesh = ResolveMainSkeletalMesh())
		{
			MainMesh->VisibilityBasedAnimTickOption =
				static_cast<EVisibilityBasedAnimTickOption>(MainMeshPreviousAnimTickOption);
			MainMesh->SetVisibility(bMainMeshWasVisible, false);
		}

		bFakeModeApplied = false;
	}
}

USkeletalMeshComponent* AGun::ResolveMainSkeletalMesh() const
{
	TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshes(this);
	for (USkeletalMeshComponent* SkeletalMesh : SkeletalMeshes)
	{
		if (SkeletalMesh && SkeletalMesh->GetFName() == MainSkeletalMeshComponentName)
		{
			return SkeletalMesh;
		}
	}

	return nullptr;
}

void AGun::Impact(
	const FVector Location,
	const FVector Normal,
	const UPhysicalMaterial* PhysicalMaterial)
{
	if (!Caliber)
	{
		return;
	}

	const FProjectileImpactEffects Effects = Caliber->GetEffectsForPhysicalMaterial(PhysicalMaterial);
	const FVector ImpactNormal = Normal.IsNearlyZero() ? FVector::UpVector : Normal.GetSafeNormal();
	const FTransform ImpactTransform(ImpactNormal.Rotation(), Location);

	if (Effects.Particle)
	{
		const FTransform ParticleTransform = Effects.ParticleTransformOffset * ImpactTransform;

		if (UParticleSystem* CascadeSystem = Cast<UParticleSystem>(Effects.Particle))
		{
			UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), CascadeSystem, ParticleTransform);
		}
		else if (UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(Effects.Particle))
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				this,
				NiagaraSystem,
				ParticleTransform.GetLocation(),
				ParticleTransform.Rotator(),
				ParticleTransform.GetScale3D());
		}
	}

	if (Effects.Sound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Effects.Sound, Location, ImpactTransform.Rotator());
	}

	if (Effects.Decal)
	{
		const FTransform DecalTransform = Effects.DecalTransformOffset * ImpactTransform;
		UGameplayStatics::SpawnDecalAtLocation(
			this,
			Effects.Decal,
			DecalTransform.GetScale3D().GetAbs(),
			DecalTransform.GetLocation(),
			DecalTransform.Rotator());
	}
}
