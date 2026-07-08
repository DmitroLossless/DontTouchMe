// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gun.h"

#include "Animation/AnimInstance.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/ActorComponent.h"
#include "FakeGunAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"
#include "../Projectile/ProjectileImpactData.h"
#include "../TouchMe.h"

const FName AGun::MainSkeletalMeshComponentName(TEXT("Item"));

namespace
{
	const TCHAR* WeaponAttachmentMeshPathToken = TEXT("/Weapons/Attachments/");
	const TCHAR* AcogMeshPathToken = TEXT("/Game/Fps/Weapons/Scope/Acog/SM_ACOG_Scope");
	const TCHAR* AcogRenderDiscMeshPath = TEXT("/Game/NoDualRenderScope/Scope_Mat_Function/SM_Disc_RenderGlass.SM_Disc_RenderGlass");
	const TCHAR* AcogGlassMeshPath = TEXT("/Game/NoDualRenderScope/Scope_Mat_Function/SM_ScopeGlass4.SM_ScopeGlass4");
	const TCHAR* AcogRenderMaterialPath = TEXT("/Game/NoDualRenderScope/Scope_Mat_Function/NewMaterials/M_Scope_Translucent_Acog.M_Scope_Translucent_Acog");
	const TCHAR* AcogGlassMaterialPath = TEXT("/Game/NoDualRenderScope/Scope_Mat_Function/NewMaterials/M_Scope_Glass_Acog.M_Scope_Glass_Acog");
	const TCHAR* AcogMaterialParameterCollectionPath = TEXT("/Game/Fps/Weapons/Camera/MPC_FP.MPC_FP");
	const TCHAR* OpticsTablePath = TEXT("/Game/MP_System_V3/Game/Blueprints/DataTables/DT_Optics.DT_Optics");
	const TCHAR* DefaultAttachmentFeedbackFXPath = TEXT("/Game/MP_System_V3/Game/Commons/Particles/P_Metal_Impact.P_Metal_Impact");
	const TCHAR* DefaultAttachmentFeedbackSoundPath = TEXT("/Game/MP_System_V3/Game/Sounds/Cue/GUI_Select_02_Cue.GUI_Select_02_Cue");
	const TCHAR* DefaultWeaponSpawnFeedbackFXPath = TEXT("/Game/MP_System_V3/Game/Commons/Particles/P_Dust_Dark.P_Dust_Dark");
	const TCHAR* DefaultWeaponSpawnFeedbackSoundPath = TEXT("/Game/MP_System_V3/Game/Sounds/Cue/Select_Cue.Select_Cue");
	constexpr float AttachmentFeedbackMonitorInterval = 0.05f;
	constexpr float AttachmentFeedbackStartupSuppressSeconds = 0.35f;
	const FName UnderbarrelDataPropertyName(TEXT("UnderbarrelData"));
	const FName UnderbarrelSocketName(TEXT("Underbarrel"));
	const FName MuzzleSocketName(TEXT("Muzzle"));
	const FName SilencercoMuzzleSocketName(TEXT("MuzzleSilencerco"));
	const FName AcogRenderDiscComponentName(TEXT("ACOG_RenderDisc"));
	const FName AcogGlassComponentName(TEXT("ACOG_Glass"));
	const FName AcogRenderDiscSocketName(TEXT("RM_Scope"));
	const FName AcogGlassSocketName(TEXT("RM_Glass"));
	const FName AcogFOVParameterName(TEXT("FOV"));
	constexpr float ImpactFXForcedCleanupDelay = 2.0f;
	constexpr float CustomizationSkinPreviewHoldSeconds = 5.5f;
	constexpr float CustomizationSkinPreviewBlendSeconds = 5.0f;
	const FName CustomizationSkinPreviewAlphaParameterNames[] =
	{
		TEXT("SkinAlpha"),
		TEXT("Skin Alpha"),
		TEXT("SkinBlend"),
		TEXT("Skin Blend"),
		TEXT("BlendAlpha"),
		TEXT("Blend Alpha"),
		TEXT("MaterialSkin"),
		TEXT("Skin"),
		TEXT("Alpha"),
		TEXT("ConstAlpha"),
		TEXT("Opacity")
	};

	void ScheduleImpactFXCleanup(UWorld* World, UActorComponent* Component)
	{
		if (!World || !Component)
		{
			return;
		}

		TWeakObjectPtr<UActorComponent> WeakComponent(Component);
		FTimerHandle CleanupHandle;
		World->GetTimerManager().SetTimer(
			CleanupHandle,
			FTimerDelegate::CreateLambda([WeakComponent]()
			{
				if (UActorComponent* ComponentToDestroy = WeakComponent.Get())
				{
					ComponentToDestroy->DestroyComponent();
				}
			}),
			ImpactFXForcedCleanupDelay,
			false);
	}

	FString CleanGeneratedWeaponClassName(FString ClassName)
	{
		ClassName.RemoveFromStart(TEXT("SKEL_"));
		ClassName.RemoveFromStart(TEXT("REINST_"));
		ClassName.RemoveFromEnd(TEXT("_C"));
		return ClassName;
	}

	bool ShouldTraceGunFunction(const UFunction* Function)
	{
		if (!IsTouchMeRuntimeTraceEnabled())
		{
			return false;
		}

		if (!Function)
		{
			return false;
		}

		const FString Name = Function->GetName();
		if (Name.Contains(TEXT("Tick"))
			|| Name.Contains(TEXT("UpdateAnimation"))
			|| Name.Contains(TEXT("EvaluateGraphExposedInputs"))
			|| Name.Contains(TEXT("AnimGraph")))
		{
			return false;
		}

		const FString Path = Function->GetPathName();
		return Name.Contains(TEXT("BeginPlay"))
			|| Name.Contains(TEXT("ReceiveBeginPlay"))
			|| Name.Contains(TEXT("UserConstructionScript"))
			|| Name.Contains(TEXT("ExecuteUbergraph"))
			|| Name.Contains(TEXT("SetActive"))
			|| Name.Contains(TEXT("Activate"))
			|| Name.Contains(TEXT("Weapon"))
			|| Name.Contains(TEXT("Gun"))
			|| Name.Contains(TEXT("Item"))
			|| Name.Contains(TEXT("Equip"))
			|| Name.Contains(TEXT("Draw"))
			|| Name.Contains(TEXT("Holster"))
			|| Name.Contains(TEXT("Active"))
			|| Name.Contains(TEXT("Attach"))
			|| Name.Contains(TEXT("Firemode"))
			|| Name.Contains(TEXT("Reload"))
			|| Name.Contains(TEXT("Slot"))
			|| Name.Contains(TEXT("Select"))
			|| Name.Contains(TEXT("Switch"))
			|| Name.Contains(TEXT("Primary"))
			|| Name.Contains(TEXT("Secondary"))
			|| Name.Contains(TEXT("Use"))
			|| Name.Contains(TEXT("MPS"))
			|| Name.Contains(TEXT("Anim"))
			|| Name.Contains(TEXT("Montage"))
			|| Name.Contains(TEXT("Fake"))
			|| Path.Contains(TEXT("BP_Weapon_Master"))
			|| Path.Contains(TEXT("BP_Kriss"))
			|| Path.Contains(TEXT("BP_SMG"))
			|| Path.Contains(TEXT("MP_System_V3"));
	}

	bool IsUnderbarrelSocketName(const FName SocketName)
	{
		return SocketName == UnderbarrelSocketName
			|| SocketName.ToString().Contains(TEXT("Underbarrel"), ESearchCase::IgnoreCase);
	}

	FName ResolveCompatibleWeaponAttachmentSocketName(
		const USceneComponent* AttachParent,
		const FName RequestedSocketName)
	{
		if (!AttachParent || RequestedSocketName.IsNone())
		{
			return RequestedSocketName;
		}

		if (AttachParent->DoesSocketExist(RequestedSocketName))
		{
			return RequestedSocketName;
		}

		if (RequestedSocketName == SilencercoMuzzleSocketName
			&& AttachParent->DoesSocketExist(MuzzleSocketName))
		{
			return MuzzleSocketName;
		}

		return RequestedSocketName;
	}

	bool TryNormalizeCompatibleWeaponAttachmentSocket(UStaticMeshComponent* Component)
	{
		if (!IsValid(Component))
		{
			return false;
		}

		USceneComponent* AttachParent = Component->GetAttachParent();
		const FName CurrentSocketName = Component->GetAttachSocketName();
		const FName ResolvedSocketName = ResolveCompatibleWeaponAttachmentSocketName(AttachParent, CurrentSocketName);
		if (!AttachParent || ResolvedSocketName == CurrentSocketName)
		{
			return false;
		}

		Component->AttachToComponent(
			AttachParent,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			ResolvedSocketName);
		return true;
	}

	const FProperty* FindStructFieldByPrefix(const UScriptStruct* Struct, const TCHAR* Prefix)
	{
		if (!Struct)
		{
			return nullptr;
		}

		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* Property = *It;
			if (Property && Property->GetName().StartsWith(Prefix))
			{
				return Property;
			}
		}

		return nullptr;
	}

	const UStaticMesh* ReadStaticMeshFieldByPrefix(const FStructProperty* StructProperty, const void* StructValue, const TCHAR* Prefix)
	{
		if (!StructProperty || !StructValue)
		{
			return nullptr;
		}

		const FProperty* MeshProperty = FindStructFieldByPrefix(StructProperty->Struct, Prefix);
		const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(MeshProperty);
		if (!ObjectProperty)
		{
			return nullptr;
		}

		const void* ValueAddress = ObjectProperty->ContainerPtrToValuePtr<void>(StructValue);
		return Cast<UStaticMesh>(ObjectProperty->GetObjectPropertyValue(ValueAddress));
	}

	FName ReadSocketFieldByPrefix(const FStructProperty* StructProperty, const void* StructValue, const TCHAR* Prefix)
	{
		if (!StructProperty || !StructValue)
		{
			return NAME_None;
		}

		const FProperty* SocketProperty = FindStructFieldByPrefix(StructProperty->Struct, Prefix);
		if (!SocketProperty)
		{
			return NAME_None;
		}

		const void* ValueAddress = SocketProperty->ContainerPtrToValuePtr<void>(StructValue);
		if (const FNameProperty* NameProperty = CastField<FNameProperty>(SocketProperty))
		{
			return NameProperty->GetPropertyValue(ValueAddress);
		}

		if (const FStrProperty* StringProperty = CastField<FStrProperty>(SocketProperty))
		{
			return FName(*StringProperty->GetPropertyValue(ValueAddress));
		}

		if (const FTextProperty* TextProperty = CastField<FTextProperty>(SocketProperty))
		{
			return FName(*TextProperty->GetPropertyValue(ValueAddress).ToString());
		}

		return NAME_None;
	}

	void AppendFunctionParameters(const UFunction* Function, void* Parameters, FString& Out)
	{
		if (!Function || !Parameters)
		{
			return;
		}

		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property || !Property->HasAnyPropertyFlags(CPF_Parm))
			{
				continue;
			}

			const FString PropertyName = Property->GetName();
			const void* ValueAddress = Property->ContainerPtrToValuePtr<void>(Parameters);
			if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				const UObject* Value = ObjectProperty->GetObjectPropertyValue(ValueAddress);
				Out += FString::Printf(TEXT(" %s=[%s];"), *PropertyName, Value ? *Value->GetPathName() : TEXT("None"));
			}
			else if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
			{
				Out += FString::Printf(TEXT(" %s=%d;"), *PropertyName, BoolProperty->GetPropertyValue(ValueAddress) ? 1 : 0);
			}
			else if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
			{
				Out += FString::Printf(TEXT(" %s=%s;"), *PropertyName, *NameProperty->GetPropertyValue(ValueAddress).ToString());
			}
			else if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
			{
				if (NumericProperty->IsFloatingPoint())
				{
					Out += FString::Printf(TEXT(" %s=%.3f;"), *PropertyName, NumericProperty->GetFloatingPointPropertyValue(ValueAddress));
				}
				else if (NumericProperty->IsInteger())
				{
					Out += FString::Printf(TEXT(" %s=%lld;"), *PropertyName, NumericProperty->GetSignedIntPropertyValue(ValueAddress));
				}
			}
		}
	}

	FString EnumValueToString(const UEnum* Enum, const int64 Value)
	{
		if (!Enum)
		{
			return FString::Printf(TEXT("%lld"), static_cast<long long>(Value));
		}

		FString DisplayName = Enum->GetDisplayNameTextByValue(Value).ToString();
		if (DisplayName.IsEmpty())
		{
			DisplayName = Enum->GetNameStringByValue(Value);
		}

		return FString::Printf(TEXT("%lld/%s"), static_cast<long long>(Value), *DisplayName);
	}

	bool PropertyNameMatches(const FProperty* Property, const TCHAR* ExpectedName)
	{
		if (!Property)
		{
			return false;
		}

		const FString Expected(ExpectedName);
		const FString PropertyName = Property->GetName();
		const bool bMatchesName = PropertyName.Equals(Expected, ESearchCase::IgnoreCase)
			|| PropertyName.StartsWith(Expected + TEXT("_"), ESearchCase::IgnoreCase)
			|| Property->GetAuthoredName().Equals(Expected, ESearchCase::IgnoreCase);
#if WITH_EDITOR
		return bMatchesName || Property->GetDisplayNameText().ToString().Equals(Expected, ESearchCase::IgnoreCase);
#else
		return bMatchesName;
#endif
	}

	const FProperty* FindPropertyByName(const UStruct* Struct, const TCHAR* ExpectedName)
	{
		if (!Struct)
		{
			return nullptr;
		}

		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* Property = *It;
			if (PropertyNameMatches(Property, ExpectedName))
			{
				return Property;
			}
		}

		return nullptr;
	}

	bool ReadBoolPropertyByName(const UObject* Object, const TCHAR* ExpectedName, bool& bOutValue)
	{
		bOutValue = false;
		if (!Object)
		{
			return false;
		}

		const FBoolProperty* BoolProperty = CastField<FBoolProperty>(FindPropertyByName(Object->GetClass(), ExpectedName));
		if (!BoolProperty)
		{
			return false;
		}

		bOutValue = BoolProperty->GetPropertyValue_InContainer(Object);
		return true;
	}

	bool ReadStaticMeshAssetPath(const FProperty* Property, const void* Container, FString& OutAssetPath)
	{
		OutAssetPath.Reset();
		if (!Property || !Container)
		{
			return false;
		}

		const void* ValueAddress = Property->ContainerPtrToValuePtr<void>(Container);
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			const UObject* Value = ObjectProperty->GetObjectPropertyValue(ValueAddress);
			if (!Value)
			{
				return false;
			}

			OutAssetPath = FSoftObjectPath(Value).GetAssetPathString();
			return !OutAssetPath.IsEmpty();
		}

		if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
		{
			const FSoftObjectPtr* Value = SoftObjectProperty->ContainerPtrToValuePtr<FSoftObjectPtr>(Container);
			if (!Value)
			{
				return false;
			}

			OutAssetPath = Value->ToSoftObjectPath().GetAssetPathString();
			return !OutAssetPath.IsEmpty();
		}

		return false;
	}

	bool ReadNumericPropertyValue(const FProperty* Property, const void* Container, float& OutValue)
	{
		OutValue = 0.0f;
		const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property);
		if (!NumericProperty || !Container)
		{
			return false;
		}

		const void* ValueAddress = NumericProperty->ContainerPtrToValuePtr<void>(Container);
		OutValue = NumericProperty->IsFloatingPoint()
			? static_cast<float>(NumericProperty->GetFloatingPointPropertyValue(ValueAddress))
			: static_cast<float>(NumericProperty->GetSignedIntPropertyValue(ValueAddress));
		return true;
	}

	const TMap<FString, float>& GetOpticZoomByMeshPath()
	{
		static bool bInitialized = false;
		static TMap<FString, float> ZoomByMeshPath;
#if WITH_EDITOR
		bInitialized = false;
#endif
		if (bInitialized)
		{
			return ZoomByMeshPath;
		}

		bInitialized = true;
		ZoomByMeshPath.Reset();
		UDataTable* OpticsTable = LoadObject<UDataTable>(nullptr, OpticsTablePath);
		if (!OpticsTable || !OpticsTable->GetRowStruct())
		{
			return ZoomByMeshPath;
		}

		const FProperty* MeshProperty = FindPropertyByName(OpticsTable->GetRowStruct(), TEXT("Mesh"));
		const FProperty* OpticsFOVProperty = FindPropertyByName(OpticsTable->GetRowStruct(), TEXT("Optics_FOV"));
		if (!MeshProperty || !OpticsFOVProperty)
		{
			return ZoomByMeshPath;
		}

		for (const TPair<FName, uint8*>& RowPair : OpticsTable->GetRowMap())
		{
			if (!RowPair.Value)
			{
				continue;
			}

			FString MeshAssetPath;
			float ZoomMultiplier = 1.0f;
			if (!ReadStaticMeshAssetPath(MeshProperty, RowPair.Value, MeshAssetPath)
				|| !ReadNumericPropertyValue(OpticsFOVProperty, RowPair.Value, ZoomMultiplier)
				|| MeshAssetPath.IsEmpty())
			{
				continue;
			}

			ZoomByMeshPath.Add(MeshAssetPath, FMath::Max(1.0f, ZoomMultiplier));
		}

		return ZoomByMeshPath;
	}

	bool TryGetOpticZoomMultiplierForMesh(const UStaticMesh* StaticMesh, float& OutZoomMultiplier)
	{
		OutZoomMultiplier = 1.0f;
		if (!StaticMesh)
		{
			return false;
		}

		const FString MeshAssetPath = FSoftObjectPath(StaticMesh).GetAssetPathString();
		if (const float* FoundZoom = GetOpticZoomByMeshPath().Find(MeshAssetPath))
		{
			OutZoomMultiplier = *FoundZoom;
			return true;
		}

		return false;
	}

	void AppendSimplePropertyValue(const FProperty* Property, const void* ValueAddress, FString& Out)
	{
		if (!Property || !ValueAddress)
		{
			Out += TEXT("None");
			return;
		}

		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			const UObject* Value = ObjectProperty->GetObjectPropertyValue(ValueAddress);
			Out += Value ? Value->GetPathName() : TEXT("None");
		}
		else if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			Out += BoolProperty->GetPropertyValue(ValueAddress) ? TEXT("1") : TEXT("0");
		}
		else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			const int64 Value = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValueAddress);
			Out += EnumValueToString(EnumProperty->GetEnum(), Value);
		}
		else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			Out += EnumValueToString(ByteProperty->Enum, ByteProperty->GetPropertyValue(ValueAddress));
		}
		else if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			Out += NameProperty->GetPropertyValue(ValueAddress).ToString();
		}
		else if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
		{
			Out += StringProperty->GetPropertyValue(ValueAddress);
		}
		else if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
		{
			Out += TextProperty->GetPropertyValue(ValueAddress).ToString();
		}
		else if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			if (NumericProperty->IsFloatingPoint())
			{
				Out += FString::Printf(TEXT("%.3f"), NumericProperty->GetFloatingPointPropertyValue(ValueAddress));
			}
			else if (NumericProperty->IsInteger())
			{
				Out += FString::Printf(TEXT("%lld"), NumericProperty->GetSignedIntPropertyValue(ValueAddress));
			}
		}
		else
		{
			Out += Property->GetCPPType();
		}
	}

	void AppendGunBlueprintState(const AGun* Gun, FString& Out)
	{
		if (!Gun)
		{
			return;
		}

		for (TFieldIterator<FProperty> It(Gun->GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property)
			{
				continue;
			}

			const FString PropertyName = Property->GetName();
			if (!PropertyName.Contains(TEXT("Active"))
				&& !PropertyName.Contains(TEXT("Current"))
				&& !PropertyName.Contains(TEXT("Equipped"))
				&& !PropertyName.Contains(TEXT("Selected"))
				&& !PropertyName.Contains(TEXT("Weapon"))
				&& !PropertyName.Contains(TEXT("Gun"))
				&& !PropertyName.Contains(TEXT("Item"))
				&& !PropertyName.Contains(TEXT("Slot"))
				&& !PropertyName.Contains(TEXT("Inventory"))
				&& !PropertyName.Contains(TEXT("Holster"))
				&& !PropertyName.Contains(TEXT("Draw"))
				&& !PropertyName.Contains(TEXT("First"))
				&& !PropertyName.Contains(TEXT("Anim"))
				&& !PropertyName.Contains(TEXT("Fake")))
			{
				continue;
			}

			Out += FString::Printf(TEXT(" %s="), *PropertyName);
			AppendSimplePropertyValue(Property, Property->ContainerPtrToValuePtr<void>(Gun), Out);
			Out += TEXT(";");
		}
	}

	FString DescribeSceneComponent(const USceneComponent* Component)
	{
		if (!Component)
		{
			return TEXT("None");
		}

		const USceneComponent* Parent = Component->GetAttachParent();
		return FString::Printf(
			TEXT("%s Parent=%s Socket=%s Visible=%d Rel=%s World=%s"),
			*Component->GetName(),
			Parent ? *Parent->GetName() : TEXT("None"),
			*Component->GetAttachSocketName().ToString(),
			Component->IsVisible() ? 1 : 0,
			*Component->GetRelativeTransform().ToHumanReadableString(),
			*Component->GetComponentTransform().ToHumanReadableString());
	}

	FString DescribeAnimClass(const USkeletalMeshComponent* Mesh)
	{
		const UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
		return AnimInstance ? AnimInstance->GetClass()->GetPathName() : TEXT("None");
	}
}

AGun::AGun()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	FakeSkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FakeSkeletalMeshComponent"));
	FakeSkeletalMeshComponent->SetupAttachment(SceneRoot);
	FakeSkeletalMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FakeSkeletalMeshComponent->SetGenerateOverlapEvents(false);
	FakeSkeletalMeshComponent->SetVisibility(false);

	FakeAttachedSkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FakeAttachedSkeletalMeshComponent"));
	FakeAttachedSkeletalMeshComponent->SetupAttachment(FakeSkeletalMeshComponent);
	FakeAttachedSkeletalMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FakeAttachedSkeletalMeshComponent->SetGenerateOverlapEvents(false);
	FakeAttachedSkeletalMeshComponent->SetVisibility(false);

	ADSSocketComponent = CreateDefaultSubobject<USceneComponent>(TEXT("ADS_Eye"));
	ADSSocketComponent->SetupAttachment(SceneRoot);
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

	if (IsTouchMeRuntimeTraceEnabled())
	{
		UE_LOG(
			LogTouchMeRuntimeTrace,
			Warning,
			TEXT("[GunBeginPlay] Gun=%s Class=%s Owner=%s FakeMode=%d Root={%s}"),
			*GetPathName(),
			*GetClass()->GetPathName(),
			GetOwner() ? *GetOwner()->GetPathName() : TEXT("None"),
			bFakeMode ? 1 : 0,
			*DescribeSceneComponent(GetRootComponent()));
	}

	ApplyFakeMode();
	RefreshADSSocket();
	RequestDeferredAttachmentSanitize();
	RequestDeferredCustomizationSkinPreviewCycle();

	UWorld* World = GetWorld();
	if (World)
	{
		AttachmentFeedbackSuppressUntilTime = World->GetTimeSeconds() + AttachmentFeedbackStartupSuppressSeconds;
		GetWorldTimerManager().SetTimer(
			AttachmentFeedbackMonitorTimerHandle,
			this,
			&AGun::MonitorAttachmentFeedbackState,
			AttachmentFeedbackMonitorInterval,
			true,
			AttachmentFeedbackMonitorInterval);
	}
}

void AGun::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(AttachmentFeedbackMonitorTimerHandle);
	GetWorldTimerManager().ClearTimer(CustomizationSkinPreviewStartTimerHandle);
	StopCustomizationSkinPreviewCycle();
	DestroyAcogRenderComponents();

	if (bFakeModeApplied)
	{
		RestoreFromFakeMode();
	}

	Super::EndPlay(EndPlayReason);
}

void AGun::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UStaticMeshComponent* AcogOpticComponent = ResolveAcogOpticComponent();
	if (!IsValid(AcogOpticComponent))
	{
		DestroyAcogRenderComponents();
	}
	else
	{
		const bool bVisible = AcogOpticComponent->IsVisible();
		if (IsValid(AcogRenderDiscComponent))
		{
			AcogRenderDiscComponent->SetVisibility(bVisible, true);
		}

		if (IsValid(AcogGlassComponent))
		{
			AcogGlassComponent->SetVisibility(bVisible, true);
		}

		UpdateAcogMaterialParameterCollection();
	}

	UpdateCustomizationSkinPreviewCycle(DeltaSeconds);
}

void AGun::SetFakeMode(const bool bEnabled)
{
	if (IsTouchMeRuntimeTraceEnabled())
	{
		UE_LOG(
			LogTouchMeRuntimeTrace,
			Warning,
			TEXT("[GunSetFakeMode] Gun=%s Class=%s Old=%d New=%d"),
			*GetPathName(),
			*GetClass()->GetPathName(),
			bFakeMode ? 1 : 0,
			bEnabled ? 1 : 0);
	}

	bFakeMode = bEnabled;
	ApplyFakeMode();
	RefreshADSSocket();
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
	const bool bTrace = ShouldTraceGunFunction(Function);
	if (bTrace)
	{
		FString Params;
		AppendFunctionParameters(Function, Parameters, Params);
		FString BlueprintState;
		AppendGunBlueprintState(this, BlueprintState);
		UE_LOG(
			LogTouchMeRuntimeTrace,
			Warning,
			TEXT("[GunProcessEvent:Before] Gun=%s Class=%s Function=%s Params={%s} BlueprintState={%s} Root={%s}"),
			*GetPathName(),
			*GetClass()->GetPathName(),
			Function ? *Function->GetPathName() : TEXT("None"),
			*Params,
			*BlueprintState,
			*DescribeSceneComponent(GetRootComponent()));
	}

	Super::ProcessEvent(Function, Parameters);

	if (ShouldRequestAttachmentSanitizeForFunction(Function))
	{
		if (HasActorBegunPlay())
		{
			RequestDeferredAttachmentFeedback(Function);
		}
		else
		{
			RequestDeferredAttachmentSanitize();
		}
		RefreshADSSocket();
	}

	if (bTrace)
	{
		USkeletalMeshComponent* MainMesh = ResolveMainSkeletalMesh();
		FString BlueprintState;
		AppendGunBlueprintState(this, BlueprintState);
		UE_LOG(
			LogTouchMeRuntimeTrace,
			Warning,
			TEXT("[GunProcessEvent:After] Gun=%s Class=%s Function=%s BlueprintState={%s} Root={%s} MainMesh={%s Anim=%s Asset=%s} FakeMesh={%s Anim=%s Asset=%s} FakeAttachedMesh={%s Asset=%s}"),
			*GetPathName(),
			*GetClass()->GetPathName(),
			Function ? *Function->GetPathName() : TEXT("None"),
			*BlueprintState,
			*DescribeSceneComponent(GetRootComponent()),
			*DescribeSceneComponent(MainMesh),
			*DescribeAnimClass(MainMesh),
			MainMesh && MainMesh->GetSkeletalMeshAsset() ? *MainMesh->GetSkeletalMeshAsset()->GetPathName() : TEXT("None"),
			*DescribeSceneComponent(FakeSkeletalMeshComponent),
			*DescribeAnimClass(FakeSkeletalMeshComponent),
			FakeSkeletalMeshComponent && FakeSkeletalMeshComponent->GetSkeletalMeshAsset()
				? *FakeSkeletalMeshComponent->GetSkeletalMeshAsset()->GetPathName()
				: TEXT("None"),
			*DescribeSceneComponent(FakeAttachedSkeletalMeshComponent),
			FakeAttachedSkeletalMeshComponent && FakeAttachedSkeletalMeshComponent->GetSkeletalMeshAsset()
				? *FakeAttachedSkeletalMeshComponent->GetSkeletalMeshAsset()->GetPathName()
				: TEXT("None"));
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
		if (!IsValid(StaticMeshComponent))
		{
			continue;
		}

		const bool bNormalizedSocket = IsWeaponAttachmentMesh(StaticMeshComponent)
			&& TryNormalizeCompatibleWeaponAttachmentSocket(StaticMeshComponent);
		if (bNormalizedSocket)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("Resolved compatible weapon attachment socket for '%s' on '%s' to '%s'."),
				*StaticMeshComponent->GetName(),
				*GetName(),
				*StaticMeshComponent->GetAttachSocketName().ToString());
		}

		if (!IsInvalidWeaponAttachmentComponent(StaticMeshComponent))
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

void AGun::RefreshADSSocket()
{
	if (!ADSSocketComponent || HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	USceneComponent* AttachParent = nullptr;
	FName AttachSocketName = NAME_None;
	bADSSocketResolved = ResolveADSSocketAttachTarget(AttachParent, AttachSocketName);

	if (!AttachParent)
	{
		AttachParent = SceneRoot;
		AttachSocketName = NAME_None;
	}

	if (ADSSocketComponent->GetAttachParent() != AttachParent
		|| ADSSocketComponent->GetAttachSocketName() != AttachSocketName)
	{
		ADSSocketComponent->AttachToComponent(
			AttachParent,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		AttachSocketName);
	}

	ADSSocketComponent->SetRelativeTransform(
		bUseSecondaryADSSocket ? SecondaryADSSocketOffset : ADSSocketOffset);
}

void AGun::SetUseSecondaryADSSocket(const bool bUseSecondary)
{
	if (bUseSecondaryADSSocket == bUseSecondary)
	{
		return;
	}

	bUseSecondaryADSSocket = bUseSecondary;
	RefreshADSSocket();
}

bool AGun::GetADSSocketWorldTransform(FTransform& OutTransform) const
{
	if (!ADSSocketComponent || !bADSSocketResolved)
	{
		OutTransform = FTransform::Identity;
		return false;
	}

	OutTransform = ADSSocketComponent->GetComponentTransform();
	return true;
}

FVector AGun::GetADSSocketForwardVector() const
{
	FTransform ADSTransform;
	if (!GetADSSocketWorldTransform(ADSTransform))
	{
		return FVector::ForwardVector;
	}

	return ADSTransform.GetUnitAxis(EAxis::X);
}

bool AGun::GetActiveOpticZoomMultiplier(float& OutZoomMultiplier) const
{
	OutZoomMultiplier = 1.0f;

	const UStaticMeshComponent* OpticComponent = bUseSecondaryADSSocket
		? ResolveSecondaryOpticComponent()
		: ResolvePrimaryOpticComponent();
	if (!IsValid(OpticComponent))
	{
		return false;
	}

	return TryGetOpticZoomMultiplierForMesh(OpticComponent->GetStaticMesh(), OutZoomMultiplier);
}

void AGun::SpawnAttachmentFeedbackAtLocation(const FVector Location, const FRotator Rotation)
{
	UWorld* World = GetWorld();
	if (!World || HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	UFXSystemAsset* FeedbackFX = AttachmentFeedbackFX;
	if (!FeedbackFX)
	{
		FeedbackFX = LoadObject<UFXSystemAsset>(nullptr, DefaultAttachmentFeedbackFXPath);
	}

	if (FeedbackFX)
	{
		if (UParticleSystem* CascadeSystem = Cast<UParticleSystem>(FeedbackFX))
		{
			UParticleSystemComponent* ParticleComponent = UGameplayStatics::SpawnEmitterAtLocation(
				World,
				CascadeSystem,
				FTransform(Rotation, Location, AttachmentFeedbackScale),
				true,
				EPSCPoolMethod::None,
				true);
			if (ParticleComponent)
			{
				ParticleComponent->bAutoDestroy = true;
				ScheduleImpactFXCleanup(World, ParticleComponent);
			}
		}
		else if (UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(FeedbackFX))
		{
			UNiagaraComponent* NiagaraComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				this,
				NiagaraSystem,
				Location,
				Rotation,
				AttachmentFeedbackScale,
				true,
				true,
				ENCPoolMethod::None,
				true);
			if (NiagaraComponent)
			{
				NiagaraComponent->SetAutoDestroy(true);
				ScheduleImpactFXCleanup(World, NiagaraComponent);
			}
		}
	}

	USoundBase* FeedbackSound = AttachmentFeedbackSound;
	if (!FeedbackSound)
	{
		FeedbackSound = LoadObject<USoundBase>(nullptr, DefaultAttachmentFeedbackSoundPath);
	}

	if (FeedbackSound)
	{
		if (bAttachmentFeedbackPlaySound2D)
		{
			UGameplayStatics::PlaySound2D(this, FeedbackSound, AttachmentFeedbackVolume, AttachmentFeedbackPitch);
		}
		else
		{
			UGameplayStatics::PlaySoundAtLocation(
				this,
				FeedbackSound,
				Location,
				Rotation,
				AttachmentFeedbackVolume,
				AttachmentFeedbackPitch);
		}
	}
}

void AGun::PlayWeaponSpawnFeedback()
{
	const FTransform FeedbackTransform = ResolveWeaponSpawnFeedbackTransform();
	SpawnWeaponSpawnFeedbackAtLocation(FeedbackTransform.GetLocation(), FeedbackTransform.Rotator());
	StartCustomizationSkinPreviewCycle();
}

void AGun::SpawnWeaponSpawnFeedbackAtLocation(const FVector Location, const FRotator Rotation)
{
	UWorld* World = GetWorld();
	if (!World || HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	UFXSystemAsset* FeedbackFX = WeaponSpawnFeedbackFX;
	if (!FeedbackFX)
	{
		FeedbackFX = LoadObject<UFXSystemAsset>(nullptr, DefaultWeaponSpawnFeedbackFXPath);
	}

	if (FeedbackFX)
	{
		if (UParticleSystem* CascadeSystem = Cast<UParticleSystem>(FeedbackFX))
		{
			UParticleSystemComponent* ParticleComponent = UGameplayStatics::SpawnEmitterAtLocation(
				World,
				CascadeSystem,
				FTransform(Rotation, Location, WeaponSpawnFeedbackScale),
				true,
				EPSCPoolMethod::None,
				true);
			if (ParticleComponent)
			{
				ParticleComponent->bAutoDestroy = true;
				ScheduleImpactFXCleanup(World, ParticleComponent);
			}
		}
		else if (UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(FeedbackFX))
		{
			UNiagaraComponent* NiagaraComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				this,
				NiagaraSystem,
				Location,
				Rotation,
				WeaponSpawnFeedbackScale,
				true,
				true,
				ENCPoolMethod::None,
				true);
			if (NiagaraComponent)
			{
				NiagaraComponent->SetAutoDestroy(true);
				ScheduleImpactFXCleanup(World, NiagaraComponent);
			}
		}
	}

	USoundBase* FeedbackSound = WeaponSpawnFeedbackSound;
	if (!FeedbackSound)
	{
		FeedbackSound = LoadObject<USoundBase>(nullptr, DefaultWeaponSpawnFeedbackSoundPath);
	}

	if (FeedbackSound)
	{
		if (bWeaponSpawnFeedbackPlaySound2D)
		{
			UGameplayStatics::PlaySound2D(this, FeedbackSound, WeaponSpawnFeedbackVolume, WeaponSpawnFeedbackPitch);
		}
		else
		{
			UGameplayStatics::PlaySoundAtLocation(
				this,
				FeedbackSound,
				Location,
				Rotation,
				WeaponSpawnFeedbackVolume,
				WeaponSpawnFeedbackPitch);
		}
	}
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

void AGun::RequestDeferredAttachmentFeedback(const UFunction* Function)
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	const FName PreferredSocketName = ResolveAttachmentFeedbackPreferredSocket(Function);
	if (!PreferredSocketName.IsNone())
	{
		AttachmentFeedbackPreferredSocketName = PreferredSocketName;
	}

	bAttachmentFeedbackRequested = true;
	RequestDeferredAttachmentSanitize();
}

void AGun::RequestDeferredCustomizationSkinPreviewCycle()
{
	if (HasAnyFlags(RF_ClassDefaultObject) || bCustomizationSkinPreviewCycleActive)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	GetWorldTimerManager().SetTimer(
		CustomizationSkinPreviewStartTimerHandle,
		this,
		&AGun::RunDeferredCustomizationSkinPreviewCycle,
		0.1f,
		false);
}

void AGun::RunDeferredAttachmentSanitize()
{
	const bool bShouldPlayAttachmentFeedback = bAttachmentFeedbackRequested;
	bAttachmentFeedbackRequested = false;
	bAttachmentSanitizeRequested = false;
	SanitizeInvalidAttachmentComponents();
	SynchronizeUnderbarrelAttachmentComponent();
	SynchronizeAcogRenderComponents();
	RefreshADSSocket();

	if (bShouldPlayAttachmentFeedback)
	{
		if (AttachmentFeedbackPreferredSocketName.IsNone())
		{
			TMap<FName, FString> CurrentStateSignatures;
			TMap<FName, FName> CurrentStateSockets;
			BuildAttachmentFeedbackStateHash(&CurrentStateSignatures, &CurrentStateSockets);

			const FName ChangedSocketName = ResolveChangedAttachmentFeedbackSocket(CurrentStateSignatures, CurrentStateSockets);
			if (!ChangedSocketName.IsNone())
			{
				AttachmentFeedbackPreferredSocketName = ChangedSocketName;
			}
		}

		PlayAttachmentFeedback();
	}

	UpdateAttachmentFeedbackStateSnapshot();
	AttachmentFeedbackPreferredSocketName = NAME_None;
	RequestDeferredCustomizationSkinPreviewCycle();
}

void AGun::RunDeferredCustomizationSkinPreviewCycle()
{
	StartCustomizationSkinPreviewCycle();
}

void AGun::PlayAttachmentFeedback()
{
	const FTransform FeedbackTransform = ResolveAttachmentFeedbackTransform();
	SpawnAttachmentFeedbackAtLocation(FeedbackTransform.GetLocation(), FeedbackTransform.Rotator());
}

FTransform AGun::ResolveAttachmentFeedbackTransform() const
{
	USkeletalMeshComponent* MainMesh = ResolveMainSkeletalMesh();
	const FName PreferredSocketName = AttachmentFeedbackPreferredSocketName;
	FName ResolvedPreferredSocketName = NAME_None;

	if (MainMesh && !PreferredSocketName.IsNone())
	{
		ResolvedPreferredSocketName = ResolveCompatibleWeaponAttachmentSocketName(MainMesh, PreferredSocketName);
		if (!ResolvedPreferredSocketName.IsNone() && MainMesh->DoesSocketExist(ResolvedPreferredSocketName))
		{
			return MainMesh->GetSocketTransform(ResolvedPreferredSocketName, RTS_World);
		}
	}

	UStaticMeshComponent* BestComponent = nullptr;
	int32 BestScore = MIN_int32;
	TInlineComponentArray<UStaticMeshComponent*> StaticMeshComponents(this);
	for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
	{
		if (!IsValid(StaticMeshComponent)
			|| !StaticMeshComponent->IsVisible()
			|| !IsWeaponAttachmentMesh(StaticMeshComponent))
		{
			continue;
		}

		const USceneComponent* AttachParent = StaticMeshComponent->GetAttachParent();
		if (MainMesh && AttachParent != MainMesh)
		{
			continue;
		}

		const FName AttachSocketName = StaticMeshComponent->GetAttachSocketName();
		if (AttachSocketName.IsNone())
		{
			continue;
		}

		const FName ResolvedSocketName = ResolveCompatibleWeaponAttachmentSocketName(AttachParent, AttachSocketName);
		if (AttachParent && !AttachParent->DoesSocketExist(ResolvedSocketName))
		{
			continue;
		}

		int32 Score = 100;
		const FString SocketString = ResolvedSocketName.ToString();

		if (!ResolvedPreferredSocketName.IsNone() && ResolvedSocketName == ResolvedPreferredSocketName)
		{
			Score += 1000;
		}

		if (ResolvedSocketName == MuzzleSocketName
			|| ResolvedSocketName == SilencercoMuzzleSocketName
			|| SocketString.Contains(TEXT("Muzzle"), ESearchCase::IgnoreCase))
		{
			Score += 120;
		}
		else if (IsUnderbarrelSocketName(ResolvedSocketName))
		{
			Score += 90;
		}
		else if (SocketString.Contains(TEXT("Optic"), ESearchCase::IgnoreCase)
			|| SocketString.Contains(TEXT("Sight"), ESearchCase::IgnoreCase)
			|| SocketString.Contains(TEXT("Rail"), ESearchCase::IgnoreCase)
			|| SocketString.Contains(TEXT("Backup"), ESearchCase::IgnoreCase)
			|| SocketString.Contains(TEXT("Canted"), ESearchCase::IgnoreCase))
		{
			Score += 70;
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			BestComponent = StaticMeshComponent;
		}
	}

	if (BestComponent)
	{
		return BestComponent->GetComponentTransform();
	}

	if (MainMesh)
	{
		if (!ResolvedPreferredSocketName.IsNone() && MainMesh->DoesSocketExist(ResolvedPreferredSocketName))
		{
			return MainMesh->GetSocketTransform(ResolvedPreferredSocketName, RTS_World);
		}

		static const FName FallbackSocketNames[] =
		{
			MuzzleSocketName,
			SilencercoMuzzleSocketName,
			UnderbarrelSocketName,
			TEXT("Optics"),
			TEXT("AT_Backup"),
			TEXT("SideRail")
		};

		for (const FName FallbackSocketName : FallbackSocketNames)
		{
			const FName ResolvedSocketName = ResolveCompatibleWeaponAttachmentSocketName(MainMesh, FallbackSocketName);
			if (MainMesh->DoesSocketExist(ResolvedSocketName))
			{
				return MainMesh->GetSocketTransform(ResolvedSocketName, RTS_World);
			}
		}

		return MainMesh->GetComponentTransform();
	}

	if (const USceneComponent* Root = GetRootComponent())
	{
		return Root->GetComponentTransform();
	}

	return GetActorTransform();
}

FTransform AGun::ResolveWeaponSpawnFeedbackTransform() const
{
	if (USkeletalMeshComponent* MainMesh = ResolveMainSkeletalMesh())
	{
		if (!WeaponSpawnFeedbackSocketName.IsNone() && MainMesh->DoesSocketExist(WeaponSpawnFeedbackSocketName))
		{
			return MainMesh->GetSocketTransform(WeaponSpawnFeedbackSocketName, RTS_World);
		}

		return MainMesh->GetComponentTransform();
	}

	if (const USceneComponent* Root = GetRootComponent())
	{
		return Root->GetComponentTransform();
	}

	return GetActorTransform();
}

FName AGun::ResolveAttachmentFeedbackPreferredSocket(const UFunction* Function) const
{
	if (!Function)
	{
		return NAME_None;
	}

	const FString FunctionName = Function->GetName();
	const FString FunctionPath = Function->GetPathName();
	const auto ContainsToken = [&FunctionName, &FunctionPath](const TCHAR* Token)
	{
		return FunctionName.Contains(Token, ESearchCase::IgnoreCase)
			|| FunctionPath.Contains(Token, ESearchCase::IgnoreCase);
	};

	if (ContainsToken(TEXT("Silencerco")))
	{
		return SilencercoMuzzleSocketName;
	}

	if (ContainsToken(TEXT("Muzzle")) || ContainsToken(TEXT("Silencer")))
	{
		return MuzzleSocketName;
	}

	if (ContainsToken(TEXT("Underbarrel")))
	{
		return UnderbarrelSocketName;
	}

	if (ContainsToken(TEXT("SideRail")) || ContainsToken(TEXT("Side")))
	{
		return WeaponSecondaryOpticsSocketName;
	}

	if (ContainsToken(TEXT("Optic"))
		|| ContainsToken(TEXT("Scope"))
		|| ContainsToken(TEXT("Sight"))
		|| ContainsToken(TEXT("RDS")))
	{
		return bUseSecondaryADSSocket ? WeaponSecondaryOpticsSocketName : WeaponOpticsSocketName;
	}

	return NAME_None;
}

FName AGun::ResolveAttachmentFeedbackSocketFromContext(const FString& Context, const FName SocketName) const
{
	if (!SocketName.IsNone())
	{
		return SocketName;
	}

	const auto ContainsToken = [&Context](const TCHAR* Token)
	{
		return Context.Contains(Token, ESearchCase::IgnoreCase);
	};

	if (ContainsToken(TEXT("Silencerco")))
	{
		return SilencercoMuzzleSocketName;
	}

	if (ContainsToken(TEXT("Muzzle")) || ContainsToken(TEXT("Silencer")))
	{
		return MuzzleSocketName;
	}

	if (ContainsToken(TEXT("Underbarrel")))
	{
		return UnderbarrelSocketName;
	}

	if (ContainsToken(TEXT("SideRail")) || ContainsToken(TEXT("Side")))
	{
		return WeaponSecondaryOpticsSocketName;
	}

	if (ContainsToken(TEXT("Optic"))
		|| ContainsToken(TEXT("Scope"))
		|| ContainsToken(TEXT("Sight"))
		|| ContainsToken(TEXT("RDS")))
	{
		return bUseSecondaryADSSocket ? WeaponSecondaryOpticsSocketName : WeaponOpticsSocketName;
	}

	return NAME_None;
}

FName AGun::ResolveChangedAttachmentFeedbackSocket(
	const TMap<FName, FString>& CurrentStateSignatures,
	const TMap<FName, FName>& CurrentStateSockets) const
{
	const auto SortNames = [](TArray<FName>& Names)
	{
		Names.Sort([](const FName& Left, const FName& Right)
		{
			return Left.ToString() < Right.ToString();
		});
	};

	TArray<FName> CurrentKeys;
	CurrentStateSignatures.GetKeys(CurrentKeys);
	SortNames(CurrentKeys);

	for (const FName Key : CurrentKeys)
	{
		const FString* CurrentSignature = CurrentStateSignatures.Find(Key);
		const FString* PreviousSignature = LastAttachmentFeedbackStateSignatures.Find(Key);
		if (!CurrentSignature || (PreviousSignature && *PreviousSignature == *CurrentSignature))
		{
			continue;
		}

		const FName ChangedSocketName = CurrentStateSockets.FindRef(Key);
		if (!ChangedSocketName.IsNone())
		{
			return ChangedSocketName;
		}
	}

	TArray<FName> PreviousKeys;
	LastAttachmentFeedbackStateSignatures.GetKeys(PreviousKeys);
	SortNames(PreviousKeys);

	for (const FName Key : PreviousKeys)
	{
		if (CurrentStateSignatures.Contains(Key))
		{
			continue;
		}

		const FName RemovedSocketName = LastAttachmentFeedbackStateSockets.FindRef(Key);
		if (!RemovedSocketName.IsNone())
		{
			return RemovedSocketName;
		}
	}

	return NAME_None;
}

void AGun::MonitorAttachmentFeedbackState()
{
	if (HasAnyFlags(RF_ClassDefaultObject)
		|| bAttachmentFeedbackRequested
		|| bAttachmentSanitizeRequested
		|| bSanitizingAttachmentComponents)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TMap<FName, FString> CurrentStateSignatures;
	TMap<FName, FName> CurrentStateSockets;
	const uint32 CurrentHash = BuildAttachmentFeedbackStateHash(&CurrentStateSignatures, &CurrentStateSockets);
	if (!bAttachmentFeedbackStateInitialized
		|| World->GetTimeSeconds() < AttachmentFeedbackSuppressUntilTime)
	{
		LastAttachmentFeedbackStateHash = CurrentHash;
		LastAttachmentFeedbackStateSignatures = MoveTemp(CurrentStateSignatures);
		LastAttachmentFeedbackStateSockets = MoveTemp(CurrentStateSockets);
		bAttachmentFeedbackStateInitialized = true;
		return;
	}

	if (CurrentHash == LastAttachmentFeedbackStateHash)
	{
		return;
	}

	const FName ChangedSocketName = ResolveChangedAttachmentFeedbackSocket(CurrentStateSignatures, CurrentStateSockets);
	LastAttachmentFeedbackStateHash = CurrentHash;
	LastAttachmentFeedbackStateSignatures = MoveTemp(CurrentStateSignatures);
	LastAttachmentFeedbackStateSockets = MoveTemp(CurrentStateSockets);
	if (!ChangedSocketName.IsNone())
	{
		AttachmentFeedbackPreferredSocketName = ChangedSocketName;
	}
	RequestDeferredAttachmentFeedback(nullptr);
}

void AGun::UpdateAttachmentFeedbackStateSnapshot()
{
	LastAttachmentFeedbackStateHash = BuildAttachmentFeedbackStateHash(
		&LastAttachmentFeedbackStateSignatures,
		&LastAttachmentFeedbackStateSockets);
	bAttachmentFeedbackStateInitialized = true;
}

uint32 AGun::BuildAttachmentFeedbackStateHash(
	TMap<FName, FString>* OutStateSignatures,
	TMap<FName, FName>* OutStateSockets) const
{
	TMap<FName, FString> StateSignatures;
	TMap<FName, FName> StateSockets;
	const auto AddState = [this, &StateSignatures, &StateSockets](
		const FName Key,
		const FString& Context,
		const FString& MeshPath,
		const FName SocketName,
		const bool bVisible)
	{
		const FName FeedbackSocketName = ResolveAttachmentFeedbackSocketFromContext(Context, SocketName);
		StateSignatures.Add(
			Key,
			FString::Printf(
				TEXT("Mesh=%s;Socket=%s;Visible=%d"),
				*MeshPath,
				*FeedbackSocketName.ToString(),
				bVisible ? 1 : 0));
		StateSockets.Add(Key, FeedbackSocketName);
	};

	TInlineComponentArray<UStaticMeshComponent*> StaticMeshComponents(this);
	for (const UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
	{
		if (!IsValid(StaticMeshComponent) || !IsWeaponAttachmentMesh(StaticMeshComponent))
		{
			continue;
		}

		const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
		const FString MeshPath = StaticMesh ? StaticMesh->GetPathName() : FString();
		const FName Key(*FString::Printf(TEXT("Component:%s"), *StaticMeshComponent->GetName()));
		const FString Context = FString::Printf(
			TEXT("Component:%s %s %s"),
			*StaticMeshComponent->GetName(),
			*MeshPath,
			*StaticMeshComponent->GetAttachSocketName().ToString());
		FName SocketName = StaticMeshComponent->GetAttachSocketName();
		if (const USceneComponent* AttachParent = StaticMeshComponent->GetAttachParent())
		{
			SocketName = ResolveCompatibleWeaponAttachmentSocketName(AttachParent, SocketName);
		}

		AddState(Key, Context, MeshPath, SocketName, StaticMeshComponent->IsVisible());
	}

	for (TFieldIterator<FStructProperty> It(GetClass()); It; ++It)
	{
		const FStructProperty* StructProperty = *It;
		if (!StructProperty)
		{
			continue;
		}

		const FString PropertyName = StructProperty->GetName();
		const bool bAttachmentDataProperty =
			PropertyName.Contains(TEXT("Muzzle"), ESearchCase::IgnoreCase)
			|| PropertyName.Contains(TEXT("Optic"), ESearchCase::IgnoreCase)
			|| PropertyName.Contains(TEXT("SideRail"), ESearchCase::IgnoreCase)
			|| PropertyName.Contains(TEXT("Underbarrel"), ESearchCase::IgnoreCase);
		if (!bAttachmentDataProperty)
		{
			continue;
		}

		const void* StructValue = StructProperty->ContainerPtrToValuePtr<void>(this);
		const UStaticMesh* StaticMesh = ReadStaticMeshFieldByPrefix(StructProperty, StructValue, TEXT("Mesh"));
		const FName SocketName = ReadSocketFieldByPrefix(StructProperty, StructValue, TEXT("Socket"));
		const FString MeshPath = StaticMesh ? StaticMesh->GetPathName() : FString();
		const FName Key(*FString::Printf(TEXT("Property:%s"), *PropertyName));
		const FString Context = FString::Printf(TEXT("Property:%s %s"), *PropertyName, *MeshPath);

		AddState(Key, Context, MeshPath, SocketName, true);
	}

	uint32 Hash = 0;
	TArray<FName> Keys;
	StateSignatures.GetKeys(Keys);
	Keys.Sort([](const FName& Left, const FName& Right)
	{
		return Left.ToString() < Right.ToString();
	});

	for (const FName Key : Keys)
	{
		Hash = HashCombine(Hash, GetTypeHash(Key.ToString()));
		Hash = HashCombine(Hash, GetTypeHash(StateSignatures.FindRef(Key)));
	}

	if (OutStateSignatures)
	{
		*OutStateSignatures = MoveTemp(StateSignatures);
	}

	if (OutStateSockets)
	{
		*OutStateSockets = MoveTemp(StateSockets);
	}

	return Hash;
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

	const FName ResolvedSocketName = ResolveCompatibleWeaponAttachmentSocketName(AttachParent, AttachSocketName);
	return !AttachParent->DoesSocketExist(ResolvedSocketName);
}

bool AGun::IsWeaponAttachmentMesh(const UStaticMeshComponent* Component)
{
	const UStaticMesh* StaticMesh = Component ? Component->GetStaticMesh() : nullptr;
	return StaticMesh && StaticMesh->GetPathName().Contains(WeaponAttachmentMeshPathToken);
}

bool AGun::IsAcogOpticMesh(const UStaticMeshComponent* Component)
{
	const UStaticMesh* StaticMesh = Component ? Component->GetStaticMesh() : nullptr;
	return StaticMesh && StaticMesh->GetPathName().Contains(AcogMeshPathToken, ESearchCase::IgnoreCase);
}

bool AGun::IsLikelyOpticComponent(const UStaticMeshComponent* Component)
{
	if (!Component)
	{
		return false;
	}

	const UStaticMesh* StaticMesh = Component->GetStaticMesh();
	const FString MeshPath = StaticMesh ? StaticMesh->GetPathName() : FString();
	const FString ComponentName = Component->GetName();
	const FString AttachSocketName = Component->GetAttachSocketName().ToString();

	if (MeshPath.Contains(TEXT("Laser"), ESearchCase::IgnoreCase)
		|| ComponentName.Contains(TEXT("Laser"), ESearchCase::IgnoreCase))
	{
		return false;
	}

	return MeshPath.Contains(WeaponAttachmentMeshPathToken)
		|| MeshPath.Contains(TEXT("/Optics/"), ESearchCase::IgnoreCase)
		|| ComponentName.Contains(TEXT("Optic"), ESearchCase::IgnoreCase)
		|| ComponentName.Contains(TEXT("Scope"), ESearchCase::IgnoreCase)
		|| ComponentName.Contains(TEXT("Sight"), ESearchCase::IgnoreCase)
		|| ComponentName.Contains(TEXT("RDS"), ESearchCase::IgnoreCase)
		|| AttachSocketName.Contains(TEXT("Optic"), ESearchCase::IgnoreCase)
		|| AttachSocketName.Contains(TEXT("Sight"), ESearchCase::IgnoreCase);
}

bool AGun::IsLikelySecondaryOpticComponent(const UStaticMeshComponent* Component)
{
	if (!Component)
	{
		return false;
	}

	const UStaticMesh* StaticMesh = Component->GetStaticMesh();
	const FString MeshPath = StaticMesh ? StaticMesh->GetPathName() : FString();
	const FString ComponentName = Component->GetName();
	const FString AttachSocketName = Component->GetAttachSocketName().ToString();

	return AttachSocketName.Equals(TEXT("AT_Backup"), ESearchCase::IgnoreCase)
		|| AttachSocketName.Contains(TEXT("Backup"), ESearchCase::IgnoreCase)
		|| AttachSocketName.Contains(TEXT("Canted"), ESearchCase::IgnoreCase)
		|| AttachSocketName.Contains(TEXT("Side"), ESearchCase::IgnoreCase)
		|| ComponentName.Contains(TEXT("Backup"), ESearchCase::IgnoreCase)
		|| ComponentName.Contains(TEXT("Canted"), ESearchCase::IgnoreCase)
		|| ComponentName.Contains(TEXT("Side"), ESearchCase::IgnoreCase)
		|| ComponentName.Contains(TEXT("Offset"), ESearchCase::IgnoreCase)
		|| MeshPath.Contains(TEXT("Backup"), ESearchCase::IgnoreCase)
		|| MeshPath.Contains(TEXT("Canted"), ESearchCase::IgnoreCase)
		|| MeshPath.Contains(TEXT("Side"), ESearchCase::IgnoreCase)
		|| MeshPath.Contains(TEXT("Offset"), ESearchCase::IgnoreCase)
		|| MeshPath.Contains(TEXT("45"), ESearchCase::IgnoreCase);
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
		|| FunctionName.Contains(TEXT("Underbarrel"))
		|| FunctionName.Contains(TEXT("Muzzle"))
		|| FunctionName.Contains(TEXT("SideRail"))
		|| FunctionName.Contains(TEXT("Optic"))
		|| FunctionName.Contains(TEXT("Sight"));
}

int32 AGun::SynchronizeUnderbarrelAttachmentComponent()
{
	const FStructProperty* UnderbarrelDataProperty = FindFProperty<FStructProperty>(GetClass(), UnderbarrelDataPropertyName);
	if (!UnderbarrelDataProperty)
	{
		return 0;
	}

	USkeletalMeshComponent* MainMesh = ResolveMainSkeletalMesh();
	if (!MainMesh)
	{
		return 0;
	}

	const void* UnderbarrelData = UnderbarrelDataProperty->ContainerPtrToValuePtr<void>(this);
	const UStaticMesh* DesiredMesh = ReadStaticMeshFieldByPrefix(UnderbarrelDataProperty, UnderbarrelData, TEXT("Mesh"));
	FName DesiredSocketName = ReadSocketFieldByPrefix(UnderbarrelDataProperty, UnderbarrelData, TEXT("Socket"));
	if (DesiredSocketName.IsNone())
	{
		DesiredSocketName = UnderbarrelSocketName;
	}

	int32 DestroyedCount = 0;
	bool bKeptDesiredComponent = false;
	TInlineComponentArray<UStaticMeshComponent*> StaticMeshComponents(this);
	for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
	{
		if (!IsValid(StaticMeshComponent) || !IsWeaponAttachmentMesh(StaticMeshComponent))
		{
			continue;
		}

		if (StaticMeshComponent->GetAttachParent() != MainMesh)
		{
			continue;
		}

		const FName AttachSocketName = StaticMeshComponent->GetAttachSocketName();
		if (AttachSocketName != DesiredSocketName && !IsUnderbarrelSocketName(AttachSocketName))
		{
			continue;
		}

		const UStaticMesh* CurrentMesh = StaticMeshComponent->GetStaticMesh();
		const bool bMatchesDesiredMesh = DesiredMesh && CurrentMesh == DesiredMesh;
		if (bMatchesDesiredMesh && !bKeptDesiredComponent)
		{
			bKeptDesiredComponent = true;
			continue;
		}

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Destroying stale underbarrel attachment component '%s' on '%s'. CurrentMesh='%s', DesiredMesh='%s', Socket='%s'."),
			*StaticMeshComponent->GetName(),
			*GetName(),
			CurrentMesh ? *CurrentMesh->GetPathName() : TEXT("None"),
			DesiredMesh ? *DesiredMesh->GetPathName() : TEXT("None"),
			*AttachSocketName.ToString());

		StaticMeshComponent->DestroyComponent();
		++DestroyedCount;
	}

	return DestroyedCount;
}

int32 AGun::SynchronizeAcogRenderComponents()
{
	UStaticMeshComponent* AcogOpticComponent = ResolveAcogOpticComponent();
	if (!IsValid(AcogOpticComponent)
		|| !AcogOpticComponent->DoesSocketExist(AcogRenderDiscSocketName)
		|| !AcogOpticComponent->DoesSocketExist(AcogGlassSocketName))
	{
		DestroyAcogRenderComponents();
		return 0;
	}

	UStaticMesh* RenderDiscMesh = LoadObject<UStaticMesh>(nullptr, AcogRenderDiscMeshPath);
	UStaticMesh* GlassMesh = LoadObject<UStaticMesh>(nullptr, AcogGlassMeshPath);
	UMaterialInterface* RenderMaterial = LoadObject<UMaterialInterface>(nullptr, AcogRenderMaterialPath);
	UMaterialInterface* GlassMaterial = LoadObject<UMaterialInterface>(nullptr, AcogGlassMaterialPath);
	if (!RenderDiscMesh || !GlassMesh || !RenderMaterial || !GlassMaterial)
	{
		DestroyAcogRenderComponents();
		return 0;
	}

	auto EnsureComponent = [this](TObjectPtr<UStaticMeshComponent>& Component, const FName ComponentName)
		-> UStaticMeshComponent*
	{
		if (IsValid(Component))
		{
			return Component.Get();
		}

		UStaticMeshComponent* NewComponent = NewObject<UStaticMeshComponent>(this, ComponentName);
		if (!NewComponent)
		{
			return nullptr;
		}

		NewComponent->CreationMethod = EComponentCreationMethod::Instance;
		NewComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		NewComponent->SetGenerateOverlapEvents(false);
		NewComponent->SetCastShadow(false);
		AddInstanceComponent(NewComponent);
		NewComponent->RegisterComponent();
		Component = NewComponent;
		return NewComponent;
	};

	UStaticMeshComponent* RenderDiscComponent = EnsureComponent(AcogRenderDiscComponent, AcogRenderDiscComponentName);
	UStaticMeshComponent* GlassComponent = EnsureComponent(AcogGlassComponent, AcogGlassComponentName);
	if (!RenderDiscComponent || !GlassComponent)
	{
		DestroyAcogRenderComponents();
		return 0;
	}

	RenderDiscComponent->SetMobility(AcogOpticComponent->Mobility);
	RenderDiscComponent->SetStaticMesh(RenderDiscMesh);
	RenderDiscComponent->SetMaterial(0, RenderMaterial);
	RenderDiscComponent->AttachToComponent(
		AcogOpticComponent,
		FAttachmentTransformRules::SnapToTargetIncludingScale,
		AcogRenderDiscSocketName);
	RenderDiscComponent->SetRelativeTransform(FTransform::Identity);

	GlassComponent->SetMobility(AcogOpticComponent->Mobility);
	GlassComponent->SetStaticMesh(GlassMesh);
	GlassComponent->SetMaterial(0, GlassMaterial);
	GlassComponent->AttachToComponent(
		AcogOpticComponent,
		FAttachmentTransformRules::SnapToTargetIncludingScale,
		AcogGlassSocketName);
	GlassComponent->SetRelativeTransform(FTransform::Identity);

	const bool bVisible = AcogOpticComponent->IsVisible();
	RenderDiscComponent->SetVisibility(bVisible, true);
	GlassComponent->SetVisibility(bVisible, true);
	UpdateAcogMaterialParameterCollection();
	bAcogRenderTickActive = true;
	RefreshActorTickEnabled();
	return 2;
}

void AGun::DestroyAcogRenderComponents()
{
	if (IsValid(AcogRenderDiscComponent))
	{
		AcogRenderDiscComponent->DestroyComponent();
	}
	AcogRenderDiscComponent = nullptr;

	if (IsValid(AcogGlassComponent))
	{
		AcogGlassComponent->DestroyComponent();
	}
	AcogGlassComponent = nullptr;

	bAcogRenderTickActive = false;
	RefreshActorTickEnabled();
}

void AGun::UpdateAcogMaterialParameterCollection() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UMaterialParameterCollection* Collection =
		LoadObject<UMaterialParameterCollection>(nullptr, AcogMaterialParameterCollectionPath);
	if (!Collection)
	{
		return;
	}

	UMaterialParameterCollectionInstance* CollectionInstance = World->GetParameterCollectionInstance(Collection);
	if (!CollectionInstance)
	{
		return;
	}

	float CurrentFOV = 90.0f;
	if (const APlayerCameraManager* PlayerCameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		CurrentFOV = PlayerCameraManager->GetFOVAngle();
	}

	CollectionInstance->SetScalarParameterValue(AcogFOVParameterName, CurrentFOV);
}

void AGun::RefreshActorTickEnabled()
{
	SetActorTickEnabled(bAcogRenderTickActive || bCustomizationSkinPreviewCycleActive);
}

bool AGun::IsCustomizationSkinPreviewAllowed() const
{
	UWorld* World = GetWorld();
	if (HasAnyFlags(RF_ClassDefaultObject) || !World)
	{
		return false;
	}

	const FString MapName = World->GetMapName();
	const FString WorldPath = World->GetPathName();
	if (MapName.Contains(TEXT("Map_MainMenu"), ESearchCase::IgnoreCase)
		|| WorldPath.Contains(TEXT("/MP_System_V3/Maps/Map_MainMenu"), ESearchCase::IgnoreCase)
		|| WorldPath.Contains(TEXT("MainMenu"), ESearchCase::IgnoreCase))
	{
		return true;
	}

	const AGameModeBase* GameMode = UGameplayStatics::GetGameMode(this);
	const UClass* GameModeClass = GameMode ? GameMode->GetClass() : nullptr;
	if (!GameModeClass)
	{
		return false;
	}

	const FString GameModePath = GameModeClass->GetPathName();
	return GameModePath.Contains(TEXT("/MainMenuPawn/GM_Menu."), ESearchCase::IgnoreCase)
		|| GameModePath.Contains(TEXT("GM_Menu_C"), ESearchCase::IgnoreCase);
}

void AGun::StartCustomizationSkinPreviewCycle()
{
	StopCustomizationSkinPreviewCycle();

	if (!IsCustomizationSkinPreviewAllowed())
	{
		return;
	}

	ApplyFakeMode();
	if (!TryInitializeCustomizationSkinPreviewCycle())
	{
		return;
	}

	CustomizationSkinPreviewElapsedSeconds = 0.0f;
	bCustomizationSkinPreviewCycleActive = true;
	ApplyCustomizationSkinPreviewAlpha(0.0f);
	RefreshActorTickEnabled();
}

void AGun::StopCustomizationSkinPreviewCycle()
{
	const int32 EntryCount = CustomizationSkinPreviewComponents.Num();
	for (int32 EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
	{
		USkeletalMeshComponent* Component = CustomizationSkinPreviewComponents[EntryIndex].Get();
		const int32 MaterialIndex = CustomizationSkinPreviewMaterialIndices.IsValidIndex(EntryIndex)
			? CustomizationSkinPreviewMaterialIndices[EntryIndex]
			: INDEX_NONE;
		UMaterialInterface* BaseMaterial = CustomizationSkinPreviewBaseMaterials.IsValidIndex(EntryIndex)
			? CustomizationSkinPreviewBaseMaterials[EntryIndex].Get()
			: nullptr;

		if (IsValid(Component) && MaterialIndex >= 0 && BaseMaterial)
		{
			Component->SetMaterial(MaterialIndex, BaseMaterial);
		}
	}

	CustomizationSkinPreviewComponents.Reset();
	CustomizationSkinPreviewMaterialIndices.Reset();
	CustomizationSkinPreviewBaseMaterials.Reset();
	CustomizationSkinPreviewSkinMaterials.Reset();
	CustomizationSkinPreviewDynamicMaterials.Reset();
	CustomizationSkinPreviewElapsedSeconds = 0.0f;
	bCustomizationSkinPreviewCycleActive = false;
	RefreshActorTickEnabled();
}

bool AGun::TryInitializeCustomizationSkinPreviewCycle()
{
	USkeletalMeshComponent* PreviewMesh = ResolveCustomizationSkinPreviewMesh();
	if (!IsValid(PreviewMesh))
	{
		return false;
	}

	const int32 MaterialCount = PreviewMesh->GetNumMaterials();
	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		UMaterialInterface* BaseMaterial = PreviewMesh->GetMaterial(MaterialIndex);
		UMaterialInterface* SkinMaterial = ResolveCustomizationSkinPreviewMaterial(BaseMaterial);
		if (!BaseMaterial || !SkinMaterial || BaseMaterial == SkinMaterial)
		{
			continue;
		}

		UMaterialInstanceDynamic* DynamicMaterial = UMaterialInstanceDynamic::Create(SkinMaterial, this);
		if (!DynamicMaterial)
		{
			continue;
		}

		CustomizationSkinPreviewComponents.Add(PreviewMesh);
		CustomizationSkinPreviewMaterialIndices.Add(MaterialIndex);
		CustomizationSkinPreviewBaseMaterials.Add(BaseMaterial);
		CustomizationSkinPreviewSkinMaterials.Add(SkinMaterial);
		CustomizationSkinPreviewDynamicMaterials.Add(DynamicMaterial);
	}

	return CustomizationSkinPreviewDynamicMaterials.Num() > 0;
}

void AGun::UpdateCustomizationSkinPreviewCycle(const float DeltaSeconds)
{
	if (!bCustomizationSkinPreviewCycleActive)
	{
		return;
	}

	if (!IsCustomizationSkinPreviewAllowed())
	{
		StopCustomizationSkinPreviewCycle();
		return;
	}

	constexpr float SafeBlendSeconds = CustomizationSkinPreviewBlendSeconds > UE_SMALL_NUMBER
		? CustomizationSkinPreviewBlendSeconds
		: UE_SMALL_NUMBER;
	const float SegmentSeconds = CustomizationSkinPreviewHoldSeconds + SafeBlendSeconds;
	const float CycleSeconds = SegmentSeconds * 2.0f;

	CustomizationSkinPreviewElapsedSeconds += FMath::Max(DeltaSeconds, 0.0f);
	const float Phase = FMath::Fmod(CustomizationSkinPreviewElapsedSeconds, CycleSeconds);

	float Alpha = 0.0f;
	if (Phase < CustomizationSkinPreviewHoldSeconds)
	{
		Alpha = 0.0f;
	}
	else if (Phase < SegmentSeconds)
	{
		Alpha = (Phase - CustomizationSkinPreviewHoldSeconds) / SafeBlendSeconds;
	}
	else if (Phase < SegmentSeconds + CustomizationSkinPreviewHoldSeconds)
	{
		Alpha = 1.0f;
	}
	else
	{
		Alpha = 1.0f - ((Phase - SegmentSeconds - CustomizationSkinPreviewHoldSeconds) / SafeBlendSeconds);
	}

	ApplyCustomizationSkinPreviewAlpha(FMath::Clamp(Alpha, 0.0f, 1.0f));
}

void AGun::ApplyCustomizationSkinPreviewAlpha(const float Alpha)
{
	const bool bUseBaseMaterial = Alpha <= UE_KINDA_SMALL_NUMBER;
	const bool bUseSkinMaterial = Alpha >= 1.0f - UE_KINDA_SMALL_NUMBER;
	const int32 EntryCount = CustomizationSkinPreviewComponents.Num();

	for (int32 EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
	{
		USkeletalMeshComponent* Component = CustomizationSkinPreviewComponents[EntryIndex].Get();
		const int32 MaterialIndex = CustomizationSkinPreviewMaterialIndices.IsValidIndex(EntryIndex)
			? CustomizationSkinPreviewMaterialIndices[EntryIndex]
			: INDEX_NONE;
		UMaterialInterface* BaseMaterial = CustomizationSkinPreviewBaseMaterials.IsValidIndex(EntryIndex)
			? CustomizationSkinPreviewBaseMaterials[EntryIndex].Get()
			: nullptr;
		UMaterialInterface* SkinMaterial = CustomizationSkinPreviewSkinMaterials.IsValidIndex(EntryIndex)
			? CustomizationSkinPreviewSkinMaterials[EntryIndex].Get()
			: nullptr;
		UMaterialInstanceDynamic* DynamicMaterial = CustomizationSkinPreviewDynamicMaterials.IsValidIndex(EntryIndex)
			? CustomizationSkinPreviewDynamicMaterials[EntryIndex].Get()
			: nullptr;

		if (!IsValid(Component) || MaterialIndex < 0 || !BaseMaterial || !SkinMaterial || !DynamicMaterial)
		{
			continue;
		}

		UMaterialInterface* DesiredMaterial = DynamicMaterial;
		if (bUseBaseMaterial)
		{
			DesiredMaterial = BaseMaterial;
		}
		else if (bUseSkinMaterial)
		{
			DesiredMaterial = SkinMaterial;
		}
		else
		{
			if (UMaterialInstance* BaseInstance = Cast<UMaterialInstance>(BaseMaterial))
			{
				if (UMaterialInstance* SkinInstance = Cast<UMaterialInstance>(SkinMaterial))
				{
					DynamicMaterial->K2_InterpolateMaterialInstanceParams(BaseInstance, SkinInstance, Alpha);
				}
			}

			for (const FName ParameterName : CustomizationSkinPreviewAlphaParameterNames)
			{
				DynamicMaterial->SetScalarParameterValue(ParameterName, Alpha);
			}
		}

		if (Component->GetMaterial(MaterialIndex) != DesiredMaterial)
		{
			Component->SetMaterial(MaterialIndex, DesiredMaterial);
		}
	}
}

USkeletalMeshComponent* AGun::ResolveCustomizationSkinPreviewMesh() const
{
	if (IsValid(FakeSkeletalMeshComponent)
		&& FakeSkeletalMeshComponent->IsVisible()
		&& FakeSkeletalMeshComponent->GetSkeletalMeshAsset())
	{
		return FakeSkeletalMeshComponent;
	}

	USkeletalMeshComponent* MainMesh = ResolveMainSkeletalMesh();
	return IsValid(MainMesh) ? MainMesh : nullptr;
}

UMaterialInterface* AGun::ResolveCustomizationSkinPreviewMaterial(const UMaterialInterface* BaseMaterial) const
{
	if (!BaseMaterial)
	{
		return nullptr;
	}

	const FString PackageName = BaseMaterial->GetOutermost() ? BaseMaterial->GetOutermost()->GetName() : FString();
	const FString AssetName = BaseMaterial->GetName();
	FString DirectoryName;
	if (!PackageName.Split(TEXT("/"), &DirectoryName, nullptr, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
	{
		return nullptr;
	}

	TArray<FString> CandidateAssetNames;
	auto AddCandidateBySuffix = [&CandidateAssetNames, &AssetName](const TCHAR* Suffix, const TCHAR* Replacement)
	{
		if (!AssetName.EndsWith(Suffix, ESearchCase::IgnoreCase))
		{
			return;
		}

		FString CandidateName = AssetName;
		CandidateName.LeftChopInline(FCString::Strlen(Suffix), EAllowShrinking::No);
		CandidateName += Replacement;
		CandidateAssetNames.AddUnique(CandidateName);
	};

	AddCandidateBySuffix(TEXT("_Skin_Mat_Inst"), TEXT("_Mat_Inst"));
	AddCandidateBySuffix(TEXT("_Color_Mat_Inst"), TEXT("_Skin_Mat_Inst"));
	AddCandidateBySuffix(TEXT("_Mat_Inst"), TEXT("_Skin_Mat_Inst"));
	AddCandidateBySuffix(TEXT("_Mat"), TEXT("_Skin_Mat_Inst"));

	for (const FString& CandidateAssetName : CandidateAssetNames)
	{
		const FString CandidatePackageName = DirectoryName + TEXT("/") + CandidateAssetName;
		if (CandidatePackageName.Equals(PackageName, ESearchCase::IgnoreCase))
		{
			continue;
		}

		const FString CandidateObjectPath = CandidatePackageName + TEXT(".") + CandidateAssetName;
		if (UMaterialInterface* CandidateMaterial = LoadObject<UMaterialInterface>(nullptr, *CandidateObjectPath))
		{
			return CandidateMaterial;
		}
	}

	return nullptr;
}

void AGun::ApplyFakeMode()
{
	USkeletalMeshComponent* MainMesh = ResolveMainSkeletalMesh();
	if (!MainMesh)
	{
		if (IsTouchMeRuntimeTraceEnabled())
		{
			UE_LOG(
				LogTouchMeRuntimeTrace,
				Warning,
				TEXT("[GunApplyFakeMode] Gun=%s no Item skeletal mesh. FakeMode=%d"),
				*GetPathName(),
				bFakeMode ? 1 : 0);
		}

		RestoreFromFakeMode();
		return;
	}

	const bool bCanApplyFakeMode = bFakeMode
		&& IsValid(FakeSkeletalMesh)
		&& FakeAnimInstanceClass != nullptr;

	if (IsTouchMeRuntimeTraceEnabled())
	{
		UE_LOG(
			LogTouchMeRuntimeTrace,
			Warning,
			TEXT("[GunApplyFakeMode] Gun=%s CanApply=%d FakeMode=%d FakeMeshAsset=%s FakeAnimClass=%s MainMesh={%s Anim=%s Asset=%s}"),
			*GetPathName(),
			bCanApplyFakeMode ? 1 : 0,
			bFakeMode ? 1 : 0,
			FakeSkeletalMesh ? *FakeSkeletalMesh->GetPathName() : TEXT("None"),
			FakeAnimInstanceClass ? *FakeAnimInstanceClass->GetPathName() : TEXT("None"),
			*DescribeSceneComponent(MainMesh),
			*DescribeAnimClass(MainMesh),
			MainMesh->GetSkeletalMeshAsset() ? *MainMesh->GetSkeletalMeshAsset()->GetPathName() : TEXT("None"));
	}

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

		const bool bCanAttachFakeMesh = IsValid(FakeAttachedSkeletalMesh)
			&& !FakeAttachedSkeletalMeshSocketName.IsNone();

		if (bCanAttachFakeMesh)
		{
			FakeAttachedSkeletalMeshComponent->AttachToComponent(
				FakeSkeletalMeshComponent,
				FAttachmentTransformRules::SnapToTargetIncludingScale,
				FakeAttachedSkeletalMeshSocketName);
			FakeAttachedSkeletalMeshComponent->SetRelativeTransform(FakeAttachedSkeletalMeshOffset);
			FakeAttachedSkeletalMeshComponent->SetSkeletalMeshAsset(FakeAttachedSkeletalMesh);
			FakeAttachedSkeletalMeshComponent->SetVisibility(true, false);
		}
		else
		{
			FakeAttachedSkeletalMeshComponent->SetVisibility(false, false);
			FakeAttachedSkeletalMeshComponent->SetSkeletalMeshAsset(nullptr);
			FakeAttachedSkeletalMeshComponent->AttachToComponent(
				FakeSkeletalMeshComponent,
				FAttachmentTransformRules::SnapToTargetIncludingScale);
		}

		bFakeModeApplied = true;
		return;
	}

	RestoreFromFakeMode();
}

void AGun::RestoreFromFakeMode()
{
	FakeAttachedSkeletalMeshComponent->SetVisibility(false, false);
	FakeAttachedSkeletalMeshComponent->SetSkeletalMeshAsset(nullptr);
	FakeAttachedSkeletalMeshComponent->AttachToComponent(
		FakeSkeletalMeshComponent,
		FAttachmentTransformRules::SnapToTargetIncludingScale);

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

bool AGun::ResolveADSSocketAttachTarget(USceneComponent*& OutParent, FName& OutSocketName) const
{
	OutParent = nullptr;
	OutSocketName = NAME_None;
	USkeletalMeshComponent* MainMesh = ResolveMainSkeletalMesh();

	if (bUseSecondaryADSSocket)
	{
		if (UStaticMeshComponent* SecondaryOpticComponent = ResolveSecondaryOpticComponent())
		{
			OutParent = SecondaryOpticComponent;
			OutSocketName = ResolveOpticADSSocket(SecondaryOpticComponent, SecondaryOpticADSSocketName);
			return true;
		}

		if (MainMesh)
		{
			const FName SecondaryWeaponSocketName = ResolveSecondaryWeaponADSSocket(MainMesh);
			if (!SecondaryWeaponSocketName.IsNone())
			{
				OutParent = MainMesh;
				OutSocketName = SecondaryWeaponSocketName;
				return true;
			}
		}
	}

	if (bFakeMode && MainMesh)
	{
		const FName WeaponSocketName = ResolveWeaponADSSocket(MainMesh);
		if (!WeaponSocketName.IsNone())
		{
			OutParent = MainMesh;
			OutSocketName = WeaponSocketName;
			return true;
		}
	}

	if (UStaticMeshComponent* OpticComponent = ResolvePrimaryOpticComponent())
	{
		OutParent = OpticComponent;
		OutSocketName = ResolveOpticADSSocket(OpticComponent, OpticADSSocketName);
		return true;
	}

	if (!MainMesh)
	{
		return false;
	}

	const FName WeaponSocketName = ResolveWeaponADSSocket(MainMesh);
	OutParent = MainMesh;
	OutSocketName = WeaponSocketName;
	return !WeaponSocketName.IsNone();
}

UStaticMeshComponent* AGun::ResolvePrimaryOpticComponent() const
{
	const USkeletalMeshComponent* MainMesh = ResolveMainSkeletalMesh();
	UStaticMeshComponent* BestComponent = nullptr;
	int32 BestScore = MIN_int32;

	TInlineComponentArray<UStaticMeshComponent*> StaticMeshes(this);
	for (UStaticMeshComponent* StaticMeshComponent : StaticMeshes)
	{
		if (!IsValid(StaticMeshComponent) || !IsLikelyOpticComponent(StaticMeshComponent))
		{
			continue;
		}

		if (IsLikelySecondaryOpticComponent(StaticMeshComponent)
			&& StaticMeshComponent->GetAttachSocketName() == WeaponSecondaryOpticsSocketName)
		{
			continue;
		}

		int32 Score = 0;
		const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
		const FString MeshPath = StaticMesh ? StaticMesh->GetPathName() : FString();
		const FString ComponentName = StaticMeshComponent->GetName();
		const FName AttachSocketName = StaticMeshComponent->GetAttachSocketName();

		if (StaticMeshComponent->IsVisible())
		{
			Score += 50;
		}

		if (StaticMeshComponent->GetAttachParent() == MainMesh)
		{
			Score += 30;
		}

		if (MeshPath.Contains(WeaponAttachmentMeshPathToken))
		{
			Score += 100;
		}

		if (!WeaponOpticsSocketName.IsNone() && AttachSocketName == WeaponOpticsSocketName)
		{
			Score += 40;
		}

		if (!OpticADSSocketName.IsNone() && StaticMeshComponent->DoesSocketExist(OpticADSSocketName))
		{
			Score += 25;
		}

		if (MeshPath.Contains(TEXT("Laser"), ESearchCase::IgnoreCase)
			|| ComponentName.Contains(TEXT("Laser"), ESearchCase::IgnoreCase))
		{
			Score -= 200;
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			BestComponent = StaticMeshComponent;
		}
	}

	return BestComponent;
}

UStaticMeshComponent* AGun::ResolveSecondaryOpticComponent() const
{
	const USkeletalMeshComponent* MainMesh = ResolveMainSkeletalMesh();
	UStaticMeshComponent* BestComponent = nullptr;
	int32 BestScore = MIN_int32;

	TInlineComponentArray<UStaticMeshComponent*> StaticMeshes(this);
	for (UStaticMeshComponent* StaticMeshComponent : StaticMeshes)
	{
		if (!IsValid(StaticMeshComponent) || !IsLikelyOpticComponent(StaticMeshComponent))
		{
			continue;
		}

		int32 Score = 0;
		const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
		const FString MeshPath = StaticMesh ? StaticMesh->GetPathName() : FString();
		const FName AttachSocketName = StaticMeshComponent->GetAttachSocketName();

		if (StaticMeshComponent->IsVisible())
		{
			Score += 50;
		}

		if (StaticMeshComponent->GetAttachParent() == MainMesh)
		{
			Score += 30;
		}

		if (MeshPath.Contains(WeaponAttachmentMeshPathToken))
		{
			Score += 100;
		}

		if (!WeaponSecondaryOpticsSocketName.IsNone() && AttachSocketName == WeaponSecondaryOpticsSocketName)
		{
			Score += 120;
		}

		if (IsLikelySecondaryOpticComponent(StaticMeshComponent))
		{
			Score += 80;
		}

		if (!SecondaryOpticADSSocketName.IsNone() && StaticMeshComponent->DoesSocketExist(SecondaryOpticADSSocketName))
		{
			Score += 25;
		}

		if (!WeaponOpticsSocketName.IsNone() && AttachSocketName == WeaponOpticsSocketName)
		{
			Score -= 20;
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			BestComponent = StaticMeshComponent;
		}
	}

	return BestComponent;
}

UStaticMeshComponent* AGun::ResolveAcogOpticComponent() const
{
	UStaticMeshComponent* PrimaryOpticComponent = ResolvePrimaryOpticComponent();
	if (IsValid(PrimaryOpticComponent) && IsAcogOpticMesh(PrimaryOpticComponent))
	{
		return PrimaryOpticComponent;
	}

	const USkeletalMeshComponent* MainMesh = ResolveMainSkeletalMesh();
	UStaticMeshComponent* BestComponent = nullptr;
	int32 BestScore = MIN_int32;

	TInlineComponentArray<UStaticMeshComponent*> StaticMeshes(this);
	for (UStaticMeshComponent* StaticMeshComponent : StaticMeshes)
	{
		if (!IsValid(StaticMeshComponent) || !IsAcogOpticMesh(StaticMeshComponent))
		{
			continue;
		}

		int32 Score = 0;
		const FName AttachSocketName = StaticMeshComponent->GetAttachSocketName();

		if (StaticMeshComponent->IsVisible())
		{
			Score += 50;
		}

		if (StaticMeshComponent->GetAttachParent() == MainMesh)
		{
			Score += 30;
		}

		if (!WeaponOpticsSocketName.IsNone() && AttachSocketName == WeaponOpticsSocketName)
		{
			Score += 40;
		}

		if (AttachSocketName.ToString().Contains(TEXT("Optic"), ESearchCase::IgnoreCase))
		{
			Score += 20;
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			BestComponent = StaticMeshComponent;
		}
	}

	return BestComponent;
}

FName AGun::ResolveOpticADSSocket(const USceneComponent* Component, const FName PreferredSocketName) const
{
	if (!Component)
	{
		return NAME_None;
	}

	if (!PreferredSocketName.IsNone() && Component->DoesSocketExist(PreferredSocketName))
	{
		return PreferredSocketName;
	}

	static const FName CandidateSocketNames[] =
	{
		TEXT("ADS"),
		TEXT("ADS_Aim"),
		TEXT("Aim"),
		TEXT("AimPoint"),
		TEXT("Sight"),
		TEXT("Scope"),
		TEXT("Camera")
	};

	for (const FName CandidateSocketName : CandidateSocketNames)
	{
		if (Component->DoesSocketExist(CandidateSocketName))
		{
			return CandidateSocketName;
		}
	}

	return NAME_None;
}

FName AGun::ResolveWeaponADSSocket(const USkeletalMeshComponent* Mesh) const
{
	if (!Mesh)
	{
		return NAME_None;
	}

	static const FName MPAimTargetSocketName(TEXT("AimTarget"));
	if (Mesh->DoesSocketExist(MPAimTargetSocketName))
	{
		return MPAimTargetSocketName;
	}

	if (!WeaponOpticsSocketName.IsNone() && Mesh->DoesSocketExist(WeaponOpticsSocketName))
	{
		return WeaponOpticsSocketName;
	}

	if (!WeaponIronSightSocketName.IsNone() && Mesh->DoesSocketExist(WeaponIronSightSocketName))
	{
		return WeaponIronSightSocketName;
	}

	static const FName CandidateSocketNames[] =
	{
		TEXT("ADS_Eye"),
		TEXT("ADS"),
		TEXT("Sight"),
		TEXT("Camera"),
		TEXT("Camera_FP")
	};

	for (const FName CandidateSocketName : CandidateSocketNames)
	{
		if (Mesh->DoesSocketExist(CandidateSocketName))
		{
			return CandidateSocketName;
		}
	}

	return NAME_None;
}

FName AGun::ResolveSecondaryWeaponADSSocket(const USkeletalMeshComponent* Mesh) const
{
	if (!Mesh)
	{
		return NAME_None;
	}

	if (!WeaponSecondaryOpticsSocketName.IsNone() && Mesh->DoesSocketExist(WeaponSecondaryOpticsSocketName))
	{
		return WeaponSecondaryOpticsSocketName;
	}

	static const FName CandidateSocketNames[] =
	{
		TEXT("Canted"),
		TEXT("CantedSight"),
		TEXT("SideSight"),
		TEXT("BackupSight"),
		TEXT("Backup"),
		TEXT("ADS_Secondary"),
		TEXT("ADS_Canted"),
		TEXT("ADS_Eye_Secondary")
	};

	for (const FName CandidateSocketName : CandidateSocketNames)
	{
		if (Mesh->DoesSocketExist(CandidateSocketName))
		{
			return CandidateSocketName;
		}
	}

	return NAME_None;
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
			UParticleSystemComponent* ParticleComponent = UGameplayStatics::SpawnEmitterAtLocation(
				GetWorld(),
				CascadeSystem,
				ParticleTransform,
				true,
				EPSCPoolMethod::None,
				true);
			if (ParticleComponent)
			{
				ParticleComponent->bAutoDestroy = true;
				ScheduleImpactFXCleanup(GetWorld(), ParticleComponent);
			}
		}
		else if (UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(Effects.Particle))
		{
			UNiagaraComponent* NiagaraComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				this,
				NiagaraSystem,
				ParticleTransform.GetLocation(),
				ParticleTransform.Rotator(),
				ParticleTransform.GetScale3D(),
				true,
				true,
				ENCPoolMethod::None,
				true);
			if (NiagaraComponent)
			{
				NiagaraComponent->SetAutoDestroy(true);
				ScheduleImpactFXCleanup(GetWorld(), NiagaraComponent);
			}
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
