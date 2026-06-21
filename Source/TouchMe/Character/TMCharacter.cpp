// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMCharacter.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "UObject/UnrealType.h"
#include "../Gun/Gun.h"
#include "../TMGameplayStatics.h"
#include "../TouchMe.h"

namespace
{
	bool TMIsTraceNoiseFunction(const FString& Name)
	{
		return Name.Contains(TEXT("Tick"))
			|| Name.Contains(TEXT("UpdateAnimation"))
			|| Name.Contains(TEXT("EvaluateGraphExposedInputs"))
			|| Name.Contains(TEXT("AnimGraph"))
			|| Name.Contains(TEXT("BlueprintThreadSafeUpdateAnimation"));
	}

	bool TMIsAimBridgeNoiseName(const FString& UpperName)
	{
		return UpperName.Contains(TEXT("AIMOFFSET"))
			|| UpperName.Contains(TEXT("AIM_OFFSET"))
			|| UpperName.Contains(TEXT("AIMINGROTATION"))
			|| UpperName.Contains(TEXT("AIMING_ROTATION"))
			|| UpperName.Contains(TEXT("AIMYAW"))
			|| UpperName.Contains(TEXT("AIM_YAW"))
			|| UpperName.Contains(TEXT("AIMSWEEP"))
			|| UpperName.Contains(TEXT("AIM_SWEEP"))
			|| UpperName.Contains(TEXT("UPDATEAIM"))
			|| UpperName.Contains(TEXT("UPDATE_AIM"))
			|| UpperName.Contains(TEXT("CALCULATEAIM"))
			|| UpperName.Contains(TEXT("CALCULATE_AIM"))
			|| UpperName.Contains(TEXT("GETAIM"))
			|| UpperName.Contains(TEXT("GET_AIM"));
	}

	bool TMIsAimBridgeFunction(const UFunction* Function)
	{
		if (!Function)
		{
			return false;
		}

		const FString UpperName = Function->GetName().ToUpper();
		if (TMIsAimBridgeNoiseName(UpperName))
		{
			return false;
		}

		return UpperName.Contains(TEXT("AIM"))
			|| UpperName.Contains(TEXT("ADS"))
			|| UpperName.Contains(TEXT("SIGHT"))
			|| UpperName.Contains(TEXT("SCOPE"));
	}

	bool TMIsMPSServerAimFunction(const UFunction* Function)
	{
		return Function && Function->GetName().Equals(TEXT("Svr_Aim"), ESearchCase::CaseSensitive);
	}

	bool TMIsAimStateBoolName(const FString& UpperName)
	{
		if (TMIsAimBridgeNoiseName(UpperName))
		{
			return false;
		}

		return UpperName.Contains(TEXT("AIM"))
			|| UpperName.Contains(TEXT("ADS"))
			|| UpperName.Contains(TEXT("SIGHT"))
			|| UpperName.Contains(TEXT("SCOPE"));
	}

	bool TMReadAimStateBoolProperty(const UObject* Object, bool& bOutAiming)
	{
		if (!Object)
		{
			return false;
		}

		static const FName PriorityPropertyNames[] =
		{
			TEXT("bIsAiming"),
			TEXT("IsAiming"),
			TEXT("bAiming"),
			TEXT("Aiming"),
			TEXT("bADS"),
			TEXT("IsADS"),
			TEXT("bIsADS"),
			TEXT("bAim"),
			TEXT("Aim")
		};

		for (const FName PropertyName : PriorityPropertyNames)
		{
			const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
			if (BoolProperty)
			{
				bOutAiming = BoolProperty->GetPropertyValue_InContainer(Object);
				return true;
			}
		}

		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			const FBoolProperty* BoolProperty = CastField<FBoolProperty>(*It);
			if (!BoolProperty || !TMIsAimStateBoolName(BoolProperty->GetName().ToUpper()))
			{
				continue;
			}

			bOutAiming = BoolProperty->GetPropertyValue_InContainer(Object);
			return true;
		}

		return false;
	}

	bool TMExtractAimStateFromParameters(const UFunction* Function, void* Parameters, bool& bOutAiming)
	{
		if (!Function || !Parameters)
		{
			return false;
		}

		const FBoolProperty* OnlyBoolParameter = nullptr;
		int32 BoolParameterCount = 0;
		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			const FBoolProperty* BoolProperty = CastField<FBoolProperty>(*It);
			if (!BoolProperty
				|| !BoolProperty->HasAnyPropertyFlags(CPF_Parm)
				|| BoolProperty->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				continue;
			}

			++BoolParameterCount;
			OnlyBoolParameter = BoolProperty;

			const FString UpperName = BoolProperty->GetName().ToUpper();
			if (TMIsAimStateBoolName(UpperName)
				|| UpperName.Contains(TEXT("VALUE"))
				|| UpperName.Contains(TEXT("PRESSED"))
				|| UpperName.Contains(TEXT("ACTIVE"))
				|| UpperName.Contains(TEXT("ENABLE"))
				|| UpperName.Contains(TEXT("STATE")))
			{
				bOutAiming = BoolProperty->GetPropertyValue(BoolProperty->ContainerPtrToValuePtr<void>(Parameters));
				return true;
			}
		}

		if (BoolParameterCount == 1 && OnlyBoolParameter)
		{
			bOutAiming = OnlyBoolParameter->GetPropertyValue(OnlyBoolParameter->ContainerPtrToValuePtr<void>(Parameters));
			return true;
		}

		return false;
	}

	bool TMReadNumericProperty(const UObject* Object, const FName PropertyName, double& OutValue)
	{
		if (!Object)
		{
			return false;
		}

		const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!NumericProperty)
		{
			return false;
		}

		const void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(Object);
		OutValue = NumericProperty->IsFloatingPoint()
			? NumericProperty->GetFloatingPointPropertyValue(ValuePtr)
			: static_cast<double>(NumericProperty->GetSignedIntPropertyValue(ValuePtr));
		return true;
	}

	bool TMReadEnumLikeProperty(const UObject* Object, const FName PropertyName, int64& OutValue)
	{
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
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			OutValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
			return true;
		}

		if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			OutValue = ByteProperty->GetPropertyValue(ValuePtr);
			return true;
		}

		if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			OutValue = NumericProperty->IsFloatingPoint()
				? FMath::RoundToInt64(NumericProperty->GetFloatingPointPropertyValue(ValuePtr))
				: NumericProperty->GetSignedIntPropertyValue(ValuePtr);
			return true;
		}

		return false;
	}

	bool TMReadNameProperty(const UObject* Object, const FName PropertyName, FName& OutValue)
	{
		if (!Object)
		{
			return false;
		}

		const FNameProperty* NameProperty = CastField<FNameProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!NameProperty)
		{
			return false;
		}

		OutValue = NameProperty->GetPropertyValue_InContainer(Object);
		return true;
	}

	bool TMReadTransformProperty(const UObject* Object, const FName PropertyName, FTransform& OutValue)
	{
		if (!Object)
		{
			return false;
		}

		const FStructProperty* StructProperty = CastField<FStructProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!StructProperty || StructProperty->Struct != TBaseStructure<FTransform>::Get())
		{
			return false;
		}

		OutValue = *StructProperty->ContainerPtrToValuePtr<FTransform>(Object);
		return true;
	}

	bool TMWriteNumericProperty(UObject* Object, const FName PropertyName, const double Value)
	{
		if (!Object)
		{
			return false;
		}

		FNumericProperty* NumericProperty = CastField<FNumericProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!NumericProperty)
		{
			return false;
		}

		void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(Object);
		if (NumericProperty->IsFloatingPoint())
		{
			NumericProperty->SetFloatingPointPropertyValue(ValuePtr, Value);
		}
		else
		{
			NumericProperty->SetIntPropertyValue(ValuePtr, FMath::RoundToInt64(Value));
		}
		return true;
	}

	bool TMWriteRotatorProperty(UObject* Object, const FName PropertyName, const FRotator& Value)
	{
		if (!Object)
		{
			return false;
		}

		FStructProperty* StructProperty = CastField<FStructProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!StructProperty || StructProperty->Struct != TBaseStructure<FRotator>::Get())
		{
			return false;
		}

		FRotator* ValuePtr = StructProperty->ContainerPtrToValuePtr<FRotator>(Object);
		*ValuePtr = Value;
		return true;
	}

	bool TMWriteVectorProperty(UObject* Object, const FName PropertyName, const FVector& Value)
	{
		if (!Object)
		{
			return false;
		}

		FStructProperty* StructProperty = CastField<FStructProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!StructProperty || StructProperty->Struct != TBaseStructure<FVector>::Get())
		{
			return false;
		}

		FVector* ValuePtr = StructProperty->ContainerPtrToValuePtr<FVector>(Object);
		*ValuePtr = Value;
		return true;
	}

	bool TMWriteTransformProperty(UObject* Object, const FName PropertyName, const FTransform& Value)
	{
		if (!Object)
		{
			return false;
		}

		FStructProperty* StructProperty = CastField<FStructProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!StructProperty || StructProperty->Struct != TBaseStructure<FTransform>::Get())
		{
			return false;
		}

		FTransform* ValuePtr = StructProperty->ContainerPtrToValuePtr<FTransform>(Object);
		*ValuePtr = Value;
		return true;
	}

	bool TMWriteBoolProperty(UObject* Object, const FName PropertyName, const bool bValue)
	{
		if (!Object)
		{
			return false;
		}

		FBoolProperty* BoolProperty = CastField<FBoolProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!BoolProperty)
		{
			return false;
		}

		BoolProperty->SetPropertyValue_InContainer(Object, bValue);
		return true;
	}

	bool TMWriteAlphaLikeProperty(UObject* Object, const FName PropertyName, const double Value)
	{
		if (TMWriteNumericProperty(Object, PropertyName, Value))
		{
			return true;
		}

		return TMWriteBoolProperty(Object, PropertyName, Value > KINDA_SMALL_NUMBER);
	}

	bool TMIsAimingForWeaponBoneLock(const ATMCharacter* Character, const UAnimInstance* AnimInstance)
	{
		static const FName RotationModePropertyName(TEXT("RotationMode"));
		static constexpr int64 ALSRotationModeAimingValue = 2;

		int64 RotationModeValue = 0;
		if ((TMReadEnumLikeProperty(AnimInstance, RotationModePropertyName, RotationModeValue)
				|| TMReadEnumLikeProperty(Character, RotationModePropertyName, RotationModeValue))
			&& RotationModeValue == ALSRotationModeAimingValue)
		{
			return true;
		}

		bool bAiming = false;
		return TMReadAimStateBoolProperty(AnimInstance, bAiming) && bAiming;
	}

	bool TMIsCharacterAimingForCameraWeaponOffset(const ATMCharacter* Character, const UAnimInstance* AnimInstance)
	{
		bool bAiming = false;
		if (TMReadAimStateBoolProperty(Character, bAiming))
		{
			return bAiming;
		}

		return TMIsAimingForWeaponBoneLock(Character, AnimInstance);
	}

	FRotator TMGetCameraFloorLockedRotation(const ATMCharacter* Character)
	{
		FRotator CameraRotation = Character ? Character->GetViewRotation() : FRotator::ZeroRotator;
		CameraRotation.Roll = 0.f;
		return CameraRotation.GetNormalized();
	}

	FRotator TMGetWeaponBoneFloorLockedComponentRotation(
		const ATMCharacter* Character,
		const USkeletalMeshComponent* Mesh,
		const double YawOffsetDegrees)
	{
		if (!Mesh)
		{
			return FRotator::ZeroRotator;
		}

		const FRotator CameraRotation = TMGetCameraFloorLockedRotation(Character);
		const FRotator DesiredWorldRotation(CameraRotation.Pitch, CameraRotation.Yaw + YawOffsetDegrees, 0.f);
		const FQuat DesiredComponentRotation = Mesh->GetComponentQuat().Inverse() * DesiredWorldRotation.Quaternion();
		return DesiredComponentRotation.Rotator().GetNormalized();
	}

	void TMUpdateFabrikFixerAlpha(UAnimInstance* AnimInstance)
	{
		static const FName EnableHandIKLPropertyName(TEXT("Enable_HandIK_L"));
		static const FName FabrikFixerAlphaPropertyName(TEXT("FabrikFixerAlpha"));

		double EnableHandIKL = 0.0;
		if (!TMReadNumericProperty(AnimInstance, EnableHandIKLPropertyName, EnableHandIKL))
		{
			return;
		}

		const double FabrikFixerAlpha = FMath::Clamp(EnableHandIKL, 0.0, 1.0) * 0.5;
		TMWriteNumericProperty(AnimInstance, FabrikFixerAlphaPropertyName, FabrikFixerAlpha);
	}

	void TMUpdateFabrikFixerAlpha(const USkeletalMeshComponent* Mesh)
	{
		if (!Mesh)
		{
			return;
		}

		TMUpdateFabrikFixerAlpha(Mesh->GetAnimInstance());
		for (UAnimInstance* LinkedAnimInstance : Mesh->GetLinkedAnimInstances())
		{
			TMUpdateFabrikFixerAlpha(LinkedAnimInstance);
		}
	}

	void TMUpdateWeaponBoneFloorLock(
		UAnimInstance* AnimInstance,
		const ATMCharacter* Character,
		const USkeletalMeshComponent* Mesh)
	{
		if (!AnimInstance || !Character || !Mesh)
		{
			return;
		}

		static const FName CameraFloorLockedRotationPropertyName(TEXT("CameraFloorLockedRotation"));
		static const FName WeaponBoneFloorLockedRotationPropertyName(TEXT("WeaponBoneFloorLockedRotation"));
		static const FName WeaponBoneFloorLockAlphaPropertyName(TEXT("WeaponBoneFloorLockAlpha"));
		static const FName WeaponBoneFloorLockYawOffsetPropertyName(TEXT("WeaponBoneFloorLockYawOffset"));

		double YawOffsetDegrees = 0.0;
		TMReadNumericProperty(AnimInstance, WeaponBoneFloorLockYawOffsetPropertyName, YawOffsetDegrees);

		const FRotator CameraFloorLockedRotation = TMGetCameraFloorLockedRotation(Character);
		const FRotator WeaponBoneFloorLockedRotation =
			TMGetWeaponBoneFloorLockedComponentRotation(Character, Mesh, YawOffsetDegrees);
		const double Alpha = TMIsAimingForWeaponBoneLock(Character, AnimInstance) ? 1.0 : 0.0;

		TMWriteRotatorProperty(AnimInstance, CameraFloorLockedRotationPropertyName, CameraFloorLockedRotation);
		TMWriteRotatorProperty(AnimInstance, WeaponBoneFloorLockedRotationPropertyName, WeaponBoneFloorLockedRotation);
		TMWriteNumericProperty(AnimInstance, WeaponBoneFloorLockAlphaPropertyName, Alpha);
	}

	void TMUpdateWeaponBoneFloorLock(const ATMCharacter* Character, const USkeletalMeshComponent* Mesh)
	{
		if (!Mesh)
		{
			return;
		}

		TMUpdateWeaponBoneFloorLock(Mesh->GetAnimInstance(), Character, Mesh);
		for (UAnimInstance* LinkedAnimInstance : Mesh->GetLinkedAnimInstances())
		{
			TMUpdateWeaponBoneFloorLock(LinkedAnimInstance, Character, Mesh);
		}
	}

	const AGun* TMGetGunDefaults(const AGun* ActiveGun)
	{
		return ActiveGun ? Cast<AGun>(ActiveGun->GetClass()->GetDefaultObject()) : nullptr;
	}

	FTransform TMGetDefaultCameraWeaponOffset(const AGun* ActiveGun)
	{
		const AGun* DefaultGun = TMGetGunDefaults(ActiveGun);
		return DefaultGun ? DefaultGun->GetCameraWeaponOffset() : FTransform::Identity;
	}

	FTransform TMGetDefaultCameraWeaponOffsetAiming(const AGun* ActiveGun)
	{
		const AGun* DefaultGun = TMGetGunDefaults(ActiveGun);
		return DefaultGun ? DefaultGun->GetCameraWeaponOffsetAiming() : FTransform::Identity;
	}

	bool TMInferAimStateFromFunctionName(const UFunction* Function, bool& bOutAiming)
	{
		if (!Function)
		{
			return false;
		}

		const FString UpperName = Function->GetName().ToUpper();
		if (UpperName.Contains(TEXT("RELEASE"))
			|| UpperName.Contains(TEXT("STOP"))
			|| UpperName.Contains(TEXT("END"))
			|| UpperName.Contains(TEXT("CANCEL"))
			|| UpperName.Contains(TEXT("COMPLETE"))
			|| UpperName.Contains(TEXT("OUT"))
			|| UpperName.Contains(TEXT("DISABLE"))
			|| UpperName.Contains(TEXT("FALSE"))
			|| UpperName.Contains(TEXT("UNAIM"))
			|| UpperName.Contains(TEXT("UNSCOPE")))
		{
			bOutAiming = false;
			return true;
		}

		if (UpperName.Contains(TEXT("PRESS"))
			|| UpperName.Contains(TEXT("START"))
			|| UpperName.Contains(TEXT("BEGIN"))
			|| UpperName.Contains(TEXT("TRIGGER"))
			|| UpperName.Contains(TEXT("ENABLE"))
			|| UpperName.Contains(TEXT("ENTER"))
			|| UpperName.Contains(TEXT("TRUE"))
			|| UpperName.Contains(TEXT("ADS"))
			|| UpperName.Contains(TEXT("AIM"))
			|| UpperName.Contains(TEXT("SIGHT"))
			|| UpperName.Contains(TEXT("SCOPE")))
		{
			bOutAiming = true;
			return true;
		}

		return false;
	}

	bool TMIsActivationTraceFunctionName(const FString& Name)
	{
		return Name.Contains(TEXT("BeginPlay"))
			|| Name.Contains(TEXT("ReceiveBeginPlay"))
			|| Name.Contains(TEXT("UserConstructionScript"))
			|| Name.Contains(TEXT("ExecuteUbergraph"))
			|| Name.Contains(TEXT("SetActive"))
			|| Name.Contains(TEXT("Activate"))
			|| Name.Contains(TEXT("ReadiedWeapon"))
			|| Name.Contains(TEXT("Readied"))
			|| Name.Contains(TEXT("CheckForInventoryItems"))
			|| Name.Contains(TEXT("LoadLoadout"))
			|| Name.Contains(TEXT("CycleInventory"))
			|| Name.Contains(TEXT("Cycle Inventory"))
			|| Name.Contains(TEXT("InputAction"))
			|| Name.Contains(TEXT("InpActEvt"));
	}

	bool TMShouldTraceFunction(const UFunction* Function)
	{
		if (!Function)
		{
			return false;
		}

		const FString Name = Function->GetName();
		if (TMIsTraceNoiseFunction(Name))
		{
			return false;
		}

		const FString Path = Function->GetPathName();
		return TMIsActivationTraceFunctionName(Name)
			|| Name.Contains(TEXT("Weapon"))
			|| Name.Contains(TEXT("Gun"))
			|| Name.Contains(TEXT("Item"))
			|| Name.Contains(TEXT("Equip"))
			|| Name.Contains(TEXT("Draw"))
			|| Name.Contains(TEXT("Holster"))
			|| Name.Contains(TEXT("Active"))
			|| Name.Contains(TEXT("Attach"))
			|| Name.Contains(TEXT("Inventory"))
			|| Name.Contains(TEXT("Loadout"))
			|| Name.Contains(TEXT("Slot"))
			|| Name.Contains(TEXT("Select"))
			|| Name.Contains(TEXT("Switch"))
			|| Name.Contains(TEXT("Primary"))
			|| Name.Contains(TEXT("Secondary"))
			|| Name.Contains(TEXT("Use"))
			|| Name.Contains(TEXT("MPS"))
			|| Name.Contains(TEXT("Anim"))
			|| Name.Contains(TEXT("Overlay"))
			|| Name.Contains(TEXT("Montage"))
			|| Path.Contains(TEXT("CBP_MPS"))
			|| Path.Contains(TEXT("BP_MPS_Master"))
			|| Path.Contains(TEXT("BP_Kriss"))
			|| Path.Contains(TEXT("BP_SMG"))
			|| Path.Contains(TEXT("AnimBP_MPS"))
			|| Path.Contains(TEXT("MP_System_V3"));
	}

	FString TMObjectName(const UObject* Object)
	{
		return Object
			? FString::Printf(TEXT("%s Class=%s"), *Object->GetPathName(), *Object->GetClass()->GetPathName())
			: TEXT("None");
	}

	FString TMComponentAttachInfo(const USceneComponent* Component, const bool bIncludeLocation = false)
	{
		if (!Component)
		{
			return TEXT("None");
		}

		const USceneComponent* Parent = Component->GetAttachParent();
		FString Info = FString::Printf(
			TEXT("%s Parent=%s Socket=%s Visible=%d"),
			*Component->GetName(),
			Parent ? *Parent->GetName() : TEXT("None"),
			*Component->GetAttachSocketName().ToString(),
			Component->IsVisible() ? 1 : 0);

		if (bIncludeLocation)
		{
			Info += FString::Printf(TEXT(" Loc=%s"), *Component->GetComponentLocation().ToCompactString());
		}

		return Info;
	}

	FString TMAnimClassName(const USkeletalMeshComponent* Mesh)
	{
		const UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
		return AnimInstance ? AnimInstance->GetClass()->GetPathName() : TEXT("None");
	}

	FString TMMontageSlotNames(const UAnimMontage* Montage)
	{
		if (!Montage)
		{
			return TEXT("None");
		}

		FString Slots;
		for (const FSlotAnimationTrack& SlotTrack : Montage->SlotAnimTracks)
		{
			if (!Slots.IsEmpty())
			{
				Slots += TEXT(",");
			}
			Slots += SlotTrack.SlotName.ToString();
		}

		return Slots.IsEmpty() ? TEXT("None") : Slots;
	}

	FString TMMontageName(const UAnimMontage* Montage)
	{
		return Montage ? Montage->GetPathName() : TEXT("None");
	}

	void TMAppendFunctionParameters(const UFunction* Function, void* Parameters, FString& Out)
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
				Out += FString::Printf(TEXT(" %s=[%s];"), *PropertyName, *TMObjectName(Value));
			}
			else if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
			{
				Out += FString::Printf(TEXT(" %s=%d;"), *PropertyName, BoolProperty->GetPropertyValue(ValueAddress) ? 1 : 0);
			}
			else if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
			{
				Out += FString::Printf(TEXT(" %s=%s;"), *PropertyName, *NameProperty->GetPropertyValue(ValueAddress).ToString());
			}
			else if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
			{
				Out += FString::Printf(TEXT(" %s=%s;"), *PropertyName, *StringProperty->GetPropertyValue(ValueAddress));
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

	bool TMIsLikelyActiveWeaponPropertyName(const FString& PropertyName)
	{
		const bool bWeaponLike = PropertyName.Contains(TEXT("Weapon"))
			|| PropertyName.Contains(TEXT("Gun"))
			|| PropertyName.Contains(TEXT("Item"));
		const bool bActiveLike = PropertyName.Contains(TEXT("Active"))
			|| PropertyName.Contains(TEXT("Current"))
			|| PropertyName.Contains(TEXT("Equipped"))
			|| PropertyName.Contains(TEXT("Selected"))
			|| PropertyName.Contains(TEXT("Held"));
		return bWeaponLike && bActiveLike;
	}

	bool TMIsRelevantActivationPropertyName(const FString& PropertyName)
	{
		return PropertyName.Contains(TEXT("Active"))
			|| PropertyName.Contains(TEXT("Current"))
			|| PropertyName.Contains(TEXT("Selected"))
			|| PropertyName.Contains(TEXT("Readied"))
			|| PropertyName.Contains(TEXT("Weapon"))
			|| PropertyName.Contains(TEXT("Gun"))
			|| PropertyName.Contains(TEXT("Item"))
			|| PropertyName.Contains(TEXT("Slot"))
			|| PropertyName.Contains(TEXT("Inventory"))
			|| PropertyName.Contains(TEXT("Loadout"))
			|| PropertyName.Contains(TEXT("Primary"))
			|| PropertyName.Contains(TEXT("Secondary"))
			|| PropertyName.Contains(TEXT("Holster"))
			|| PropertyName.Contains(TEXT("Draw"))
			|| PropertyName.Contains(TEXT("First"))
			|| PropertyName.Contains(TEXT("Return"));
	}

	bool TMObjectLooksLikeWeapon(const UObject* Object)
	{
		if (!Object)
		{
			return false;
		}

		const FString ObjectPath = Object->GetPathName();
		const FString ClassPath = Object->GetClass()->GetPathName();
		return ObjectPath.Contains(TEXT("/Weapons/"))
			|| ClassPath.Contains(TEXT("/Weapons/"))
			|| ObjectPath.Contains(TEXT("BP_Kriss"))
			|| ClassPath.Contains(TEXT("BP_Kriss"))
			|| ObjectPath.Contains(TEXT("BP_SMG"))
			|| ClassPath.Contains(TEXT("BP_SMG"));
	}

	FString TMEnumValueToString(const UEnum* Enum, const int64 Value)
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

	void TMAppendPropertyValue(const FProperty* Property, const void* ValuePtr, FString& Out, const int32 Depth);

	void TMAppendArrayPropertyValue(const FArrayProperty* ArrayProperty, const void* ValuePtr, FString& Out, const int32 Depth)
	{
		if (!ArrayProperty || !ValuePtr)
		{
			Out += TEXT("[]");
			return;
		}

		FScriptArrayHelper Helper(ArrayProperty, ValuePtr);
		Out += FString::Printf(TEXT("[Num=%d"), Helper.Num());
		const int32 MaxElementsToLog = FMath::Min(Helper.Num(), 8);
		for (int32 Index = 0; Index < MaxElementsToLog; ++Index)
		{
			Out += FString::Printf(TEXT(" %d="), Index);
			TMAppendPropertyValue(ArrayProperty->Inner, Helper.GetRawPtr(Index), Out, Depth + 1);
		}
		if (Helper.Num() > MaxElementsToLog)
		{
			Out += TEXT(" ...");
		}
		Out += TEXT("]");
	}

	void TMAppendStructPropertyValue(const FStructProperty* StructProperty, const void* ValuePtr, FString& Out, const int32 Depth)
	{
		if (!StructProperty || !ValuePtr || Depth > 1)
		{
			Out += StructProperty && StructProperty->Struct ? *StructProperty->Struct->GetName() : TEXT("Struct");
			return;
		}

		Out += FString::Printf(TEXT("%s{"), *StructProperty->Struct->GetName());
		bool bHasField = false;
		for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
		{
			const FProperty* InnerProperty = *It;
			if (!InnerProperty || !TMIsRelevantActivationPropertyName(InnerProperty->GetName()))
			{
				continue;
			}

			if (bHasField)
			{
				Out += TEXT(" ");
			}
			bHasField = true;

			const void* InnerValuePtr = InnerProperty->ContainerPtrToValuePtr<void>(ValuePtr);
			Out += FString::Printf(TEXT("%s="), *InnerProperty->GetName());
			TMAppendPropertyValue(InnerProperty, InnerValuePtr, Out, Depth + 1);
		}
		Out += bHasField ? TEXT("}") : TEXT("}");
	}

	void TMAppendPropertyValue(const FProperty* Property, const void* ValuePtr, FString& Out, const int32 Depth)
	{
		if (!Property || !ValuePtr)
		{
			Out += TEXT("None");
			return;
		}

		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			const UObject* Value = ObjectProperty->GetObjectPropertyValue(ValuePtr);
			Out += FString::Printf(TEXT("[%s]"), *TMObjectName(Value));
		}
		else if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			Out += BoolProperty->GetPropertyValue(ValuePtr) ? TEXT("1") : TEXT("0");
		}
		else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			const int64 Value = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
			Out += TMEnumValueToString(EnumProperty->GetEnum(), Value);
		}
		else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			const uint8 Value = ByteProperty->GetPropertyValue(ValuePtr);
			Out += TMEnumValueToString(ByteProperty->Enum, Value);
		}
		else if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			Out += NameProperty->GetPropertyValue(ValuePtr).ToString();
		}
		else if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
		{
			Out += StringProperty->GetPropertyValue(ValuePtr);
		}
		else if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
		{
			Out += TextProperty->GetPropertyValue(ValuePtr).ToString();
		}
		else if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			if (NumericProperty->IsFloatingPoint())
			{
				Out += FString::Printf(TEXT("%.3f"), NumericProperty->GetFloatingPointPropertyValue(ValuePtr));
			}
			else if (NumericProperty->IsInteger())
			{
				Out += FString::Printf(TEXT("%lld"), NumericProperty->GetSignedIntPropertyValue(ValuePtr));
			}
		}
		else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			TMAppendArrayPropertyValue(ArrayProperty, ValuePtr, Out, Depth);
		}
		else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			TMAppendStructPropertyValue(StructProperty, ValuePtr, Out, Depth);
		}
		else
		{
			Out += Property->GetCPPType();
		}
	}

	void TMAppendRelevantObjectProperties(const UObject* Object, FString& Out, const int32 MaxChars = 12000)
	{
		if (!Object)
		{
			return;
		}

		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property)
			{
				continue;
			}

			const FString PropertyName = Property->GetName();
			if (!TMIsRelevantActivationPropertyName(PropertyName))
			{
				if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
				{
					const UObject* Value = ObjectProperty->GetObjectPropertyValue_InContainer(Object);
					if (!TMObjectLooksLikeWeapon(Value))
					{
						continue;
					}
				}
				else
				{
					continue;
				}
			}

			Out += FString::Printf(TEXT(" %s="), *PropertyName);
			TMAppendPropertyValue(Property, Property->ContainerPtrToValuePtr<void>(Object), Out, 0);
			Out += TEXT(";");

			if (Out.Len() >= MaxChars)
			{
				Out += TEXT(" ...");
				return;
			}
		}
	}

	FString TMDescribeActiveWeaponState(const UObject* Object)
	{
		if (!Object)
		{
			return TEXT("None");
		}

		FString State;
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property)
			{
				continue;
			}

			const FString PropertyName = Property->GetName();
			if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				const UObject* Value = ObjectProperty->GetObjectPropertyValue_InContainer(Object);
				if (TMIsLikelyActiveWeaponPropertyName(PropertyName)
					|| PropertyName.Contains(TEXT("Active"))
					|| TMObjectLooksLikeWeapon(Value))
				{
					State += FString::Printf(TEXT(" %s=[%s];"), *PropertyName, *TMObjectName(Value));
				}
			}
			else if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
			{
				if (TMIsLikelyActiveWeaponPropertyName(PropertyName)
					|| PropertyName.Contains(TEXT("Active"))
					|| PropertyName.Contains(TEXT("Holster")))
				{
					State += FString::Printf(
						TEXT(" %s=%d;"),
						*PropertyName,
						BoolProperty->GetPropertyValue_InContainer(Object) ? 1 : 0);
				}
			}
			else if (PropertyName.Contains(TEXT("Active")) || PropertyName.Contains(TEXT("Slot")))
			{
				State += FString::Printf(TEXT(" %s="), *PropertyName);
				TMAppendPropertyValue(Property, Property->ContainerPtrToValuePtr<void>(Object), State, 0);
				State += TEXT(";");
			}
		}

		return State.IsEmpty() ? TEXT("None") : State;
	}

	FString TMDescribeMontages(const USkeletalMeshComponent* Mesh)
	{
		const UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
		if (!AnimInstance)
		{
			return TEXT("AnimInstance=None ActiveMontage=None");
		}

		const UAnimMontage* CurrentMontage = AnimInstance->GetCurrentActiveMontage();
		FString Result = FString::Printf(
			TEXT("AnimInstance=%s Class=%s ActiveMontage=%s"),
			*AnimInstance->GetPathName(),
			*AnimInstance->GetClass()->GetPathName(),
			*TMMontageName(CurrentMontage));

		for (const FAnimMontageInstance* MontageInstance : AnimInstance->MontageInstances)
		{
			if (!MontageInstance)
			{
				continue;
			}

			const UAnimMontage* Montage = MontageInstance->Montage;
			Result += FString::Printf(
				TEXT(" MontageInstance{Montage=%s Slots=%s Section=%s Pos=%.3f Rate=%.3f Weight=%.3f DesiredWeight=%.3f Playing=%d Active=%d Stopped=%d};"),
				*TMMontageName(Montage),
				*TMMontageSlotNames(Montage),
				Montage ? *AnimInstance->Montage_GetCurrentSection(Montage).ToString() : TEXT("None"),
				MontageInstance->GetPosition(),
				MontageInstance->GetPlayRate(),
				MontageInstance->GetWeight(),
				MontageInstance->GetDesiredWeight(),
				MontageInstance->IsPlaying() ? 1 : 0,
				MontageInstance->IsActive() ? 1 : 0,
				MontageInstance->IsStopped() ? 1 : 0);
		}

		return Result;
	}

	void TMAppendObjectProperties(const UObject* Object, FString& Out)
	{
		TMAppendRelevantObjectProperties(Object, Out);
	}

	FString TMGunState(const AGun* Gun)
	{
		if (!Gun)
		{
			return TEXT("None");
		}

		FString State = FString::Printf(
			TEXT("Gun=%s Owner=%s Root={%s} FakeMode=%d"),
			*TMObjectName(Gun),
			Gun->GetOwner() ? *Gun->GetOwner()->GetPathName() : TEXT("None"),
			*TMComponentAttachInfo(Gun->GetRootComponent()),
			Gun->IsFakeMode() ? 1 : 0);

		TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshes(Gun);
		for (const USkeletalMeshComponent* SkeletalMesh : SkeletalMeshes)
		{
			State += FString::Printf(
				TEXT(" Mesh{%s Anim=%s Asset=%s};"),
				*TMComponentAttachInfo(SkeletalMesh),
				*TMAnimClassName(SkeletalMesh),
				SkeletalMesh->GetSkeletalMeshAsset()
					? *SkeletalMesh->GetSkeletalMeshAsset()->GetPathName()
					: TEXT("None"));
		}

		TMAppendRelevantObjectProperties(Gun, State, 8000);
		return State;
	}

	FString TMDescribeActivationCore(const ATMCharacter* Character)
	{
		if (!Character)
		{
			return TEXT("Character=None");
		}

		FString State = FString::Printf(
			TEXT("Character=%s ActiveWeaponState={%s}"),
			*TMObjectName(Character),
			*TMDescribeActiveWeaponState(Character));
		TMAppendRelevantObjectProperties(Character, State, 12000);
		return State;
	}

	AGun* TMResolveActiveGun(const ATMCharacter* Character)
	{
		if (!Character)
		{
			return nullptr;
		}

		AGun* WeaponLikeCandidate = nullptr;
		for (TFieldIterator<FProperty> It(Character->GetClass()); It; ++It)
		{
			const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(*It);
			if (!ObjectProperty)
			{
				continue;
			}

			UObject* Value = ObjectProperty->GetObjectPropertyValue_InContainer(Character);
			AGun* Gun = Cast<AGun>(Value);
			if (!Gun)
			{
				continue;
			}

			const FString PropertyName = ObjectProperty->GetName();
			if (TMIsLikelyActiveWeaponPropertyName(PropertyName))
			{
				return Gun;
			}

			if (!WeaponLikeCandidate && TMObjectLooksLikeWeapon(Gun))
			{
				WeaponLikeCandidate = Gun;
			}
		}

		if (WeaponLikeCandidate)
		{
			return WeaponLikeCandidate;
		}

		UWorld* World = Character->GetWorld();
		if (!World)
		{
			return nullptr;
		}

		AGun* ClosestCandidate = nullptr;
		double ClosestDistanceSquared = TNumericLimits<double>::Max();
		for (TActorIterator<AGun> It(World); It; ++It)
		{
			AGun* Gun = *It;
			if (!Gun)
			{
				continue;
			}

			if (Gun->GetOwner() == Character)
			{
				return Gun;
			}

			const double DistanceSquared = FVector::DistSquared(Gun->GetActorLocation(), Character->GetActorLocation());
			if (DistanceSquared < FMath::Square(400.f) && DistanceSquared < ClosestDistanceSquared)
			{
				ClosestDistanceSquared = DistanceSquared;
				ClosestCandidate = Gun;
			}
		}

		return ClosestCandidate;
	}

	FTransform TMMakeADSCameraTargetTransform(const FTransform& ADSSocketWorldTransform, const double EyeRelief)
	{
		FRotator CameraRotation = ADSSocketWorldTransform.GetRotation().Rotator().GetNormalized();
		CameraRotation.Roll = 0.f;

		const FVector CameraLocation =
			ADSSocketWorldTransform.GetLocation() - CameraRotation.Vector() * EyeRelief;
		return FTransform(CameraRotation, CameraLocation, FVector::OneVector);
	}

	FTransform TMGetCameraWorldTransform(const ATMCharacter* Character)
	{
		if (!Character)
		{
			return FTransform::Identity;
		}

		FVector CameraLocation = Character->GetPawnViewLocation();
		FRotator CameraRotation = Character->GetViewRotation();
		if (const UCameraComponent* CameraComponent = Character->FindComponentByClass<UCameraComponent>())
		{
			CameraLocation = CameraComponent->GetComponentLocation();
		}

		CameraRotation.Roll = 0.f;
		return FTransform(CameraRotation.GetNormalized(), CameraLocation, FVector::OneVector);
	}

	bool TMHasBoneOrSocket(const USkeletalMeshComponent* Mesh, const FName BoneOrSocketName)
	{
		return Mesh
			&& !BoneOrSocketName.IsNone()
			&& (Mesh->GetBoneIndex(BoneOrSocketName) != INDEX_NONE || Mesh->DoesSocketExist(BoneOrSocketName));
	}

	bool TMComputeADSWeaponBoneComponentTransform(
		const UAnimInstance* AnimInstance,
		const ATMCharacter* Character,
		const USkeletalMeshComponent* Mesh,
		const FTransform& ADSSocketWorldTransform,
		FTransform& OutWeaponBoneComponentTransform,
		FTransform& OutDesiredSocketWorldTransform)
	{
		if (!AnimInstance || !Character || !Mesh)
		{
			OutWeaponBoneComponentTransform = FTransform::Identity;
			OutDesiredSocketWorldTransform = FTransform::Identity;
			return false;
		}

		static const FName ADSWeaponBoneNamePropertyName(TEXT("ADSWeaponBoneName"));
		static const FName CameraWeaponOffsetAimingPropertyName(TEXT("CameraWeaponOffsetAiming"));
		static const FName ADSWeaponBoneRotationStrengthPropertyName(TEXT("ADSWeaponBoneRotationStrength"));

		FName WeaponBoneName(TEXT("Weapon"));
		TMReadNameProperty(AnimInstance, ADSWeaponBoneNamePropertyName, WeaponBoneName);
		if (!TMHasBoneOrSocket(Mesh, WeaponBoneName))
		{
			OutWeaponBoneComponentTransform = FTransform::Identity;
			OutDesiredSocketWorldTransform = FTransform::Identity;
			return false;
		}

		FTransform SocketCameraOffset = FTransform::Identity;
		TMReadTransformProperty(AnimInstance, CameraWeaponOffsetAimingPropertyName, SocketCameraOffset);

		const FTransform CurrentWeaponBoneWorldTransform = Mesh->GetSocketTransform(WeaponBoneName, RTS_World);
		const FTransform ADSSocketRelativeToWeaponBone =
			ADSSocketWorldTransform.GetRelativeTransform(CurrentWeaponBoneWorldTransform);

		OutDesiredSocketWorldTransform = SocketCameraOffset * TMGetCameraWorldTransform(Character);
		const FTransform FullySolvedWeaponBoneWorldTransform =
			ADSSocketRelativeToWeaponBone.Inverse() * OutDesiredSocketWorldTransform;

		double RotationStrength = 0.0;
		TMReadNumericProperty(AnimInstance, ADSWeaponBoneRotationStrengthPropertyName, RotationStrength);
		RotationStrength = FMath::Clamp(RotationStrength, 0.0, 1.0);

		const FQuat DesiredWeaponBoneWorldRotation = FQuat::Slerp(
			CurrentWeaponBoneWorldTransform.GetRotation(),
			FullySolvedWeaponBoneWorldTransform.GetRotation(),
			RotationStrength).GetNormalized();
		const FVector DesiredWeaponBoneWorldLocation =
			OutDesiredSocketWorldTransform.GetLocation()
			- DesiredWeaponBoneWorldRotation.RotateVector(ADSSocketRelativeToWeaponBone.GetLocation());
		const FTransform DesiredWeaponBoneWorldTransform(
			DesiredWeaponBoneWorldRotation,
			DesiredWeaponBoneWorldLocation,
			CurrentWeaponBoneWorldTransform.GetScale3D());

		OutWeaponBoneComponentTransform =
			DesiredWeaponBoneWorldTransform.GetRelativeTransform(Mesh->GetComponentTransform());
		OutWeaponBoneComponentTransform.NormalizeRotation();
		return true;
	}

	void TMApplyActiveGunCameraOffsetToFPCameraSocket(ATMCharacter* Character, USkeletalMeshComponent* Mesh, const AGun* ActiveGun)
	{
		if (!Mesh || !ActiveGun)
		{
			return;
		}

		USkeletalMesh* SkeletalMesh = Mesh->GetSkeletalMeshAsset();
		if (!SkeletalMesh)
		{
			return;
		}

		static const FName FPCameraSocketName(TEXT("FP_Camera"));
		USkeletalMeshSocket* FPCameraSocket = nullptr;
		if (USkeleton* Skeleton = SkeletalMesh->GetSkeleton())
		{
			FPCameraSocket = Skeleton->FindSocket(FPCameraSocketName);
		}

		if (!FPCameraSocket)
		{
			FPCameraSocket = SkeletalMesh->FindSocket(FPCameraSocketName);
		}

		if (!FPCameraSocket)
		{
			return;
		}

		const bool bUseAimingOffset = TMIsCharacterAimingForCameraWeaponOffset(Character, Mesh->GetAnimInstance());
		const FTransform CameraWeaponOffset = bUseAimingOffset
			? TMGetDefaultCameraWeaponOffsetAiming(ActiveGun)
			: TMGetDefaultCameraWeaponOffset(ActiveGun);
		const FVector TargetLocation = CameraWeaponOffset.GetLocation();
		UCameraComponent* CameraComponent = Character && Character->IsLocallyControlled()
			? Character->FindComponentByClass<UCameraComponent>()
			: nullptr;
		if (CameraComponent && CameraComponent->IsUsingAbsoluteRotation())
		{
			CameraComponent->SetAbsolute(
				CameraComponent->IsUsingAbsoluteLocation(),
				false,
				CameraComponent->IsUsingAbsoluteScale());
		}

		const bool bNeedsSocketLocationUpdate = !FPCameraSocket->RelativeLocation.Equals(TargetLocation, KINDA_SMALL_NUMBER);
		const bool bNeedsCameraRollUpdate = CameraComponent
			&& !FMath::IsNearlyZero(CameraComponent->GetComponentRotation().GetNormalized().Roll, 0.01f);
		if (!bNeedsSocketLocationUpdate && !bNeedsCameraRollUpdate)
		{
			return;
		}

		bool bHasViewTarget = false;
		FVector ViewTarget = FVector::ZeroVector;
		if (Character && CameraComponent)
		{
			const FVector ViewLocation = CameraComponent->GetComponentLocation();
			const FRotator ViewRotation = Character->GetViewRotation();
			const FVector TraceEnd = ViewLocation + ViewRotation.Vector() * 100000.0f;

			FHitResult HitResult;
			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TMApplyActiveGunCameraOffsetToFPCameraSocket), false);
			QueryParams.AddIgnoredActor(Character);
			QueryParams.AddIgnoredActor(ActiveGun);
			bHasViewTarget = Character->GetWorld()
				&& Character->GetWorld()->LineTraceSingleByChannel(
					HitResult,
					ViewLocation,
					TraceEnd,
					ECC_Visibility,
					QueryParams);
			ViewTarget = bHasViewTarget ? HitResult.ImpactPoint : TraceEnd;
			bHasViewTarget = true;
		}

		if (bNeedsSocketLocationUpdate)
		{
#if WITH_EDITOR
			FPCameraSocket->Modify();
#endif
			FPCameraSocket->RelativeLocation = TargetLocation;
			Mesh->UpdateChildTransforms();
		}

		if (bHasViewTarget && CameraComponent)
		{
			const FVector UpdatedViewLocation = CameraComponent->GetComponentLocation();
			FRotator TargetViewRotation = (ViewTarget - UpdatedViewLocation).Rotation().GetNormalized();
			TargetViewRotation.Roll = 0.0f;

			if (AController* Controller = Character->GetController())
			{
				Controller->SetControlRotation(TargetViewRotation);
			}

			if (!Character->GetController() || !CameraComponent->bUsePawnControlRotation)
			{
				CameraComponent->SetWorldRotation(TargetViewRotation, false, nullptr, ETeleportType::TeleportPhysics);
			}
		}
	}

	void TMUpdateCameraWeaponOffsetAnimBridge(UAnimInstance* AnimInstance, const AGun* ActiveGun, const ATMCharacter* Character)
	{
		if (!AnimInstance || !ActiveGun)
		{
			return;
		}

		static const FName CameraWeaponOffsetPropertyName(TEXT("CameraWeaponOffset"));
		static const FName CameraWeaponOffsetNoAimingPropertyName(TEXT("CameraWeaponOffsetNoAiming"));
		static const FName CameraWeaponOffsetAimingPropertyName(TEXT("CameraWeaponOffsetAiming"));
		const FTransform CameraWeaponOffsetNoAiming = TMGetDefaultCameraWeaponOffset(ActiveGun);
		const FTransform CameraWeaponOffsetAiming = TMGetDefaultCameraWeaponOffsetAiming(ActiveGun);
		const bool bUseAimingOffset = TMIsCharacterAimingForCameraWeaponOffset(Character, AnimInstance);

		TMWriteTransformProperty(
			AnimInstance,
			CameraWeaponOffsetPropertyName,
			bUseAimingOffset ? CameraWeaponOffsetAiming : CameraWeaponOffsetNoAiming);
		TMWriteTransformProperty(
			AnimInstance,
			CameraWeaponOffsetNoAimingPropertyName,
			CameraWeaponOffsetNoAiming);
		TMWriteTransformProperty(
			AnimInstance,
			CameraWeaponOffsetAimingPropertyName,
			CameraWeaponOffsetAiming);
	}

	void TMUpdateADSSocketAnimBridge(
		UAnimInstance* AnimInstance,
		const ATMCharacter* Character,
		const USkeletalMeshComponent* Mesh,
		const FTransform& ADSSocketWorldTransform,
		const bool bHasADSSocket)
	{
		if (!AnimInstance || !Character || !Mesh)
		{
			return;
		}

		static const FName ADSSocketWorldTransformPropertyName(TEXT("ADSSocketWorldTransform"));
		static const FName ADSSocketComponentTransformPropertyName(TEXT("ADSSocketComponentTransform"));
		static const FName ADSCameraTargetWorldTransformPropertyName(TEXT("ADSCameraTargetWorldTransform"));
		static const FName ADSSocketValidPropertyName(TEXT("ADSSocketValid"));
		static const FName ADSSocketAlphaPropertyName(TEXT("ADSSocketAlpha"));
		static const FName ADSEyeReliefPropertyName(TEXT("ADSEyeRelief"));
		static const FName ADSWeaponBoneComponentTransformPropertyName(TEXT("ADSWeaponBoneComponentTransform"));
		static const FName ADSWeaponBoneComponentLocationPropertyName(TEXT("ADSWeaponBoneComponentLocation"));
		static const FName ADSWeaponBoneComponentRotationPropertyName(TEXT("ADSWeaponBoneComponentRotation"));
		static const FName ADSDesiredSocketWorldTransformPropertyName(TEXT("ADSDesiredSocketWorldTransform"));
		static const FName ADSWeaponBonePullAlphaPropertyName(TEXT("ADSWeaponBonePullAlpha"));
		static const FName ADSWeaponBonePullStrengthPropertyName(TEXT("ADSWeaponBonePullStrength"));

		double EyeRelief = 0.0;
		TMReadNumericProperty(AnimInstance, ADSEyeReliefPropertyName, EyeRelief);

		const FTransform ADSSocketComponentTransform =
			bHasADSSocket
				? ADSSocketWorldTransform.GetRelativeTransform(Mesh->GetComponentTransform())
				: FTransform::Identity;
		const FTransform ADSCameraTargetWorldTransform =
			bHasADSSocket
				? TMMakeADSCameraTargetTransform(ADSSocketWorldTransform, EyeRelief)
				: FTransform::Identity;
		const bool bAiming = TMIsAimingForWeaponBoneLock(Character, AnimInstance);
		const double Alpha = bHasADSSocket && bAiming ? 1.0 : 0.0;

		FTransform ADSWeaponBoneComponentTransform = FTransform::Identity;
		FTransform ADSDesiredSocketWorldTransform = FTransform::Identity;
		const bool bHasADSWeaponBoneTransform =
			bHasADSSocket
			&& TMComputeADSWeaponBoneComponentTransform(
				AnimInstance,
				Character,
				Mesh,
				ADSSocketWorldTransform,
				ADSWeaponBoneComponentTransform,
				ADSDesiredSocketWorldTransform);

		double PullStrength = 1.0;
		TMReadNumericProperty(AnimInstance, ADSWeaponBonePullStrengthPropertyName, PullStrength);
		const double PullAlpha = bHasADSWeaponBoneTransform && bAiming ? FMath::Clamp(PullStrength, 0.0, 1.0) : 0.0;

		TMWriteTransformProperty(AnimInstance, ADSSocketWorldTransformPropertyName, bHasADSSocket ? ADSSocketWorldTransform : FTransform::Identity);
		TMWriteTransformProperty(AnimInstance, ADSSocketComponentTransformPropertyName, ADSSocketComponentTransform);
		TMWriteTransformProperty(AnimInstance, ADSCameraTargetWorldTransformPropertyName, ADSCameraTargetWorldTransform);
		TMWriteBoolProperty(AnimInstance, ADSSocketValidPropertyName, bHasADSSocket);
		TMWriteNumericProperty(AnimInstance, ADSSocketAlphaPropertyName, Alpha);
		TMWriteTransformProperty(AnimInstance, ADSWeaponBoneComponentTransformPropertyName, ADSWeaponBoneComponentTransform);
		TMWriteVectorProperty(AnimInstance, ADSWeaponBoneComponentLocationPropertyName, ADSWeaponBoneComponentTransform.GetLocation());
		TMWriteRotatorProperty(AnimInstance, ADSWeaponBoneComponentRotationPropertyName, ADSWeaponBoneComponentTransform.GetRotation().Rotator().GetNormalized());
		TMWriteTransformProperty(AnimInstance, ADSDesiredSocketWorldTransformPropertyName, ADSDesiredSocketWorldTransform);
		TMWriteNumericProperty(AnimInstance, ADSWeaponBonePullAlphaPropertyName, PullAlpha);
	}

	void TMUpdateADSSocketAnimBridge(const ATMCharacter* Character, const USkeletalMeshComponent* Mesh)
	{
		if (!Character || !Mesh)
		{
			return;
		}

		FTransform ADSSocketWorldTransform = FTransform::Identity;
		bool bHasADSSocket = false;
		AGun* ActiveGun = TMResolveActiveGun(Character);
		if (ActiveGun)
		{
			ActiveGun->RefreshADSSocket();
			bHasADSSocket = ActiveGun->GetADSSocketWorldTransform(ADSSocketWorldTransform);
		}

		TMApplyActiveGunCameraOffsetToFPCameraSocket(
			const_cast<ATMCharacter*>(Character),
			const_cast<USkeletalMeshComponent*>(Mesh),
			ActiveGun);
		TMUpdateCameraWeaponOffsetAnimBridge(Mesh->GetAnimInstance(), ActiveGun, Character);
		TMUpdateADSSocketAnimBridge(
			Mesh->GetAnimInstance(),
			Character,
			Mesh,
			ADSSocketWorldTransform,
			bHasADSSocket);

		for (UAnimInstance* LinkedAnimInstance : Mesh->GetLinkedAnimInstances())
		{
			TMUpdateCameraWeaponOffsetAnimBridge(LinkedAnimInstance, ActiveGun, Character);
			TMUpdateADSSocketAnimBridge(
				LinkedAnimInstance,
				Character,
				Mesh,
				ADSSocketWorldTransform,
				bHasADSSocket);
		}
	}

	bool TMIsRightHandIKTargetTooFar(const USkeletalMeshComponent* Mesh)
	{
		static const FName RightHandBoneName(TEXT("hand_r"));
		static const FName RightHandIKTargetBoneName(TEXT("VB LHS_ik_hand_r"));
		static constexpr double MaxRightHandIKTargetDistance = 80.0;

		if (!TMHasBoneOrSocket(Mesh, RightHandBoneName) || !TMHasBoneOrSocket(Mesh, RightHandIKTargetBoneName))
		{
			return false;
		}

		const FVector RightHandLocation = Mesh->GetSocketLocation(RightHandBoneName);
		const FVector RightHandTargetLocation = Mesh->GetSocketLocation(RightHandIKTargetBoneName);
		return FVector::DistSquared(RightHandLocation, RightHandTargetLocation) > FMath::Square(MaxRightHandIKTargetDistance);
	}

	void TMUpdateRightHandIKTargetGuard(UAnimInstance* AnimInstance, const USkeletalMeshComponent* Mesh)
	{
		if (!AnimInstance || !Mesh || !TMIsRightHandIKTargetTooFar(Mesh))
		{
			return;
		}

		static const FName EnableHandIKRPropertyName(TEXT("Enable_HandIK_R"));
		static const FName HandIKRightPropertyName(TEXT("HandIK_Right"));

		TMWriteAlphaLikeProperty(AnimInstance, EnableHandIKRPropertyName, 0.0);
		TMWriteAlphaLikeProperty(AnimInstance, HandIKRightPropertyName, 0.0);
	}

	void TMUpdateRightHandIKTargetGuard(const USkeletalMeshComponent* Mesh)
	{
		if (!Mesh)
		{
			return;
		}

		TMUpdateRightHandIKTargetGuard(Mesh->GetAnimInstance(), Mesh);
		for (UAnimInstance* LinkedAnimInstance : Mesh->GetLinkedAnimInstances())
		{
			TMUpdateRightHandIKTargetGuard(LinkedAnimInstance, Mesh);
		}
	}

}

ATMCharacter::ATMCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

AGun* ATMCharacter::GetActiveGun() const
{
	return TMResolveActiveGun(this);
}

bool ATMCharacter::GetActiveWeaponADSSocketWorldTransform(FTransform& OutTransform) const
{
	OutTransform = FTransform::Identity;

	AGun* ActiveGun = GetActiveGun();
	if (!ActiveGun)
	{
		return false;
	}

	ActiveGun->RefreshADSSocket();
	return ActiveGun->GetADSSocketWorldTransform(OutTransform);
}

bool ATMCharacter::GetActiveWeaponADSCameraTargetTransform(const float EyeRelief, FTransform& OutTransform) const
{
	FTransform ADSSocketWorldTransform = FTransform::Identity;
	if (!GetActiveWeaponADSSocketWorldTransform(ADSSocketWorldTransform))
	{
		OutTransform = FTransform::Identity;
		return false;
	}

	OutTransform = TMMakeADSCameraTargetTransform(ADSSocketWorldTransform, EyeRelief);
	return true;
}

void ATMCharacter::BeginPlay()
{
	Super::BeginPlay();

	UpdateLocalPlayerControlledFlag();
	if (IsTouchMeRuntimeTraceEnabled())
	{
		BindRuntimeTraceAnimDelegates();
		LogRuntimeTraceSnapshot(TEXT("BeginPlay"));
	}
}

void ATMCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateLocalPlayerControlledFlag();
	UTMGameplayStatics::ApplyALSTurnInPlaceState(this, DeltaSeconds);
	TMUpdateFabrikFixerAlpha(GetMesh());
	TMUpdateWeaponBoneFloorLock(this, GetMesh());
	TMUpdateADSSocketAnimBridge(this, GetMesh());
	TMUpdateRightHandIKTargetGuard(GetMesh());
	if (!IsTouchMeRuntimeTraceEnabled())
	{
		return;
	}

	BindRuntimeTraceAnimDelegates();

	if (!bIsLocalPlayerControlled)
	{
		return;
	}

	RuntimeTraceAccumulator += DeltaSeconds;
	if (RuntimeTraceAccumulator < 0.1f)
	{
		return;
	}
	RuntimeTraceAccumulator = 0.f;

	LogRuntimeTraceSnapshot(TEXT("Tick"));
}

void ATMCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	UpdateLocalPlayerControlledFlag();
}

void ATMCharacter::UnPossessed()
{
	Super::UnPossessed();

	UpdateLocalPlayerControlledFlag();
}

void ATMCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();

	UpdateLocalPlayerControlledFlag();
	if (IsTouchMeRuntimeTraceEnabled())
	{
		BindRuntimeTraceAnimDelegates();
	}
}

void ATMCharacter::ProcessEvent(UFunction* Function, void* Parameters)
{
	const bool bTrace = IsTouchMeRuntimeTraceEnabled() && TMShouldTraceFunction(Function);
	const bool bMPSServerAimFunction = TMIsMPSServerAimFunction(Function);
	const bool bALSAimBridgeCandidate = bMPSServerAimFunction || TMIsAimBridgeFunction(Function);
	bool bFallbackALSAimingState = false;
	const bool bHasFallbackALSAimingState = bALSAimBridgeCandidate
		&& (TMExtractAimStateFromParameters(Function, Parameters, bFallbackALSAimingState)
			|| TMInferAimStateFromFunctionName(Function, bFallbackALSAimingState));
	const FString BeforeActivationState = bTrace ? TMDescribeActivationCore(this) : FString();
	if (bTrace)
	{
		FString Params;
		TMAppendFunctionParameters(Function, Parameters, Params);
		UE_LOG(
			LogTouchMeRuntimeTrace,
			Warning,
			TEXT("[CharacterActivationTrace:Before] Function=%s Params={%s} State={%s} %s"),
			Function ? *Function->GetPathName() : TEXT("None"),
			*Params,
			*BeforeActivationState,
			*TMDescribeMontages(GetMesh()));
		UE_LOG(
			LogTouchMeRuntimeTrace,
			Warning,
			TEXT("[CharacterProcessEvent:Before] Character=%s Function=%s Params={%s} ActiveWeaponState={%s} %s"),
			*TMObjectName(this),
			Function ? *Function->GetPathName() : TEXT("None"),
			*Params,
			*TMDescribeActiveWeaponState(this),
			*TMDescribeMontages(GetMesh()));
	}

	Super::ProcessEvent(Function, Parameters);

	if (bALSAimBridgeCandidate)
	{
		bool bAiming = false;
		bool bHasAimingState = false;
		if (bMPSServerAimFunction && bHasFallbackALSAimingState)
		{
			bAiming = bFallbackALSAimingState;
			bHasAimingState = true;
		}
		else
		{
			bHasAimingState = TMReadAimStateBoolProperty(this, bAiming)
				|| (bHasFallbackALSAimingState ? (bAiming = bFallbackALSAimingState, true) : false);
		}

		if (bHasAimingState && (bMPSServerAimFunction || !bHasLastALSAimBridgeState || bLastALSAimBridgeState != bAiming))
		{
			bHasLastALSAimBridgeState = true;
			bLastALSAimBridgeState = bAiming;
			UTMGameplayStatics::ApplyALSAimState(this, bAiming);

			if (IsTouchMeRuntimeTraceEnabled())
			{
				UE_LOG(
					LogTouchMeRuntimeTrace,
					Warning,
					TEXT("[ALSAimBridge] Character=%s Function=%s bAiming=%d"),
					*TMObjectName(this),
					Function ? *Function->GetPathName() : TEXT("None"),
					bAiming ? 1 : 0);
			}
		}
	}

	if (bTrace)
	{
		const FString AfterActivationState = TMDescribeActivationCore(this);
		UE_LOG(
			LogTouchMeRuntimeTrace,
			Warning,
			TEXT("[CharacterActivationTrace:After] Function=%s Changed=%d Before={%s} After={%s}"),
			Function ? *Function->GetPathName() : TEXT("None"),
			BeforeActivationState != AfterActivationState ? 1 : 0,
			*BeforeActivationState,
			*AfterActivationState);

		const FString Reason = FString::Printf(
			TEXT("ProcessEventAfter:%s"),
			Function ? *Function->GetName() : TEXT("None"));
		LogRuntimeTraceSnapshot(*Reason);
	}
}

void ATMCharacter::OnRuntimeTraceMontageStarted(UAnimMontage* Montage)
{
	if (!IsTouchMeRuntimeTraceEnabled())
	{
		return;
	}

	UE_LOG(
		LogTouchMeRuntimeTrace,
		Warning,
		TEXT("[MontageStarted] Character=%s Montage=%s Slots=%s %s"),
		*TMObjectName(this),
		*TMMontageName(Montage),
		*TMMontageSlotNames(Montage),
		*TMDescribeMontages(GetMesh()));
}

void ATMCharacter::OnRuntimeTraceMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	if (!IsTouchMeRuntimeTraceEnabled())
	{
		return;
	}

	UE_LOG(
		LogTouchMeRuntimeTrace,
		Warning,
		TEXT("[MontageBlendingOut] Character=%s Montage=%s Interrupted=%d %s"),
		*TMObjectName(this),
		*TMMontageName(Montage),
		bInterrupted ? 1 : 0,
		*TMDescribeMontages(GetMesh()));
}

void ATMCharacter::OnRuntimeTraceMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (!IsTouchMeRuntimeTraceEnabled())
	{
		return;
	}

	UE_LOG(
		LogTouchMeRuntimeTrace,
		Warning,
		TEXT("[MontageEnded] Character=%s Montage=%s Interrupted=%d %s"),
		*TMObjectName(this),
		*TMMontageName(Montage),
		bInterrupted ? 1 : 0,
		*TMDescribeMontages(GetMesh()));
}

void ATMCharacter::OnRuntimeTraceMontageSectionChanged(UAnimMontage* Montage, FName SectionName, bool bLooped)
{
	if (!IsTouchMeRuntimeTraceEnabled())
	{
		return;
	}

	UE_LOG(
		LogTouchMeRuntimeTrace,
		Warning,
		TEXT("[MontageSectionChanged] Character=%s Montage=%s Section=%s Looped=%d %s"),
		*TMObjectName(this),
		*TMMontageName(Montage),
		*SectionName.ToString(),
		bLooped ? 1 : 0,
		*TMDescribeMontages(GetMesh()));
}

void ATMCharacter::BindRuntimeTraceAnimDelegates()
{
	if (!IsTouchMeRuntimeTraceEnabled() || !bIsLocalPlayerControlled || !GetMesh())
	{
		return;
	}

	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (!AnimInstance || RuntimeTraceAnimInstance.Get() == AnimInstance)
	{
		return;
	}

	if (UAnimInstance* PreviousAnimInstance = RuntimeTraceAnimInstance.Get())
	{
		PreviousAnimInstance->OnMontageStarted.RemoveDynamic(this, &ATMCharacter::OnRuntimeTraceMontageStarted);
		PreviousAnimInstance->OnMontageBlendingOut.RemoveDynamic(this, &ATMCharacter::OnRuntimeTraceMontageBlendingOut);
		PreviousAnimInstance->OnMontageEnded.RemoveDynamic(this, &ATMCharacter::OnRuntimeTraceMontageEnded);
		PreviousAnimInstance->OnMontageSectionChanged.RemoveDynamic(this, &ATMCharacter::OnRuntimeTraceMontageSectionChanged);
	}

	RuntimeTraceAnimInstance = AnimInstance;
	AnimInstance->OnMontageStarted.AddUniqueDynamic(this, &ATMCharacter::OnRuntimeTraceMontageStarted);
	AnimInstance->OnMontageBlendingOut.AddUniqueDynamic(this, &ATMCharacter::OnRuntimeTraceMontageBlendingOut);
	AnimInstance->OnMontageEnded.AddUniqueDynamic(this, &ATMCharacter::OnRuntimeTraceMontageEnded);
	AnimInstance->OnMontageSectionChanged.AddUniqueDynamic(this, &ATMCharacter::OnRuntimeTraceMontageSectionChanged);

	UE_LOG(
		LogTouchMeRuntimeTrace,
		Warning,
		TEXT("[MontageTraceBound] Character=%s %s"),
		*TMObjectName(this),
		*TMDescribeMontages(GetMesh()));
}

void ATMCharacter::LogRuntimeTraceSnapshot(const TCHAR* Reason)
{
	if (!IsTouchMeRuntimeTraceEnabled() || !bIsLocalPlayerControlled)
	{
		return;
	}

	FString Signature = FString::Printf(
		TEXT("Reason=%s Character=%s MeshAnim=%s Mesh={%s} %s"),
		Reason,
		*TMObjectName(this),
		*TMAnimClassName(GetMesh()),
		*TMComponentAttachInfo(GetMesh()),
		*TMDescribeMontages(GetMesh()));

	TMAppendObjectProperties(this, Signature);

	const FString ActiveWeaponSignature = TMDescribeActiveWeaponState(this);
	if (ActiveWeaponSignature != LastRuntimeTraceActiveWeaponSignature)
	{
		LastRuntimeTraceActiveWeaponSignature = ActiveWeaponSignature;
		UE_LOG(
			LogTouchMeRuntimeTrace,
			Warning,
			TEXT("[ActiveWeaponStateChanged] Reason=%s Character=%s ActiveWeaponState={%s}"),
			Reason,
			*TMObjectName(this),
			*ActiveWeaponSignature);
	}

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AGun> It(World); It; ++It)
		{
			const AGun* Gun = *It;
			if (!Gun)
			{
				continue;
			}

			if (Gun->GetOwner() == this || FVector::DistSquared(Gun->GetActorLocation(), GetActorLocation()) < FMath::Square(400.f))
			{
				Signature += TEXT(" | ");
				Signature += TMGunState(Gun);
			}
		}
	}

	if (Signature != LastRuntimeTraceSignature)
	{
		LastRuntimeTraceSignature = Signature;
		UE_LOG(LogTouchMeRuntimeTrace, Warning, TEXT("[CharacterSnapshot] %s"), *Signature);
	}
}

void ATMCharacter::UpdateLocalPlayerControlledFlag()
{
	bIsLocalPlayerControlled = IsPlayerControlled() && IsLocallyControlled();
}
