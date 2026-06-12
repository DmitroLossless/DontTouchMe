// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gun.h"

#include "FakeGunAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Particles/ParticleSystem.h"
#include "TimerManager.h"
#include "../Projectile/ProjectileImpactData.h"

const FName AGun::MainSkeletalMeshComponentName(TEXT("Item"));

namespace
{
	const TCHAR* WeaponAttachmentMeshPathToken = TEXT("/Weapons/Attachments/");

	FString CleanGeneratedWeaponClassName(FString ClassName)
	{
		ClassName.RemoveFromStart(TEXT("SKEL_"));
		ClassName.RemoveFromStart(TEXT("REINST_"));
		ClassName.RemoveFromEnd(TEXT("_C"));
		return ClassName;
	}
}

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

FText AGun::GetWeaponDisplayName() const
{
	if (HasCustomWeaponDisplayName())
	{
		return WeaponDisplayName;
	}

	return MakeDefaultWeaponDisplayName(GetClass());
}

FText AGun::GetWeaponDisplayNameFromClass(TSubclassOf<AGun> WeaponClass)
{
	if (!WeaponClass)
	{
		return FText::GetEmpty();
	}

	const AGun* DefaultWeapon = WeaponClass->GetDefaultObject<AGun>();
	return DefaultWeapon ? DefaultWeapon->GetWeaponDisplayName() : MakeDefaultWeaponDisplayName(WeaponClass.Get());
}

void AGun::PostLoad()
{
	Super::PostLoad();

	if (HasAnyFlags(RF_ClassDefaultObject) && !HasCustomWeaponDisplayName())
	{
		WeaponDisplayName = MakeDefaultWeaponDisplayName(GetClass());
	}
}

void AGun::BeginPlay()
{
	Super::BeginPlay();
	ApplyFakeMode();
	RequestDeferredAttachmentSanitize();
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
	RequestDeferredAttachmentSanitize();
}

UFakeGunAnimInstance* AGun::GetFakeAnimInstance() const
{
	return FakeSkeletalMeshComponent
		? Cast<UFakeGunAnimInstance>(FakeSkeletalMeshComponent->GetAnimInstance())
		: nullptr;
}

void AGun::ProcessEvent(UFunction* Function, void* Parameters)
{
	Super::ProcessEvent(Function, Parameters);

	if (ShouldRequestAttachmentSanitizeForFunction(Function))
	{
		RequestDeferredAttachmentSanitize();
	}
}

int32 AGun::SanitizeInvalidAttachmentComponents()
{
	if (bSanitizingAttachmentComponents || HasAnyFlags(RF_ClassDefaultObject))
	{
		return 0;
	}

	TGuardValue<bool> SanitizingGuard(bSanitizingAttachmentComponents, true);

	int32 DestroyedCount = 0;
	TInlineComponentArray<UStaticMeshComponent*> StaticMeshComponents(this);
	for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
	{
		if (!IsValid(StaticMeshComponent) || !IsInvalidWeaponAttachmentComponent(StaticMeshComponent))
		{
			continue;
		}

		const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
		const USceneComponent* AttachParent = StaticMeshComponent->GetAttachParent();

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Destroying invalid weapon attachment component '%s' on '%s'. Mesh='%s', Parent='%s', Socket='%s'."),
			*StaticMeshComponent->GetName(),
			*GetName(),
			StaticMesh ? *StaticMesh->GetPathName() : TEXT("None"),
			AttachParent ? *AttachParent->GetName() : TEXT("None"),
			*StaticMeshComponent->GetAttachSocketName().ToString());

		StaticMeshComponent->DestroyComponent();
		++DestroyedCount;
	}

	return DestroyedCount;
}

void AGun::RequestDeferredAttachmentSanitize()
{
	if (bAttachmentSanitizeRequested || bSanitizingAttachmentComponents || HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	bAttachmentSanitizeRequested = true;
	GetWorldTimerManager().SetTimerForNextTick(this, &AGun::RunDeferredAttachmentSanitize);
}

void AGun::RunDeferredAttachmentSanitize()
{
	bAttachmentSanitizeRequested = false;
	SanitizeInvalidAttachmentComponents();
}

bool AGun::IsInvalidWeaponAttachmentComponent(const UStaticMeshComponent* Component) const
{
	if (!IsWeaponAttachmentMesh(Component))
	{
		return false;
	}

	const USkeletalMeshComponent* MainMesh = ResolveMainSkeletalMesh();
	if (!MainMesh)
	{
		return true;
	}

	const USceneComponent* AttachParent = Component->GetAttachParent();
	if (AttachParent != MainMesh)
	{
		return true;
	}

	const FName AttachSocketName = Component->GetAttachSocketName();
	if (AttachSocketName.IsNone())
	{
		return true;
	}

	return !AttachParent->DoesSocketExist(AttachSocketName);
}

bool AGun::IsWeaponAttachmentMesh(const UStaticMeshComponent* Component)
{
	const UStaticMesh* StaticMesh = Component ? Component->GetStaticMesh() : nullptr;
	return StaticMesh && StaticMesh->GetPathName().Contains(WeaponAttachmentMeshPathToken);
}

bool AGun::ShouldRequestAttachmentSanitizeForFunction(const UFunction* Function)
{
	if (!Function)
	{
		return false;
	}

	const FString FunctionName = Function->GetName();
	return FunctionName.Contains(TEXT("AddComponent"))
		|| FunctionName.Contains(TEXT("Attach"))
		|| FunctionName.Contains(TEXT("Detach"))
		|| FunctionName.Contains(TEXT("Attachment"))
		|| FunctionName.Contains(TEXT("Attachament"))
		|| FunctionName.Contains(TEXT("Optic"))
		|| FunctionName.Contains(TEXT("Sight"));
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

FText AGun::MakeDefaultWeaponDisplayName(const UClass* WeaponClass)
{
	FString ClassName;

#if WITH_EDITORONLY_DATA
	if (WeaponClass && WeaponClass->ClassGeneratedBy)
	{
		ClassName = WeaponClass->ClassGeneratedBy->GetName();
	}
#endif

	if (ClassName.IsEmpty())
	{
		ClassName = WeaponClass ? WeaponClass->GetName() : StaticClass()->GetName();
	}

	return FText::FromString(CleanGeneratedWeaponClassName(ClassName));
}

bool AGun::HasCustomWeaponDisplayName() const
{
	const FString DisplayNameString = WeaponDisplayName.ToString().TrimStartAndEnd();
	if (DisplayNameString.IsEmpty())
	{
		return false;
	}

	return !(GetClass() != StaticClass() && DisplayNameString.Equals(TEXT("Gun"), ESearchCase::IgnoreCase));
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
