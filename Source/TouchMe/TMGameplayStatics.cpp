// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMGameplayStatics.h"
#include "TouchMe.h"
#include "Gun/Gun.h"
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
#include "NiagaraCommon.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/KismetSystemLibrary.h"
#include "SceneView.h"
#include "Components/PrimitiveComponent.h"
#include "Math/InverseRotationMatrix.h"
#include "UObject/Package.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "ParticleHelper.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LocalPlayer.h"
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

#if WITH_EDITOR
#include "AnimGraphNode_CopyBone.h"
#include "AnimGraphNode_Fabrik.h"
#include "AnimGraphNode_ModifyBone.h"
#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(TMGameplayStatics)

#if WITH_ACCESSIBILITY
#include "Framework/Application/SlateApplication.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Accessibility/SlateAccessibleMessageHandler.h"
#endif

#define LOCTEXT_NAMESPACE "TMGameplayStatics"

namespace TMGameplayStatics
{
	struct FALSTurnInPlaceBridgeState
	{
		bool bHasSourceYaw = false;
		double SourceYaw = 0.0;
	};

	TMap<TWeakObjectPtr<ACharacter>, FALSTurnInPlaceBridgeState> GALSTurnInPlaceBridgeStates;

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

#if WITH_EDITOR
	bool IsWeaponPoseRelevantBone(const FName BoneName)
	{
		static const TSet<FName> RelevantBones = {
			TEXT("Camera_FP"),
			TEXT("FP_Camera"),
			TEXT("VB Control"),
			TEXT("VB Hand_R"),
			TEXT("VB Hand_L"),
			TEXT("VB LHS_ik_hand_l"),
			TEXT("VB LHS_ik_hand_r"),
			TEXT("VB RHS_ik_hand_l"),
			TEXT("VB RHS_ik_hand_gun"),
			TEXT("Weapon"),
			TEXT("hand_r"),
			TEXT("hand_l"),
			TEXT("ik_hand_l"),
			TEXT("ik_hand_r"),
			TEXT("ik_hand_gun"),
			TEXT("head")
		};
		return RelevantBones.Contains(BoneName);
	}

	FString DescribeGraphPinLinks(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return TEXT("None");
		}

		TArray<FString> Links;
		for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			const UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
			Links.Add(FString::Printf(
				TEXT("%s.%s"),
				LinkedNode ? *LinkedNode->GetName() : TEXT("None"),
				LinkedPin ? *LinkedPin->PinName.ToString() : TEXT("None")));
		}

		return Links.Num() > 0 ? FString::Join(Links, TEXT(", ")) : TEXT("None");
	}

	bool ShouldDumpGraphPin(const UEdGraphPin* Pin)
	{
		return Pin
			&& (Pin->LinkedTo.Num() > 0
				|| Pin->PinName == TEXT("ComponentPose")
				|| Pin->PinName == TEXT("Pose")
				|| Pin->PinName == TEXT("Translation")
				|| Pin->PinName == TEXT("Rotation")
				|| Pin->PinName == TEXT("Scale")
				|| Pin->PinName == TEXT("Alpha")
				|| Pin->PinName == TEXT("EffectorTransform"));
	}

	void AppendNodePins(const UEdGraphNode* Node, FString& Dump)
	{
		if (!Node)
		{
			return;
		}

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!ShouldDumpGraphPin(Pin))
			{
				continue;
			}

			Dump += FString::Printf(
				TEXT("    Pin=%s Dir=%s Default={%s} Links=%s\n"),
				*Pin->PinName.ToString(),
				Pin->Direction == EGPD_Input ? TEXT("In") : TEXT("Out"),
				*Pin->DefaultValue,
				*DescribeGraphPinLinks(Pin));
		}
	}

	void AppendAnimBlueprintGraphDump(const TCHAR* AssetPath, FString& Dump)
	{
		UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, AssetPath);
		if (!AnimBlueprint)
		{
			Dump += FString::Printf(TEXT("FAILED load %s\n"), AssetPath);
			return;
		}

		Dump += FString::Printf(TEXT("ASSET %s\n"), *AnimBlueprint->GetPathName());
		TArray<UEdGraph*> Graphs;
		AnimBlueprint->GetAllGraphs(Graphs);

		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}

			for (UEdGraphNode* GraphNode : Graph->Nodes)
			{
				if (const UAnimGraphNode_ModifyBone* ModifyBoneNode = Cast<UAnimGraphNode_ModifyBone>(GraphNode))
				{
					const FName BoneName = ModifyBoneNode->Node.BoneToModify.BoneName;
					if (!IsWeaponPoseRelevantBone(BoneName))
					{
						continue;
					}

					Dump += FString::Printf(
						TEXT("  Graph=%s Node=%s Type=ModifyBone Comment={%s} Bone=%s TM=%d RM=%d TS=%d RS=%d Loc=%s Rot=%s\n"),
						*Graph->GetName(),
						*ModifyBoneNode->GetName(),
						*ModifyBoneNode->NodeComment,
						*BoneName.ToString(),
						static_cast<int32>(ModifyBoneNode->Node.TranslationMode),
						static_cast<int32>(ModifyBoneNode->Node.RotationMode),
						static_cast<int32>(ModifyBoneNode->Node.TranslationSpace),
						static_cast<int32>(ModifyBoneNode->Node.RotationSpace),
						*ModifyBoneNode->Node.Translation.ToString(),
						*ModifyBoneNode->Node.Rotation.ToString());
					AppendNodePins(ModifyBoneNode, Dump);
				}
				else if (const UAnimGraphNode_CopyBone* CopyBoneNode = Cast<UAnimGraphNode_CopyBone>(GraphNode))
				{
					const FName SourceBoneName = CopyBoneNode->Node.SourceBone.BoneName;
					const FName TargetBoneName = CopyBoneNode->Node.TargetBone.BoneName;
					if (!IsWeaponPoseRelevantBone(SourceBoneName) && !IsWeaponPoseRelevantBone(TargetBoneName))
					{
						continue;
					}

					Dump += FString::Printf(
						TEXT("  Graph=%s Node=%s Type=CopyBone Source=%s Target=%s CopyT=%d CopyR=%d CopyS=%d Space=%d\n"),
						*Graph->GetName(),
						*CopyBoneNode->GetName(),
						*SourceBoneName.ToString(),
						*TargetBoneName.ToString(),
						CopyBoneNode->Node.bCopyTranslation ? 1 : 0,
						CopyBoneNode->Node.bCopyRotation ? 1 : 0,
						CopyBoneNode->Node.bCopyScale ? 1 : 0,
						static_cast<int32>(CopyBoneNode->Node.ControlSpace));
					AppendNodePins(CopyBoneNode, Dump);
				}
				else if (const UAnimGraphNode_Fabrik* FabrikNode = Cast<UAnimGraphNode_Fabrik>(GraphNode))
				{
					const FName RootBoneName = FabrikNode->Node.RootBone.BoneName;
					const FName TipBoneName = FabrikNode->Node.TipBone.BoneName;
					const FName EffectorTargetName = FabrikNode->Node.EffectorTarget.bUseSocket
						? FabrikNode->Node.EffectorTarget.SocketReference.SocketName
						: FabrikNode->Node.EffectorTarget.BoneReference.BoneName;
					if (!IsWeaponPoseRelevantBone(RootBoneName)
						&& !IsWeaponPoseRelevantBone(TipBoneName)
						&& !IsWeaponPoseRelevantBone(EffectorTargetName))
					{
						continue;
					}

					Dump += FString::Printf(
						TEXT("  Graph=%s Node=%s Type=FABRIK Root=%s Tip=%s EffectorTarget=%s UseSocket=%d ETSpace=%d ERSource=%d AlphaType=%d Alpha=%.3f AlphaCurve=%s Effector=%s\n"),
						*Graph->GetName(),
						*FabrikNode->GetName(),
						*RootBoneName.ToString(),
						*TipBoneName.ToString(),
						*EffectorTargetName.ToString(),
						FabrikNode->Node.EffectorTarget.bUseSocket ? 1 : 0,
						static_cast<int32>(FabrikNode->Node.EffectorTransformSpace.GetValue()),
						static_cast<int32>(FabrikNode->Node.EffectorRotationSource.GetValue()),
						static_cast<int32>(FabrikNode->Node.AlphaInputType),
						FabrikNode->Node.Alpha,
						*FabrikNode->Node.AlphaCurveName.ToString(),
						*FabrikNode->Node.EffectorTransform.ToString());
					AppendNodePins(FabrikNode, Dump);
				}
			}
		}
	}
#endif

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

	bool ResolveEnumValueByAliases(const UEnum* Enum, const TArray<FString>& Aliases, int64& OutValue)
	{
		if (!Enum || Aliases.IsEmpty())
		{
			return false;
		}

		for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
		{
#if WITH_METADATA
			if (Enum->HasMetaData(TEXT("Hidden"), Index))
			{
				continue;
			}
#endif

			const FString Name = NormalizeOverlayEnumText(Enum->GetNameStringByIndex(Index));
			const FString DisplayName = NormalizeOverlayEnumText(Enum->GetDisplayNameTextByIndex(Index).ToString());
			if (Aliases.Contains(Name) || Aliases.Contains(DisplayName))
			{
				OutValue = Enum->GetValueByIndex(Index);
				return true;
			}
		}

		return false;
	}

	bool SetEnumLikeValueByAliases(FProperty* Property, void* ValuePtr, const TArray<FString>& Aliases)
	{
		if (!Property || !ValuePtr || Aliases.IsEmpty())
		{
			return false;
		}

		UEnum* Enum = nullptr;
		FNumericProperty* NumericProperty = nullptr;
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

		int64 Value = 0;
		if (!Enum || !NumericProperty || !ResolveEnumValueByAliases(Enum, Aliases, Value))
		{
			return false;
		}

		NumericProperty->SetIntPropertyValue(ValuePtr, Value);
		return true;
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

		return SetEnumLikeValueByAliases(Property, Property->ContainerPtrToValuePtr<void>(Object), Aliases);
	}

	bool SetEnumLikePropertyValueByAliasesOrValue(UObject* Object, const FName PropertyName, const TArray<FString>& Aliases, const int64 FallbackValue)
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
		UEnum* Enum = nullptr;
		FNumericProperty* NumericProperty = nullptr;
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

		if (!NumericProperty)
		{
			return false;
		}

		int64 TargetValue = FallbackValue;
		if (Enum)
		{
			ResolveEnumValueByAliases(Enum, Aliases, TargetValue);
		}

		if (NumericProperty->GetSignedIntPropertyValue(ValuePtr) == TargetValue)
		{
			return false;
		}

		NumericProperty->SetIntPropertyValue(ValuePtr, TargetValue);
		return true;
	}

	bool GetBoolPropertyValueByName(const UObject* Object, const FName PropertyName, bool& bOutValue)
	{
		if (!Object)
		{
			return false;
		}

		const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!BoolProperty)
		{
			return false;
		}

		bOutValue = BoolProperty->GetPropertyValue_InContainer(Object);
		return true;
	}

	bool GetNumericPropertyValueByName(const UObject* Object, const FName PropertyName, double& OutValue)
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

	bool SetNumericPropertyValueByName(UObject* Object, const FName PropertyName, const double Value)
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
		const double CurrentValue = NumericProperty->IsFloatingPoint()
			? NumericProperty->GetFloatingPointPropertyValue(ValuePtr)
			: static_cast<double>(NumericProperty->GetSignedIntPropertyValue(ValuePtr));
		if (FMath::IsNearlyEqual(CurrentValue, Value, KINDA_SMALL_NUMBER))
		{
			return false;
		}

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

	bool SetRotatorPropertyValueByName(UObject* Object, const FName PropertyName, const FRotator& Value)
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
		if (ValuePtr && ValuePtr->Equals(Value, KINDA_SMALL_NUMBER))
		{
			return false;
		}

		if (ValuePtr)
		{
			*ValuePtr = Value;
			return true;
		}
		return false;
	}

	bool SetVector2DPropertyValueByName(UObject* Object, const FName PropertyName, const FVector2D& Value)
	{
		if (!Object)
		{
			return false;
		}

		FStructProperty* StructProperty = CastField<FStructProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!StructProperty || StructProperty->Struct != TBaseStructure<FVector2D>::Get())
		{
			return false;
		}

		FVector2D* ValuePtr = StructProperty->ContainerPtrToValuePtr<FVector2D>(Object);
		if (ValuePtr && ValuePtr->Equals(Value, KINDA_SMALL_NUMBER))
		{
			return false;
		}

		if (ValuePtr)
		{
			*ValuePtr = Value;
			return true;
		}
		return false;
	}

	void AddRotationModeAlias(TArray<FString>& Aliases, const FString& Alias)
	{
		AddOverlayAlias(Aliases, Alias);
	}

	TArray<FString> BuildRotationModeAliasesForAimState(const ACharacter* Character, const bool bAiming)
	{
		TArray<FString> Aliases;
		if (bAiming)
		{
			AddRotationModeAlias(Aliases, TEXT("Aiming"));
			return Aliases;
		}

		int64 DesiredRotationModeValue = 1;
		FString DesiredRotationModeDisplayName;
		if (GetEnumLikePropertyValue(Character, TEXT("DesiredRotationMode"), DesiredRotationModeValue, DesiredRotationModeDisplayName))
		{
			AddRotationModeAlias(Aliases, DesiredRotationModeDisplayName);
			switch (DesiredRotationModeValue)
			{
			case 0:
				AddRotationModeAlias(Aliases, TEXT("VelocityDirection"));
				AddRotationModeAlias(Aliases, TEXT("Velocity Direction"));
				break;
			case 1:
				AddRotationModeAlias(Aliases, TEXT("LookingDirection"));
				AddRotationModeAlias(Aliases, TEXT("Looking Direction"));
				break;
			case 2:
				AddRotationModeAlias(Aliases, TEXT("Aiming"));
				break;
			default:
				break;
			}
		}

		if (Aliases.IsEmpty())
		{
			AddRotationModeAlias(Aliases, TEXT("LookingDirection"));
			AddRotationModeAlias(Aliases, TEXT("Looking Direction"));
		}

		return Aliases;
	}

	bool AliasesContainAiming(const TArray<FString>& Aliases)
	{
		return Aliases.Contains(NormalizeOverlayEnumText(TEXT("Aiming")));
	}

	TArray<FString> BuildLookingDirectionAliases()
	{
		TArray<FString> Aliases;
		AddRotationModeAlias(Aliases, TEXT("LookingDirection"));
		AddRotationModeAlias(Aliases, TEXT("Looking Direction"));
		return Aliases;
	}

	TArray<FString> BuildThirdPersonViewModeAliases()
	{
		TArray<FString> Aliases;
		AddOverlayAlias(Aliases, TEXT("ThirdPerson"));
		AddOverlayAlias(Aliases, TEXT("Third Person"));
		AddOverlayAlias(Aliases, TEXT("TP"));
		return Aliases;
	}

	bool SetBoolPropertyByName(UObject* Object, const FName PropertyName, const bool bValue)
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

		if (BoolProperty->GetPropertyValue_InContainer(Object) == bValue)
		{
			return false;
		}

		BoolProperty->SetPropertyValue_InContainer(Object, bValue);
		return true;
	}

	bool SetObjectPropertyValueByName(UObject* Object, const FName PropertyName, UObject* Value)
	{
		if (!Object || !Value)
		{
			return false;
		}

		FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!ObjectProperty || !Value->IsA(ObjectProperty->PropertyClass))
		{
			return false;
		}

		UObject* CurrentValue = ObjectProperty->GetObjectPropertyValue_InContainer(Object);
		if (CurrentValue == Value)
		{
			return false;
		}

		ObjectProperty->SetObjectPropertyValue_InContainer(Object, Value);
		return true;
	}

	bool CallSetRotationModeFunction(UObject* Object, const TArray<FString>& Aliases)
	{
		if (!Object || Aliases.IsEmpty())
		{
			return false;
		}

		UFunction* Function = Object->FindFunction(TEXT("SetRotationMode"));
		if (!Function)
		{
			return false;
		}

		void* Parameters = FMemory_Alloca(Function->ParmsSize);
		FMemory::Memzero(Parameters, Function->ParmsSize);

		bool bSetRotationMode = false;
		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property || !Property->HasAnyPropertyFlags(CPF_Parm) || Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				continue;
			}

			void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Parameters);
			if (!bSetRotationMode && SetEnumLikeValueByAliases(Property, ValuePtr, Aliases))
			{
				bSetRotationMode = true;
			}
			else if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
			{
				const FString PropertyName = Property->GetName();
				if (PropertyName.Contains(TEXT("Force"), ESearchCase::IgnoreCase)
					|| PropertyName.Equals(TEXT("bForce"), ESearchCase::IgnoreCase))
				{
					BoolProperty->SetPropertyValue(ValuePtr, true);
				}
			}
		}

		if (!bSetRotationMode)
		{
			return false;
		}

		Object->ProcessEvent(Function, Parameters);
		return true;
	}

	bool PatchLinkedOverlayAnimMasters(USkeletalMeshComponent* Mesh, UAnimInstance* MasterAnimInstance)
	{
		if (!Mesh || !MasterAnimInstance)
		{
			return false;
		}

		bool bChanged = false;
		const USkeletalMeshComponent* ConstMesh = Mesh;
		for (UAnimInstance* LinkedAnimInstance : ConstMesh->GetLinkedAnimInstances())
		{
			if (!LinkedAnimInstance || LinkedAnimInstance == MasterAnimInstance)
			{
				continue;
			}

			bChanged |= SetObjectPropertyValueByName(LinkedAnimInstance, TEXT("AnimBPMaster"), MasterAnimInstance);
		}

		return bChanged;
	}

	bool ApplyRotationModeAliasesToAnimInstance(UAnimInstance* AnimInstance, const TArray<FString>& Aliases)
	{
		if (!AnimInstance)
		{
			return false;
		}

		const bool bAiming = AliasesContainAiming(Aliases);
		bool bChanged = SetEnumLikePropertyValueByAliases(AnimInstance, TEXT("RotationMode"), Aliases);
		bChanged |= SetBoolPropertyByName(AnimInstance, TEXT("bIsAiming"), bAiming);
		bChanged |= SetBoolPropertyByName(AnimInstance, TEXT("bAiming"), bAiming);
		bChanged |= SetBoolPropertyByName(AnimInstance, TEXT("IsAiming"), bAiming);
		return bChanged;
	}

	bool ApplyRotationModeAliasesToMesh(USkeletalMeshComponent* Mesh, const TArray<FString>& Aliases)
	{
		if (!Mesh)
		{
			return false;
		}

		bool bChanged = false;
		UAnimInstance* AnimInstance = Mesh->GetAnimInstance();
		bChanged |= ApplyRotationModeAliasesToAnimInstance(AnimInstance, Aliases);
		bChanged |= PatchLinkedOverlayAnimMasters(Mesh, AnimInstance);

		const USkeletalMeshComponent* ConstMesh = Mesh;
		for (UAnimInstance* LinkedAnimInstance : ConstMesh->GetLinkedAnimInstances())
		{
			bChanged |= ApplyRotationModeAliasesToAnimInstance(LinkedAnimInstance, Aliases);
		}

		return bChanged;
	}

	bool ReadNumericPropertyFromMeshAnimInstances(const USkeletalMeshComponent* Mesh, const FName PropertyName, double& OutValue)
	{
		if (!Mesh)
		{
			return false;
		}

		if (const UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
		{
			if (GetNumericPropertyValueByName(AnimInstance, PropertyName, OutValue))
			{
				return true;
			}
		}

		for (const UAnimInstance* LinkedAnimInstance : Mesh->GetLinkedAnimInstances())
		{
			if (GetNumericPropertyValueByName(LinkedAnimInstance, PropertyName, OutValue))
			{
				return true;
			}
		}

		return false;
	}

	bool ReadTurnYawOffset(ACharacter* Character, double& OutYawOffset)
	{
		if (!Character)
		{
			return false;
		}

		static const FName YawOffsetPropertyNames[] =
		{
			TEXT("YawOffset"),
			TEXT("TurnYawOffset"),
			TEXT("AimYawOffset"),
			TEXT("RotationYawOffset")
		};

		USkeletalMeshComponent* Mesh = Character->GetMesh();
		for (const FName PropertyName : YawOffsetPropertyNames)
		{
			if (ReadNumericPropertyFromMeshAnimInstances(Mesh, PropertyName, OutYawOffset)
				|| GetNumericPropertyValueByName(Character, PropertyName, OutYawOffset))
			{
				OutYawOffset = FRotator::NormalizeAxis(OutYawOffset);
				return true;
			}
		}

		return false;
	}

	double ReadALSTurnPitch(ACharacter* Character, const FRotator& BaseAimRotation)
	{
		double Pitch = BaseAimRotation.Pitch;
		if (!Character)
		{
			return Pitch;
		}

		double MPSPitch = 0.0;
		if (GetNumericPropertyValueByName(Character, TEXT("Pitch"), MPSPitch)
			|| ReadNumericPropertyFromMeshAnimInstances(Character->GetMesh(), TEXT("Pitch"), MPSPitch))
		{
			Pitch = MPSPitch;
		}

		return FRotator::NormalizeAxis(Pitch);
	}

	bool ReadALSAimBool(ACharacter* Character, bool& bOutAiming)
	{
		if (!Character)
		{
			return false;
		}

		static const FName AimPropertyNames[] =
		{
			TEXT("bIsAiming"),
			TEXT("IsAiming"),
			TEXT("IsAiming?"),
			TEXT("bAiming"),
			TEXT("Aiming"),
			TEXT("bADS"),
			TEXT("IsADS"),
			TEXT("bIsADS")
		};

		for (const FName PropertyName : AimPropertyNames)
		{
			if (GetBoolPropertyValueByName(Character, PropertyName, bOutAiming))
			{
				return true;
			}
		}

		if (const USkeletalMeshComponent* Mesh = Character->GetMesh())
		{
			if (const UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
			{
				for (const FName PropertyName : AimPropertyNames)
				{
					if (GetBoolPropertyValueByName(AnimInstance, PropertyName, bOutAiming))
					{
						return true;
					}
				}
			}
		}

		return false;
	}

	bool ApplyViewModeToAnimInstance(UAnimInstance* AnimInstance, const TArray<FString>& ViewModeAliases)
	{
		return SetEnumLikePropertyValueByAliasesOrValue(AnimInstance, TEXT("ViewMode"), ViewModeAliases, 0);
	}

	bool ApplyLookingDirectionToAnimInstance(UAnimInstance* AnimInstance, const TArray<FString>& RotationModeAliases)
	{
		if (!AnimInstance)
		{
			return false;
		}

		bool bChanged = SetEnumLikePropertyValueByAliasesOrValue(AnimInstance, TEXT("RotationMode"), RotationModeAliases, 1);
		bChanged |= SetBoolPropertyByName(AnimInstance, TEXT("bIsAiming"), false);
		bChanged |= SetBoolPropertyByName(AnimInstance, TEXT("bAiming"), false);
		bChanged |= SetBoolPropertyByName(AnimInstance, TEXT("IsAiming"), false);
		return bChanged;
	}

	bool ApplyALSTurnValuesToObject(UObject* Object, const FRotator& AimingRotation, const FVector2D& AimingAngle, const double AimYawRate)
	{
		if (!Object)
		{
			return false;
		}

		bool bChanged = SetRotatorPropertyValueByName(Object, TEXT("AimingRotation"), AimingRotation);
		bChanged |= SetNumericPropertyValueByName(Object, TEXT("AimYawRate"), AimYawRate);
		bChanged |= SetVector2DPropertyValueByName(Object, TEXT("AimingAngle"), AimingAngle);
		return bChanged;
	}

	bool ApplyALSTurnValuesToMesh(USkeletalMeshComponent* Mesh, const FRotator& AimingRotation, const FVector2D& AimingAngle, const double AimYawRate, const bool bApplyLookingDirection)
	{
		if (!Mesh)
		{
			return false;
		}

		bool bChanged = false;
		const TArray<FString> ViewModeAliases = BuildThirdPersonViewModeAliases();
		const TArray<FString> RotationModeAliases = BuildLookingDirectionAliases();

		if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
		{
			bChanged |= ApplyALSTurnValuesToObject(AnimInstance, AimingRotation, AimingAngle, AimYawRate);
			bChanged |= ApplyViewModeToAnimInstance(AnimInstance, ViewModeAliases);
			if (bApplyLookingDirection)
			{
				bChanged |= ApplyLookingDirectionToAnimInstance(AnimInstance, RotationModeAliases);
			}
		}

		const USkeletalMeshComponent* ConstMesh = Mesh;
		for (UAnimInstance* LinkedAnimInstance : ConstMesh->GetLinkedAnimInstances())
		{
			bChanged |= ApplyALSTurnValuesToObject(LinkedAnimInstance, AimingRotation, AimingAngle, AimYawRate);
			bChanged |= ApplyViewModeToAnimInstance(LinkedAnimInstance, ViewModeAliases);
			if (bApplyLookingDirection)
			{
				bChanged |= ApplyLookingDirectionToAnimInstance(LinkedAnimInstance, RotationModeAliases);
			}
		}

		return bChanged;
	}

	void CleanupTurnBridgeStatesIfNeeded()
	{
		if (GALSTurnInPlaceBridgeStates.Num() < 128)
		{
			return;
		}

		for (auto It = GALSTurnInPlaceBridgeStates.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
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

	bool IsAttachedMuzzleFlashTarget(const UFXSystemAsset* EmitterTemplate, const FName AttachPointName)
	{
		return EmitterTemplate
			&& EmitterTemplate->GetName().Equals(TEXT("NS_MuzzleFlash"), ESearchCase::IgnoreCase)
			&& AttachPointName.IsEqual(TEXT("Muzzle"), ENameCase::IgnoreCase);
	}

	const TCHAR* AttachLocationTypeToString(const EAttachLocation::Type LocationType);

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

	bool AssetPropertyMatches(const FProperty* Property, const void* Container, const UFXSystemAsset* EmitterTemplate)
	{
		if (!Property || !Container || !EmitterTemplate)
		{
			return false;
		}

		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			const UObject* Value = ObjectProperty->GetObjectPropertyValue_InContainer(Container);
			return Value == EmitterTemplate
				|| (Value && Value->GetPathName().Equals(EmitterTemplate->GetPathName(), ESearchCase::IgnoreCase));
		}

		if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
		{
			const FSoftObjectPtr* Value = SoftObjectProperty->ContainerPtrToValuePtr<FSoftObjectPtr>(Container);
			if (!Value)
			{
				return false;
			}

			const FSoftObjectPath ValuePath = Value->ToSoftObjectPath();
			const FSoftObjectPath TemplatePath(EmitterTemplate);
			return ValuePath == TemplatePath
				|| ValuePath.GetAssetPathString().Equals(TemplatePath.GetAssetPathString(), ESearchCase::IgnoreCase);
		}

		return false;
	}

	bool TryGetAttachedParticleScaleFromWeaponTable(const UFXSystemAsset* EmitterTemplate, FVector& OutScale, FName& OutRowName)
	{
		static const TCHAR* WeaponTablePath = TEXT("/Game/MP_System_V3/Game/Blueprints/DataTables/DT_Weapons.DT_Weapons");

		UDataTable* WeaponTable = LoadObject<UDataTable>(nullptr, WeaponTablePath);
		if (!WeaponTable || !WeaponTable->GetRowStruct())
		{
			return false;
		}

		const FStructProperty* FeaturesProperty = CastField<FStructProperty>(FindPropertyByName(WeaponTable->GetRowStruct(), TEXT("Features")));
		if (!FeaturesProperty || !FeaturesProperty->Struct)
		{
			return false;
		}

		const FProperty* AttachedParticleProperty = FindPropertyByName(FeaturesProperty->Struct, TEXT("Attached Particle"));
		const FStructProperty* AttachedOffsetProperty = CastField<FStructProperty>(FindPropertyByName(FeaturesProperty->Struct, TEXT("Attached offset")));
		if (!AttachedParticleProperty || !AttachedOffsetProperty || AttachedOffsetProperty->Struct != TBaseStructure<FTransform>::Get())
		{
			return false;
		}

		for (const TPair<FName, uint8*>& RowPair : WeaponTable->GetRowMap())
		{
			if (!RowPair.Value)
			{
				continue;
			}

			const void* FeaturesValue = FeaturesProperty->ContainerPtrToValuePtr<void>(RowPair.Value);
			if (!AssetPropertyMatches(AttachedParticleProperty, FeaturesValue, EmitterTemplate))
			{
				continue;
			}

			const FTransform* AttachedOffset = AttachedOffsetProperty->ContainerPtrToValuePtr<FTransform>(FeaturesValue);
			if (!AttachedOffset)
			{
				return false;
			}

			OutScale = AttachedOffset->GetScale3D();
			OutRowName = RowPair.Key;
			return !OutScale.IsNearlyZero();
		}

		return false;
	}

	void ApplyAttachedMuzzleFlashScale(
		const UFXSystemAsset* EmitterTemplate,
		const FName AttachPointName,
		FVector& Scale)
	{
		if (!IsAttachedMuzzleFlashTarget(EmitterTemplate, AttachPointName) || !Scale.Equals(FVector(1.0f), KINDA_SMALL_NUMBER))
		{
			return;
		}

		FVector TableScale = FVector::OneVector;
		FName RowName = NAME_None;
		if (!TryGetAttachedParticleScaleFromWeaponTable(EmitterTemplate, TableScale, RowName))
		{
			return;
		}

		UE_LOG(
			LogTouchMeRuntimeTrace,
			Display,
			TEXT("[MuzzleFXScaleFix] Asset=%s Row=%s IncomingScale=%s -> TableAttachedScale=%s"),
			*EmitterTemplate->GetPathName(),
			*RowName.ToString(),
			*Scale.ToCompactString(),
			*TableScale.ToCompactString());

		Scale = TableScale;
	}

	void FixWorldSpaceMuzzleFlashOffset(
		const UFXSystemAsset* EmitterTemplate,
		const USceneComponent* AttachToComponent,
		const FName AttachPointName,
		FVector& Location,
		FRotator& Rotation,
		const EAttachLocation::Type LocationType)
	{
		if (!IsAttachedMuzzleFlashTarget(EmitterTemplate, AttachPointName) || !AttachToComponent)
		{
			return;
		}

		if (LocationType != EAttachLocation::SnapToTarget && LocationType != EAttachLocation::SnapToTargetIncludingScale)
		{
			return;
		}

		if (!AttachToComponent->DoesSocketExist(AttachPointName))
		{
			return;
		}

		const FTransform SocketWorldTransform = AttachToComponent->GetSocketTransform(AttachPointName, RTS_World);
		const FVector SocketWorldLocation = SocketWorldTransform.GetLocation();

		UE_LOG(
			LogTouchMeRuntimeTrace,
			Display,
			TEXT("[MuzzleFXAttachFix] Asset=%s AttachTo=%s AttachPoint=%s IncomingLoc=%s IncomingRot=%s SocketWorldLoc=%s SocketWorldRot=%s LocationType=%s -> RelativeLoc=Zero RelativeRot=Zero"),
			*EmitterTemplate->GetPathName(),
			*AttachToComponent->GetPathName(),
			*AttachPointName.ToString(),
			*Location.ToCompactString(),
			*Rotation.ToCompactString(),
			*SocketWorldLocation.ToCompactString(),
			*SocketWorldTransform.Rotator().ToCompactString(),
			AttachLocationTypeToString(LocationType));

		Location = FVector::ZeroVector;
		Rotation = FRotator::ZeroRotator;
	}

	const TCHAR* AttachLocationTypeToString(const EAttachLocation::Type LocationType)
	{
		switch (LocationType)
		{
		case EAttachLocation::KeepRelativeOffset:
			return TEXT("KeepRelativeOffset");
		case EAttachLocation::KeepWorldPosition:
			return TEXT("KeepWorldPosition");
		case EAttachLocation::SnapToTarget:
			return TEXT("SnapToTarget");
		case EAttachLocation::SnapToTargetIncludingScale:
			return TEXT("SnapToTargetIncludingScale");
		default:
			return TEXT("Unknown");
		}
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

UFXSystemComponent* UTMGameplayStatics::SpawnFXSystemAtLocation(
	const UObject* WorldContextObject,
	UFXSystemAsset* EmitterTemplate,
	FVector Location,
	FRotator Rotation,
	FVector Scale,
	bool bAutoDestroy,
	EPSCPoolMethod PoolingMethod,
	bool bAutoActivateSystem)
{
	if (!EmitterTemplate)
	{
		return nullptr;
	}

	if (UParticleSystem* CascadeSystem = Cast<UParticleSystem>(EmitterTemplate))
	{
		UParticleSystemComponent* Component = UGameplayStatics::SpawnEmitterAtLocation(
			WorldContextObject,
			CascadeSystem,
			Location,
			Rotation,
			Scale,
			bAutoDestroy,
			PoolingMethod,
			bAutoActivateSystem);
		return Component;
	}

	if (UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(EmitterTemplate))
	{
		UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			WorldContextObject,
			NiagaraSystem,
			Location,
			Rotation,
			Scale,
			bAutoDestroy,
			bAutoActivateSystem,
			ToNiagaraPooling(PoolingMethod));
		return Component;
	}

	return nullptr;
}

UFXSystemComponent* UTMGameplayStatics::SpawnFXSystemAttached(
	UFXSystemAsset* EmitterTemplate,
	USceneComponent* AttachToComponent,
	FName AttachPointName,
	FVector Location,
	FRotator Rotation,
	FVector Scale,
	EAttachLocation::Type LocationType,
	bool bAutoDestroy,
	EPSCPoolMethod PoolingMethod,
	bool bAutoActivate)
{
	if (!EmitterTemplate || !AttachToComponent)
	{
		return nullptr;
	}

	TMGameplayStatics::FixWorldSpaceMuzzleFlashOffset(EmitterTemplate, AttachToComponent, AttachPointName, Location, Rotation, LocationType);
	TMGameplayStatics::ApplyAttachedMuzzleFlashScale(EmitterTemplate, AttachPointName, Scale);

	if (UParticleSystem* CascadeSystem = Cast<UParticleSystem>(EmitterTemplate))
	{
		UParticleSystemComponent* Component = UGameplayStatics::SpawnEmitterAttached(
			CascadeSystem,
			AttachToComponent,
			AttachPointName,
			Location,
			Rotation,
			Scale,
			LocationType,
			bAutoDestroy,
			PoolingMethod,
			bAutoActivate);
		return Component;
	}

	if (UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(EmitterTemplate))
	{
		UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAttached(
			NiagaraSystem,
			AttachToComponent,
			AttachPointName,
			Location,
			Rotation,
			Scale,
			LocationType,
			bAutoDestroy,
			ToNiagaraPooling(PoolingMethod),
			bAutoActivate);
		return Component;
	}

	return nullptr;
}

void UTMGameplayStatics::PlayWeaponSpawnFeedbackForActor(AActor* WeaponActor)
{
	AGun* Gun = Cast<AGun>(WeaponActor);
	if (!Gun)
	{
		return;
	}

	Gun->PlayWeaponSpawnFeedback();
}

void UTMGameplayStatics::MarketSoundRoom(bool enable)
{
	
}

bool UTMGameplayStatics::ApplyMPSOverlayPose(ACharacter* Character, UObject* ActiveWeapon)
{
	if (!Character)
	{
		if (IsTouchMeRuntimeTraceEnabled())
		{
			UE_LOG(
				LogTouchMeRuntimeTrace,
				Warning,
				TEXT("[ApplyMPSOverlayPose] Character=None ActiveWeapon=%s"),
				ActiveWeapon ? *ActiveWeapon->GetPathName() : TEXT("None"));
		}
		return false;
	}

	int64 PoseValue = 0;
	FString PoseDisplayName;
	TMGameplayStatics::GetEnumLikePropertyValue(ActiveWeapon, TEXT("DT_OverlayPose"), PoseValue, PoseDisplayName);
	if (IsTouchMeRuntimeTraceEnabled())
	{
		UE_LOG(
			LogTouchMeRuntimeTrace,
			Warning,
			TEXT("[ApplyMPSOverlayPose] Character=%s CharacterClass=%s ActiveWeapon=%s ActiveWeaponClass=%s PoseValue=%lld PoseDisplayName=%s"),
			*Character->GetPathName(),
			*Character->GetClass()->GetPathName(),
			ActiveWeapon ? *ActiveWeapon->GetPathName() : TEXT("None"),
			ActiveWeapon ? *ActiveWeapon->GetClass()->GetPathName() : TEXT("None"),
			static_cast<long long>(PoseValue),
			*PoseDisplayName);
	}
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
		if (IsTouchMeRuntimeTraceEnabled())
		{
			UE_LOG(
				LogTouchMeRuntimeTrace,
				Warning,
				TEXT("[ApplyMPSOverlayPose] OverlayAnimClass=None PoseValue=%lld PoseDisplayName=%s"),
				static_cast<long long>(PoseValue),
				*PoseDisplayName);
		}
		return false;
	}

	if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
	{
		TMGameplayStatics::SetEnumLikePropertyValueByAliases(AnimInstance, TEXT("OverlayState"), OverlayStateAliases);
		if (IsTouchMeRuntimeTraceEnabled())
		{
			UE_LOG(
				LogTouchMeRuntimeTrace,
				Warning,
				TEXT("[ApplyMPSOverlayPose] AnimInstance=%s Class=%s OverlayAnimClass=%s"),
				*AnimInstance->GetPathName(),
				*AnimInstance->GetClass()->GetPathName(),
				*OverlayAnimClass->GetPathName());
		}
	}

	Mesh->LinkAnimClassLayers(OverlayAnimClass);
	TMGameplayStatics::PatchLinkedOverlayAnimMasters(Mesh, Mesh->GetAnimInstance());
	if (IsTouchMeRuntimeTraceEnabled())
	{
		UE_LOG(
			LogTouchMeRuntimeTrace,
			Warning,
			TEXT("[ApplyMPSOverlayPose] LinkAnimClassLayers done OverlayAnimClass=%s"),
			*OverlayAnimClass->GetPathName());
	}
	return true;
}

bool UTMGameplayStatics::ApplyALSAimState(ACharacter* Character, const bool bAiming)
{
	if (!Character)
	{
		return false;
	}

	const TArray<FString> RotationModeAliases = TMGameplayStatics::BuildRotationModeAliasesForAimState(Character, bAiming);
	bool bChanged = TMGameplayStatics::CallSetRotationModeFunction(Character, RotationModeAliases);
	bChanged |= TMGameplayStatics::SetEnumLikePropertyValueByAliases(Character, TEXT("RotationMode"), RotationModeAliases);
	bChanged |= TMGameplayStatics::SetBoolPropertyByName(Character, TEXT("bIsAiming"), bAiming);
	bChanged |= TMGameplayStatics::SetBoolPropertyByName(Character, TEXT("bAiming"), bAiming);
	bChanged |= TMGameplayStatics::SetBoolPropertyByName(Character, TEXT("IsAiming"), bAiming);
	bChanged |= TMGameplayStatics::SetBoolPropertyByName(Character, TEXT("bADS"), bAiming);
	bChanged |= TMGameplayStatics::SetBoolPropertyByName(Character, TEXT("IsADS"), bAiming);
	bChanged |= TMGameplayStatics::ApplyRotationModeAliasesToMesh(Character->GetMesh(), RotationModeAliases);

	if (IsTouchMeRuntimeTraceEnabled())
	{
		UE_LOG(
			LogTouchMeRuntimeTrace,
			Warning,
			TEXT("[ApplyALSAimState] Character=%s bAiming=%d Changed=%d Aliases=%s"),
			*Character->GetPathName(),
			bAiming ? 1 : 0,
			bChanged ? 1 : 0,
			*FString::Join(RotationModeAliases, TEXT(",")));
	}

	return bChanged;
}

bool UTMGameplayStatics::ApplyALSTurnInPlaceState(ACharacter* Character, const float DeltaSeconds)
{
	if (!Character)
	{
		return false;
	}

	const FRotator ActorRotation = Character->GetActorRotation();
	const FRotator BaseAimRotation = Character->GetBaseAimRotation();

	double YawOffset = 0.0;
	if (!TMGameplayStatics::ReadTurnYawOffset(Character, YawOffset))
	{
		YawOffset = FRotator::NormalizeAxis(BaseAimRotation.Yaw - ActorRotation.Yaw);
	}

	const double SourceYaw = FRotator::NormalizeAxis(ActorRotation.Yaw + YawOffset);
	const double Pitch = TMGameplayStatics::ReadALSTurnPitch(Character, BaseAimRotation);
	const FRotator AimingRotation(Pitch, SourceYaw, 0.0);
	const FVector2D AimingAngle(YawOffset, Pitch);

	TMGameplayStatics::CleanupTurnBridgeStatesIfNeeded();
	const TWeakObjectPtr<ACharacter> CharacterKey(Character);
	TMGameplayStatics::FALSTurnInPlaceBridgeState& BridgeState = TMGameplayStatics::GALSTurnInPlaceBridgeStates.FindOrAdd(CharacterKey);
	double AimYawRate = 0.0;
	if (BridgeState.bHasSourceYaw && DeltaSeconds > SMALL_NUMBER)
	{
		const double DeltaYaw = FMath::Abs(FRotator::NormalizeAxis(SourceYaw - BridgeState.SourceYaw));
		AimYawRate = FMath::Min(DeltaYaw / static_cast<double>(DeltaSeconds), 720.0);
	}
	BridgeState.bHasSourceYaw = true;
	BridgeState.SourceYaw = SourceYaw;

	bool bAiming = false;
	const bool bApplyLookingDirection = !TMGameplayStatics::ReadALSAimBool(Character, bAiming) || !bAiming;
	const TArray<FString> ViewModeAliases = TMGameplayStatics::BuildThirdPersonViewModeAliases();
	const TArray<FString> RotationModeAliases = TMGameplayStatics::BuildLookingDirectionAliases();

	bool bChanged = TMGameplayStatics::ApplyALSTurnValuesToObject(Character, AimingRotation, AimingAngle, AimYawRate);
	bChanged |= TMGameplayStatics::SetEnumLikePropertyValueByAliasesOrValue(Character, TEXT("ViewMode"), ViewModeAliases, 0);
	if (bApplyLookingDirection)
	{
		bChanged |= TMGameplayStatics::SetEnumLikePropertyValueByAliasesOrValue(Character, TEXT("RotationMode"), RotationModeAliases, 1);
	}
	bChanged |= TMGameplayStatics::ApplyALSTurnValuesToMesh(Character->GetMesh(), AimingRotation, AimingAngle, AimYawRate, bApplyLookingDirection);

	if (bChanged && IsTouchMeRuntimeTraceEnabled())
	{
		UE_LOG(
			LogTouchMeRuntimeTrace,
			Verbose,
			TEXT("[ALSTurnInPlaceBridge] Character=%s YawOffset=%.2f SourceYaw=%.2f AimYawRate=%.2f"),
			*Character->GetPathName(),
			YawOffset,
			SourceYaw,
			AimYawRate);
	}

	return bChanged;
}

bool UTMGameplayStatics::DumpAnimBlueprintGraphLinks()
{
#if WITH_EDITOR
	static const TCHAR* TargetAnimBlueprintPaths[] =
	{
		TEXT("/Game/MP_System_V3/Game/Blueprints/Core/AnimBP_MPS_Master.AnimBP_MPS_Master"),
		TEXT("/Game/Test/MPVS_SkeletonProbe/ImportedOnALS/ABP_UE5_MPSBones_OnALS.ABP_UE5_MPSBones_OnALS")
	};

	FString Dump;
	for (const TCHAR* TargetAnimBlueprintPath : TargetAnimBlueprintPaths)
	{
		TMGameplayStatics::AppendAnimBlueprintGraphDump(TargetAnimBlueprintPath, Dump);
		Dump += TEXT("\n");
	}

	const FString OutPath = FPaths::ProjectSavedDir() / TEXT("Codex/anim_graph_dump_cpp.txt");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutPath), true);
	const bool bSaved = FFileHelper::SaveStringToFile(Dump, *OutPath);
	UE_LOG(LogTemp, Display, TEXT("[TMAnimGraphDump] Saved=%d Path=%s"), bSaved ? 1 : 0, *OutPath);
	return bSaved;
#else
	return false;
#endif
}

bool UTMGameplayStatics::FixMPSBonesAimTargetGraph()
{
#if WITH_EDITOR
	static const TCHAR* TargetAnimBlueprintPath =
		TEXT("/Game/Test/MPVS_SkeletonProbe/ImportedOnALS/ABP_UE5_MPSBones_OnALS.ABP_UE5_MPSBones_OnALS");

	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, TargetAnimBlueprintPath);
	if (!AnimBlueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("[TMFixAimTargetGraph] Failed to load %s"), TargetAnimBlueprintPath);
		return false;
	}

	bool bChanged = false;
	TArray<UEdGraph*> Graphs;
	AnimBlueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph || Graph->GetFName() != TEXT("Pose_AimTarget"))
		{
			continue;
		}

		for (UEdGraphNode* GraphNode : Graph->Nodes)
		{
			UAnimGraphNode_ModifyBone* ModifyBoneNode = Cast<UAnimGraphNode_ModifyBone>(GraphNode);
			if (!ModifyBoneNode
				|| ModifyBoneNode->Node.BoneToModify.BoneName != TEXT("VB Control")
				|| !FMath::IsNearlyEqual(ModifyBoneNode->Node.Translation.Y, 16.0))
			{
				continue;
			}

			const FRotator MPRotation(-90.0, 0.0, 0.0);
			if (!ModifyBoneNode->Node.Rotation.Equals(MPRotation, KINDA_SMALL_NUMBER))
			{
				AnimBlueprint->Modify();
				Graph->Modify();
				ModifyBoneNode->Modify();
				ModifyBoneNode->Node.Rotation = MPRotation;
				ModifyBoneNode->ReconstructNode();
				bChanged = true;
			}
		}
	}

	if (!bChanged)
	{
		UE_LOG(LogTemp, Display, TEXT("[TMFixAimTargetGraph] No changes needed for %s"), TargetAnimBlueprintPath);
		return true;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
	FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);

	UPackage* Package = AnimBlueprint->GetOutermost();
	const FString PackageFilename =
		FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	const bool bSaved = UPackage::SavePackage(Package, AnimBlueprint, *PackageFilename, SaveArgs);
	UE_LOG(
		LogTemp,
		Display,
		TEXT("[TMFixAimTargetGraph] Changed=1 Saved=%d Asset=%s"),
		bSaved ? 1 : 0,
		*AnimBlueprint->GetPathName());
	return bSaved;
#else
	return false;
#endif
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

