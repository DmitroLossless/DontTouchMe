#include "TMAnimGraphPatchCommandlet.h"

#if WITH_EDITOR

#include "AnimGraphNode_CopyBone.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_Fabrik.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_LocalRefPose.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ModifyBone.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "BoneControllers/AnimNode_ModifyBone.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNodeUtils.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "HAL/PlatformFileManager.h"
#include "../Gun/FakeGunAnimInstance.h"
#include "../Gun/Gun.h"
#include "../TMGameplayStatics.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Blueprint/WidgetTree.h"
#include "Components/AudioComponent.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ContentWidget.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/RichTextBlock.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "EditorReimportHandler.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_GetDataTableRow.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_Select.h"
#include "K2Node_Self.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SlateSound.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"
#include "UObject/Class.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace
{
	const TCHAR* TargetAnimBlueprintPaths[] =
	{
		TEXT("/Game/MP_System_V3/Game/Blueprints/Core/AnimBP_MPS_Master.AnimBP_MPS_Master"),
		TEXT("/Game/Test/MPVS_SkeletonProbe/ImportedOnALS/ABP_UE5_MPSBones_OnALS.ABP_UE5_MPSBones_OnALS")
	};

	const TCHAR* TMUIButtonPushSoundPath =
		TEXT("/Game/AGLS/Audio/Foley/Button/ButtonPush_S011FO_80.ButtonPush_S011FO_80");
	const TCHAR* TMMainMenuSoundtrackPath =
		TEXT("/Game/Sound/MainCulto.MainCulto");
	const TCHAR* TMMainMenuSoundtrackPatchComment =
		TEXT("TM: moved MainCulto start from W_Intro to W_MainMenu");

	bool TMClearReadOnlyFile(const FString& Filename, const TCHAR* LogPrefix)
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (!PlatformFile.FileExists(*Filename) || !PlatformFile.IsReadOnly(*Filename))
		{
			return true;
		}

		if (!PlatformFile.SetReadOnly(*Filename, false))
		{
			UE_LOG(LogTemp, Error, TEXT("[%s] Failed to clear read-only: %s"), LogPrefix, *Filename);
			return false;
		}

		UE_LOG(LogTemp, Display, TEXT("[%s] Cleared read-only: %s"), LogPrefix, *Filename);
		return true;
	}

	bool TMSavePackageForAsset(UObject* Asset, const TCHAR* LogPrefix)
	{
		if (!Asset)
		{
			return false;
		}

		UPackage* Package = Asset->GetOutermost();
		const FString PackageFilename = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension());

		if (!TMClearReadOnlyFile(PackageFilename, LogPrefix))
		{
			return false;
		}

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;

		if (!UPackage::SavePackage(Package, nullptr, *PackageFilename, SaveArgs))
		{
			UE_LOG(LogTemp, Error, TEXT("[%s] Failed to save package: %s"), LogPrefix, *PackageFilename);
			return false;
		}

		UE_LOG(LogTemp, Display, TEXT("[%s] Saved package: %s"), LogPrefix, *PackageFilename);
		return true;
	}

	UEdGraphPin* TMFindPinByName(UEdGraphNode* Node, const FName PinName, const EEdGraphPinDirection Direction)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->PinName == PinName && Pin->Direction == Direction)
			{
				return Pin;
			}
		}

		return nullptr;
	}

	UEdGraphPin* TMFindFirstDataPin(UEdGraphNode* Node, const EEdGraphPinDirection Direction)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Direction && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				return Pin;
			}
		}

		return nullptr;
	}

	UEdGraph* TMFindGraphByName(const UBlueprint* Blueprint, const FName GraphName)
	{
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);

		for (UEdGraph* Graph : Graphs)
		{
			if (Graph && Graph->GetFName() == GraphName)
			{
				return Graph;
			}
		}

		return nullptr;
	}

	bool TMIsFloatPin(const UEdGraphPin* Pin)
	{
		return Pin
			&& (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real
				|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Float
				|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Double);
	}

	bool TMExtractFloatPinDefault(const UEdGraphPin* Pin, double& OutValue)
	{
		if (!Pin)
		{
			return false;
		}

		FString DefaultValue = Pin->DefaultValue;
		if (DefaultValue.IsEmpty())
		{
			DefaultValue = Pin->AutogeneratedDefaultValue;
		}

		if (DefaultValue.IsEmpty())
		{
			return false;
		}

		LexFromString(OutValue, *DefaultValue);
		return true;
	}

	bool TMIsVectorAxisPin(const UEdGraphPin* Pin, const TCHAR* AxisName)
	{
		if (!Pin)
		{
			return false;
		}

		const FString PinName = Pin->PinName.ToString();
		const FString FriendlyName = Pin->PinFriendlyName.ToString();
		return PinName.Equals(AxisName, ESearchCase::IgnoreCase)
			|| PinName.EndsWith(FString::Printf(TEXT("_%s"), AxisName), ESearchCase::IgnoreCase)
			|| PinName.EndsWith(FString::Printf(TEXT(".%s"), AxisName), ESearchCase::IgnoreCase)
			|| FriendlyName.Equals(AxisName, ESearchCase::IgnoreCase);
	}

	bool TMGetVectorAxisGroupFromText(const FString& Text, const TCHAR* AxisName, FString& OutGroup)
	{
		if (Text.Equals(AxisName, ESearchCase::IgnoreCase))
		{
			OutGroup.Reset();
			return true;
		}

		const FString UnderscoreSuffix = FString::Printf(TEXT("_%s"), AxisName);
		if (Text.EndsWith(UnderscoreSuffix, ESearchCase::IgnoreCase))
		{
			OutGroup = Text.LeftChop(UnderscoreSuffix.Len());
			return true;
		}

		const FString DotSuffix = FString::Printf(TEXT(".%s"), AxisName);
		if (Text.EndsWith(DotSuffix, ESearchCase::IgnoreCase))
		{
			OutGroup = Text.LeftChop(DotSuffix.Len());
			return true;
		}

		return false;
	}

	bool TMGetVectorAxisGroup(const UEdGraphPin* Pin, const TCHAR* AxisName, FString& OutGroup)
	{
		if (!Pin)
		{
			return false;
		}

		return TMGetVectorAxisGroupFromText(Pin->PinName.ToString(), AxisName, OutGroup)
			|| TMGetVectorAxisGroupFromText(Pin->PinFriendlyName.ToString(), AxisName, OutGroup);
	}

	UEdGraphPin* TMFindVectorSubPin(UEdGraphPin* Pin, const TCHAR* AxisName)
	{
		if (!Pin)
		{
			return nullptr;
		}

		for (UEdGraphPin* SubPin : Pin->SubPins)
		{
			if (TMIsVectorAxisPin(SubPin, AxisName))
			{
				return SubPin;
			}
		}

		for (UEdGraphPin* SubPin : Pin->SubPins)
		{
			UEdGraphPin* FoundPin = TMFindVectorSubPin(SubPin, AxisName);
			if (FoundPin)
			{
				return FoundPin;
			}
		}

		return nullptr;
	}

	UEdGraphPin* TMFindNodeAxisPin(
		UEdGraphNode* Node,
		const EEdGraphPinDirection Direction,
		const TCHAR* AxisName,
		const FString& Group)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			FString PinGroup;
			if (Pin
				&& Pin->Direction == Direction
				&& Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec
				&& TMGetVectorAxisGroup(Pin, AxisName, PinGroup)
				&& PinGroup == Group)
			{
				return Pin;
			}
		}

		return nullptr;
	}

	const UEdGraphPin* TMFindVectorSubPin(const UEdGraphPin* Pin, const TCHAR* AxisName)
	{
		return TMFindVectorSubPin(const_cast<UEdGraphPin*>(Pin), AxisName);
	}

	bool TMParseVectorDefaultString(const FString& DefaultValue, FVector& OutValue)
	{
		FString TrimmedValue = DefaultValue;
		TrimmedValue.TrimStartAndEndInline();
		TrimmedValue.RemoveFromStart(TEXT("("));
		TrimmedValue.RemoveFromEnd(TEXT(")"));

		if (TrimmedValue.IsEmpty())
		{
			return false;
		}

		if (OutValue.InitFromString(TrimmedValue))
		{
			return true;
		}

		TArray<FString> Components;
		if (TrimmedValue.ParseIntoArray(Components, TEXT(","), true) != 3)
		{
			return false;
		}

		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
		Components[0].TrimStartAndEndInline();
		Components[1].TrimStartAndEndInline();
		Components[2].TrimStartAndEndInline();
		LexFromString(X, *Components[0]);
		LexFromString(Y, *Components[1]);
		LexFromString(Z, *Components[2]);
		OutValue = FVector(X, Y, Z);
		return true;
	}

	bool TMExtractVectorPinDefault(const UEdGraphPin* Pin, FVector& OutValue)
	{
		if (!Pin)
		{
			return false;
		}

		if (Pin->SubPins.Num() > 0)
		{
			const UEdGraphPin* XPin = TMFindVectorSubPin(Pin, TEXT("X"));
			const UEdGraphPin* YPin = TMFindVectorSubPin(Pin, TEXT("Y"));
			const UEdGraphPin* ZPin = TMFindVectorSubPin(Pin, TEXT("Z"));
			double X = 0.0;
			double Y = 0.0;
			double Z = 0.0;
			if (TMExtractFloatPinDefault(XPin, X)
				&& TMExtractFloatPinDefault(YPin, Y)
				&& TMExtractFloatPinDefault(ZPin, Z))
			{
				OutValue = FVector(X, Y, Z);
				return true;
			}
		}

		FString DefaultValue = Pin->DefaultValue;
		if (DefaultValue.IsEmpty())
		{
			DefaultValue = Pin->AutogeneratedDefaultValue;
		}

		if (DefaultValue.IsEmpty())
		{
			return false;
		}

		FVector ParsedValue = FVector::ZeroVector;
		if (TMParseVectorDefaultString(DefaultValue, ParsedValue))
		{
			OutValue = ParsedValue;
			return true;
		}

		return false;
	}

	bool TMSetVectorPinDefault(const UEdGraphSchema* Schema, UEdGraphPin* Pin, const FVector& Value)
	{
		if (!Schema || !Pin)
		{
			return false;
		}

		if (Pin->SubPins.Num() > 0)
		{
			UEdGraphPin* XPin = TMFindVectorSubPin(Pin, TEXT("X"));
			UEdGraphPin* YPin = TMFindVectorSubPin(Pin, TEXT("Y"));
			UEdGraphPin* ZPin = TMFindVectorSubPin(Pin, TEXT("Z"));
			if (!XPin || !YPin || !ZPin)
			{
				return false;
			}

			Schema->TrySetDefaultValue(*XPin, FString::SanitizeFloat(Value.X));
			Schema->TrySetDefaultValue(*YPin, FString::SanitizeFloat(Value.Y));
			Schema->TrySetDefaultValue(*ZPin, FString::SanitizeFloat(Value.Z));
			return true;
		}

		Schema->TrySetDefaultValue(
			*Pin,
			FString::Printf(
				TEXT("%s,%s,%s"),
				*FString::SanitizeFloat(Value.X),
				*FString::SanitizeFloat(Value.Y),
				*FString::SanitizeFloat(Value.Z)));
		return true;
	}

	void TMDumpNodeVectorPins(const UEdGraphNode* Node, const TCHAR* Label)
	{
		if (!Node)
		{
			return;
		}

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			FVector Value = FVector::ZeroVector;
			if (!TMExtractVectorPinDefault(Pin, Value))
			{
				continue;
			}

			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMMenuViewerLH] %s Node=%s Class=%s Pin=%s Default=%s Links=%d SubPins=%d"),
				Label,
				*Node->GetName(),
				*Node->GetClass()->GetName(),
				*Pin->PinName.ToString(),
				*Value.ToString(),
				Pin->LinkedTo.Num(),
				Pin->SubPins.Num());
		}
	}

	UEdGraphPin* TMFindVariableSetValuePin(UK2Node_VariableSet* SetNode, const FName VariableName)
	{
		if (!SetNode)
		{
			return nullptr;
		}

		UEdGraphPin* NamedPin = TMFindPinByName(SetNode, VariableName, EGPD_Input);
		if (NamedPin)
		{
			return NamedPin;
		}

		for (UEdGraphPin* Pin : SetNode->Pins)
		{
			if (Pin
				&& Pin->Direction == EGPD_Input
				&& Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec
				&& Pin->PinName != UEdGraphSchema_K2::PN_Self)
			{
				return Pin;
			}
		}

		return nullptr;
	}

	UAnimGraphNode_ModifyBone* TMFindModifyBoneNode(UEdGraph* Graph, const FName BoneName)
	{
		if (!Graph)
		{
			return nullptr;
		}

		for (UEdGraphNode* GraphNode : Graph->Nodes)
		{
			UAnimGraphNode_ModifyBone* ModifyBoneNode = Cast<UAnimGraphNode_ModifyBone>(GraphNode);
			if (ModifyBoneNode && ModifyBoneNode->Node.BoneToModify.BoneName == BoneName)
			{
				return ModifyBoneNode;
			}
		}

		return nullptr;
	}

	bool TMIsCameraOffsetRelevantBone(const FName BoneName)
	{
		static const TSet<FName> RelevantBones = {
			TEXT("Camera_FP"),
			TEXT("FP_Camera"),
			TEXT("VB Control"),
			TEXT("VB Hand_R"),
			TEXT("VB Hand_L"),
			TEXT("VB LHS_ik_hand_l"),
			TEXT("Weapon"),
			TEXT("hand_r"),
			TEXT("hand_l"),
			TEXT("ik_hand_l"),
			TEXT("head")
		};
		return RelevantBones.Contains(BoneName);
	}

	FString TMDescribePinLinks(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return TEXT("None");
		}

		TArray<FString> Links;
		for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			const UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
			FString NodeDescription = LinkedNode ? LinkedNode->GetName() : TEXT("None");
			if (const UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(LinkedNode))
			{
				NodeDescription += FString::Printf(
					TEXT("[%s]"),
					*CallFunctionNode->FunctionReference.GetMemberName().ToString());
			}
			else if (const UK2Node_VariableGet* VariableGetNode = Cast<UK2Node_VariableGet>(LinkedNode))
			{
				NodeDescription += FString::Printf(
					TEXT("[%s]"),
					*VariableGetNode->VariableReference.GetMemberName().ToString());
			}
			else if (const UK2Node_VariableSet* VariableSetNode = Cast<UK2Node_VariableSet>(LinkedNode))
			{
				NodeDescription += FString::Printf(
					TEXT("[%s]"),
					*VariableSetNode->VariableReference.GetMemberName().ToString());
			}

			Links.Add(FString::Printf(
				TEXT("%s.%s"),
				*NodeDescription,
				LinkedPin ? *LinkedPin->PinName.ToString() : TEXT("None")));
		}
		return Links.Num() > 0 ? FString::Join(Links, TEXT(", ")) : TEXT("None");
	}

	void TMDumpPinVerbose(const UEdGraphPin* Pin, const TCHAR* Label, const int32 Depth)
	{
		if (!Pin)
		{
			return;
		}

		const FString Indent = FString::ChrN(Depth * 2, TEXT(' '));
		const UObject* SubCategoryObject = Pin->PinType.PinSubCategoryObject.Get();
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMMenuViewerLH] %s%sPin=%s Friendly=%s Dir=%s Cat=%s SubCat=%s Obj=%s Default={%s} Auto={%s} Links=%s Parent=%s SubPins=%d"),
			Label,
			*Indent,
			*Pin->PinName.ToString(),
			*Pin->PinFriendlyName.ToString(),
			Pin->Direction == EGPD_Input ? TEXT("In") : TEXT("Out"),
			*Pin->PinType.PinCategory.ToString(),
			*Pin->PinType.PinSubCategory.ToString(),
			*GetNameSafe(SubCategoryObject),
			*Pin->DefaultValue,
			*Pin->AutogeneratedDefaultValue,
			*TMDescribePinLinks(Pin),
			Pin->ParentPin ? *Pin->ParentPin->PinName.ToString() : TEXT("None"),
			Pin->SubPins.Num());

		for (const UEdGraphPin* SubPin : Pin->SubPins)
		{
			TMDumpPinVerbose(SubPin, Label, Depth + 1);
		}
	}

	void TMDumpNodePinsVerbose(const UEdGraphNode* Node, const TCHAR* Label)
	{
		if (!Node)
		{
			return;
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMMenuViewerLH] VerboseDump %s Node=%s Class=%s Pins=%d"),
			Label,
			*Node->GetName(),
			*Node->GetClass()->GetName(),
			Node->Pins.Num());

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			TMDumpPinVerbose(Pin, Label, 1);
		}
	}

	bool TMShouldDumpPin(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return false;
		}

		return Pin->LinkedTo.Num() > 0
			|| Pin->PinName == TEXT("ComponentPose")
			|| Pin->PinName == TEXT("Pose")
			|| Pin->PinName == TEXT("Translation")
			|| Pin->PinName == TEXT("Rotation")
			|| Pin->PinName == TEXT("Scale");
	}

	void TMDumpNodePins(const UEdGraphNode* Node)
	{
		if (!Node)
		{
			return;
		}

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!TMShouldDumpPin(Pin))
			{
				continue;
			}

			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMAnimGraphDump]     Pin=%s Dir=%s Links=%s"),
				*Pin->PinName.ToString(),
				Pin->Direction == EGPD_Input ? TEXT("In") : TEXT("Out"),
				*TMDescribePinLinks(Pin));
		}
	}

	void TMDumpAlphaSourceNodePins(const UEdGraphNode* Node, const TCHAR* Label)
	{
		const UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node);
		if (!CallFunctionNode)
		{
			return;
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMAlphaSourceDump] %s Node=%s Function=%s Pins=%d"),
			Label,
			*CallFunctionNode->GetName(),
			*CallFunctionNode->FunctionReference.GetMemberName().ToString(),
			CallFunctionNode->Pins.Num());

		for (const UEdGraphPin* Pin : CallFunctionNode->Pins)
		{
			if (!Pin)
			{
				continue;
			}

			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMAlphaSourceDump] %s     Pin=%s Dir=%s Default={%s} Auto={%s} Links=%s"),
				Label,
				*Pin->PinName.ToString(),
				Pin->Direction == EGPD_Input ? TEXT("In") : TEXT("Out"),
				*Pin->DefaultValue,
				*Pin->AutogeneratedDefaultValue,
				*TMDescribePinLinks(Pin));
		}
	}

	void TMDumpRelevantAnimGraphLinks(const UAnimBlueprint* AnimBlueprint, const TCHAR* Label)
	{
		if (!AnimBlueprint)
		{
			return;
		}

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
					if (!TMIsCameraOffsetRelevantBone(BoneName))
					{
						continue;
					}

					UE_LOG(
						LogTemp,
						Display,
						TEXT("[TMAnimGraphDump] %s Graph=%s Node=%s Type=ModifyBone Bone=%s TM=%d RM=%d TS=%d RS=%d Loc=%s Rot=%s"),
						Label,
						*Graph->GetName(),
						*ModifyBoneNode->GetName(),
						*BoneName.ToString(),
						static_cast<int32>(ModifyBoneNode->Node.TranslationMode),
						static_cast<int32>(ModifyBoneNode->Node.RotationMode),
						static_cast<int32>(ModifyBoneNode->Node.TranslationSpace),
						static_cast<int32>(ModifyBoneNode->Node.RotationSpace),
						*ModifyBoneNode->Node.Translation.ToString(),
						*ModifyBoneNode->Node.Rotation.ToString());
					TMDumpNodePins(ModifyBoneNode);
				}
				else if (const UAnimGraphNode_CopyBone* CopyBoneNode = Cast<UAnimGraphNode_CopyBone>(GraphNode))
				{
					const FName SourceBoneName = CopyBoneNode->Node.SourceBone.BoneName;
					const FName TargetBoneName = CopyBoneNode->Node.TargetBone.BoneName;
					if (!TMIsCameraOffsetRelevantBone(SourceBoneName) && !TMIsCameraOffsetRelevantBone(TargetBoneName))
					{
						continue;
					}

					UE_LOG(
						LogTemp,
						Display,
						TEXT("[TMAnimGraphDump] %s Graph=%s Node=%s Type=CopyBone Source=%s Target=%s CopyT=%d CopyR=%d CopyS=%d Space=%d"),
						Label,
						*Graph->GetName(),
						*CopyBoneNode->GetName(),
						*SourceBoneName.ToString(),
						*TargetBoneName.ToString(),
						CopyBoneNode->Node.bCopyTranslation ? 1 : 0,
						CopyBoneNode->Node.bCopyRotation ? 1 : 0,
						CopyBoneNode->Node.bCopyScale ? 1 : 0,
						static_cast<int32>(CopyBoneNode->Node.ControlSpace));
					TMDumpNodePins(CopyBoneNode);
				}
				else if (const UAnimGraphNode_Fabrik* FabrikNode = Cast<UAnimGraphNode_Fabrik>(GraphNode))
				{
					const FName RootBoneName = FabrikNode->Node.RootBone.BoneName;
					const FName TipBoneName = FabrikNode->Node.TipBone.BoneName;
					const FName EffectorTargetName = FabrikNode->Node.EffectorTarget.bUseSocket
						? FabrikNode->Node.EffectorTarget.SocketReference.SocketName
						: FabrikNode->Node.EffectorTarget.BoneReference.BoneName;
					if (!TMIsCameraOffsetRelevantBone(RootBoneName)
						&& !TMIsCameraOffsetRelevantBone(TipBoneName)
						&& !TMIsCameraOffsetRelevantBone(EffectorTargetName))
					{
						continue;
					}

					UE_LOG(
						LogTemp,
						Display,
						TEXT("[TMAnimGraphDump] %s Graph=%s Node=%s Type=FABRIK Root=%s Tip=%s EffectorTarget=%s UseSocket=%d ETSpace=%d ERSource=%d AlphaType=%d Alpha=%.3f AlphaCurve=%s Effector=%s"),
						Label,
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
					TMDumpNodePins(FabrikNode);
					const UEdGraphPin* AlphaPin = nullptr;
					for (const UEdGraphPin* Pin : FabrikNode->Pins)
					{
						if (Pin && Pin->PinName == TEXT("Alpha") && Pin->Direction == EGPD_Input)
						{
							AlphaPin = Pin;
							break;
						}
					}
					if (AlphaPin)
					{
						for (const UEdGraphPin* LinkedPin : AlphaPin->LinkedTo)
						{
							TMDumpAlphaSourceNodePins(LinkedPin ? LinkedPin->GetOwningNode() : nullptr, Label);
						}
					}
				}
			}
		}
	}

	bool TMSetOptionalPinVisible(UAnimGraphNode_ModifyBone* ModifyBoneNode, const FName PropertyName, const bool bVisible)
	{
		if (!ModifyBoneNode)
		{
			return false;
		}

		for (FOptionalPinFromProperty& OptionalPin : ModifyBoneNode->ShowPinForProperties)
		{
			if (OptionalPin.PropertyName == PropertyName)
			{
				OptionalPin.bShowPin = bVisible;
				return true;
			}
		}

		return false;
	}

	bool TMIsBreakTransformCallNode(const UEdGraphNode* Node)
	{
		const UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node);
		return CallFunctionNode &&
			CallFunctionNode->FunctionReference.GetMemberName() == GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, BreakTransform);
	}

	bool TMIsComposeRotatorsCallNode(const UEdGraphNode* Node)
	{
		const UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node);
		return CallFunctionNode &&
			CallFunctionNode->FunctionReference.GetMemberName() == GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, ComposeRotators);
	}

	bool TMIsCameraOffsetVariableGetNode(const UEdGraphNode* Node)
	{
		const UK2Node_VariableGet* VariableGet = Cast<UK2Node_VariableGet>(Node);
		return VariableGet && VariableGet->VariableReference.GetMemberName() == TEXT("CameraWeaponOffset");
	}

	bool TMCollectCameraOffsetVariableGetsLinkedToNode(UEdGraphNode* Node, TSet<UEdGraphNode*>& NodesToRemove)
	{
		if (!Node)
		{
			return false;
		}

		bool bFoundCameraOffset = false;
		for (UEdGraphPin* NodePin : Node->Pins)
		{
			if (!NodePin)
			{
				continue;
			}

			for (UEdGraphPin* LinkedPin : NodePin->LinkedTo)
			{
				UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
				if (TMIsCameraOffsetVariableGetNode(LinkedNode))
				{
					NodesToRemove.Add(LinkedNode);
					bFoundCameraOffset = true;
				}
			}
		}

		return bFoundCameraOffset;
	}

	bool TMPinLinksToNode(const UEdGraphPin* Pin, const UEdGraphNode* Node)
	{
		if (!Pin || !Node)
		{
			return false;
		}

		for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			if (LinkedPin && LinkedPin->GetOwningNode() == Node)
			{
				return true;
			}
		}

		return false;
	}

	bool TMBlueprintHasTransformProperty(const UBlueprint* Blueprint, const FName PropertyName)
	{
		if (!Blueprint)
		{
			return false;
		}

		const UClass* ClassesToSearch[] =
		{
			Blueprint->SkeletonGeneratedClass,
			Blueprint->GeneratedClass
		};

		for (const UClass* ClassToSearch : ClassesToSearch)
		{
			if (!ClassToSearch)
			{
				continue;
			}

			const FStructProperty* StructProperty =
				CastField<FStructProperty>(ClassToSearch->FindPropertyByName(PropertyName));
			if (StructProperty && StructProperty->Struct == TBaseStructure<FTransform>::Get())
			{
				return true;
			}
		}

		return false;
	}

	bool TMEnsureTransformVariable(UBlueprint* Blueprint, const FName VariableName)
	{
		if (!Blueprint)
		{
			return false;
		}

		if (TMBlueprintHasTransformProperty(Blueprint, VariableName))
		{
			return true;
		}

		FEdGraphPinType TransformPinType;
		TransformPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		TransformPinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();

		if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, VariableName, TransformPinType))
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to add Transform variable %s to %s."), *VariableName.ToString(), *Blueprint->GetPathName());
			return false;
		}

		UE_LOG(LogTemp, Display, TEXT("Added Transform variable %s to %s."), *VariableName.ToString(), *Blueprint->GetPathName());
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		return TMBlueprintHasTransformProperty(Blueprint, VariableName);
	}

	void TMRemovePreviousCameraOffsetRotationPatch(UBlueprint* Blueprint, UEdGraph* Graph, UEdGraphPin* RotationPin)
	{
		if (!Blueprint || !Graph || !RotationPin)
		{
			return;
		}

		TSet<UEdGraphNode*> NodesToRemove;
		TArray<UEdGraphPin*> LinksToBreak;

		for (UEdGraphPin* LinkedPin : RotationPin->LinkedTo)
		{
			UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
			if (!LinkedNode)
			{
				continue;
			}

			if (TMIsBreakTransformCallNode(LinkedNode)
				|| LinkedNode->GetClass()->GetFName() == TEXT("K2Node_BreakStruct"))
			{
				TMCollectCameraOffsetVariableGetsLinkedToNode(LinkedNode, NodesToRemove);
				NodesToRemove.Add(LinkedNode);
				LinksToBreak.Add(LinkedPin);
			}
			else if (TMIsComposeRotatorsCallNode(LinkedNode))
			{
				for (UEdGraphPin* ComposePin : LinkedNode->Pins)
				{
					if (!ComposePin || ComposePin->Direction != EGPD_Input)
					{
						continue;
					}

					for (UEdGraphPin* ComposeInputPin : ComposePin->LinkedTo)
					{
						UEdGraphNode* ComposeInputNode = ComposeInputPin ? ComposeInputPin->GetOwningNode() : nullptr;
						if (TMIsBreakTransformCallNode(ComposeInputNode)
							|| (ComposeInputNode && ComposeInputNode->GetClass()->GetFName() == TEXT("K2Node_BreakStruct")))
						{
							TMCollectCameraOffsetVariableGetsLinkedToNode(ComposeInputNode, NodesToRemove);
							NodesToRemove.Add(ComposeInputNode);
						}
					}
				}

				NodesToRemove.Add(LinkedNode);
				LinksToBreak.Add(LinkedPin);
			}
		}

		if (NodesToRemove.Num() == 0)
		{
			return;
		}

		for (UEdGraphPin* LinkedPin : LinksToBreak)
		{
			if (LinkedPin)
			{
				RotationPin->BreakLinkTo(LinkedPin);
			}
		}

		for (UEdGraphNode* NodeToRemove : NodesToRemove)
		{
			if (NodeToRemove)
			{
				FBlueprintEditorUtils::RemoveNode(Blueprint, NodeToRemove, true);
			}
		}
	}

	bool TMCreateCameraOffsetRotationGetter(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		const FName CameraWeaponOffsetPropertyName,
		const int32 NodePosX,
		const int32 NodePosY,
		UEdGraphPin*& OutRotationPin)
	{
		OutRotationPin = nullptr;
		if (!Blueprint || !Graph)
		{
			return false;
		}

		FGraphNodeCreator<UK2Node_VariableGet> VariableGetCreator(*Graph);
		UK2Node_VariableGet* VariableGetNode = VariableGetCreator.CreateNode();
		VariableGetNode->VariableReference.SetSelfMember(CameraWeaponOffsetPropertyName);
		VariableGetNode->NodePosX = NodePosX;
		VariableGetNode->NodePosY = NodePosY;
		VariableGetCreator.Finalize();

		UFunction* BreakTransformFunction =
			UKismetMathLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, BreakTransform));
		if (!BreakTransformFunction)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to find UKismetMathLibrary::BreakTransform."));
			VariableGetNode->DestroyNode();
			return false;
		}

		FGraphNodeCreator<UK2Node_CallFunction> BreakTransformCreator(*Graph);
		UK2Node_CallFunction* BreakTransformNode = BreakTransformCreator.CreateNode();
		BreakTransformNode->SetFromFunction(BreakTransformFunction);
		BreakTransformNode->NodePosX = NodePosX + 240;
		BreakTransformNode->NodePosY = NodePosY;
		BreakTransformCreator.Finalize();

		UEdGraphPin* VariableOutputPin = TMFindPinByName(VariableGetNode, CameraWeaponOffsetPropertyName, EGPD_Output);
		if (!VariableOutputPin)
		{
			VariableOutputPin = TMFindFirstDataPin(VariableGetNode, EGPD_Output);
		}

		UEdGraphPin* BreakInputPin = TMFindPinByName(BreakTransformNode, TEXT("InTransform"), EGPD_Input);
		if (!BreakInputPin)
		{
			BreakInputPin = TMFindFirstDataPin(BreakTransformNode, EGPD_Input);
		}

		const UEdGraphSchema* Schema = Graph->GetSchema();
		if (!VariableOutputPin || !BreakInputPin || !Schema->TryCreateConnection(VariableOutputPin, BreakInputPin))
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to wire CameraWeaponOffset to BreakTransform."));
			VariableGetNode->DestroyNode();
			BreakTransformNode->DestroyNode();
			return false;
		}

		OutRotationPin = TMFindPinByName(BreakTransformNode, TEXT("Rotation"), EGPD_Output);
		if (!OutRotationPin)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to find BreakTransform Rotation output pin."));
			VariableGetNode->DestroyNode();
			BreakTransformNode->DestroyNode();
			return false;
		}

		return true;
	}

	bool TMCreateTransformBreakGetter(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		const FName TransformPropertyName,
		const int32 NodePosX,
		const int32 NodePosY,
		UEdGraphPin*& OutLocationPin,
		UEdGraphPin*& OutRotationPin)
	{
		OutLocationPin = nullptr;
		OutRotationPin = nullptr;
		if (!Blueprint || !Graph)
		{
			return false;
		}

		FGraphNodeCreator<UK2Node_VariableGet> VariableGetCreator(*Graph);
		UK2Node_VariableGet* VariableGetNode = VariableGetCreator.CreateNode();
		VariableGetNode->VariableReference.SetSelfMember(TransformPropertyName);
		VariableGetNode->NodePosX = NodePosX;
		VariableGetNode->NodePosY = NodePosY;
		VariableGetCreator.Finalize();

		UFunction* BreakTransformFunction =
			UKismetMathLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, BreakTransform));
		if (!BreakTransformFunction)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to find UKismetMathLibrary::BreakTransform."));
			VariableGetNode->DestroyNode();
			return false;
		}

		FGraphNodeCreator<UK2Node_CallFunction> BreakTransformCreator(*Graph);
		UK2Node_CallFunction* BreakTransformNode = BreakTransformCreator.CreateNode();
		BreakTransformNode->SetFromFunction(BreakTransformFunction);
		BreakTransformNode->NodePosX = NodePosX + 240;
		BreakTransformNode->NodePosY = NodePosY;
		BreakTransformCreator.Finalize();

		UEdGraphPin* VariableOutputPin = TMFindPinByName(VariableGetNode, TransformPropertyName, EGPD_Output);
		if (!VariableOutputPin)
		{
			VariableOutputPin = TMFindFirstDataPin(VariableGetNode, EGPD_Output);
		}

		UEdGraphPin* BreakInputPin = TMFindPinByName(BreakTransformNode, TEXT("InTransform"), EGPD_Input);
		if (!BreakInputPin)
		{
			BreakInputPin = TMFindFirstDataPin(BreakTransformNode, EGPD_Input);
		}

		const UEdGraphSchema* Schema = Graph->GetSchema();
		if (!VariableOutputPin || !BreakInputPin || !Schema || !Schema->TryCreateConnection(VariableOutputPin, BreakInputPin))
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to wire %s to BreakTransform."), *TransformPropertyName.ToString());
			VariableGetNode->DestroyNode();
			BreakTransformNode->DestroyNode();
			return false;
		}

		OutLocationPin = TMFindPinByName(BreakTransformNode, TEXT("Location"), EGPD_Output);
		OutRotationPin = TMFindPinByName(BreakTransformNode, TEXT("Rotation"), EGPD_Output);
		if (!OutLocationPin || !OutRotationPin)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to find BreakTransform outputs for %s."), *TransformPropertyName.ToString());
			VariableGetNode->DestroyNode();
			BreakTransformNode->DestroyNode();
			return false;
		}

		return true;
	}

	bool TMConnectCameraOffsetRotationToModifyBone(
		UAnimBlueprint* AnimBlueprint,
		UEdGraph* Graph,
		const FName BoneName,
		const EBoneControlSpace RotationSpace,
		const FName CameraWeaponOffsetPropertyName,
		const int32 NodeYOffset)
	{
		static const FName RotationPinName(TEXT("Rotation"));

		UAnimGraphNode_ModifyBone* ModifyBoneNode = TMFindModifyBoneNode(Graph, BoneName);
		if (!ModifyBoneNode)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to find ModifyBone node for bone %s in %s"), *BoneName.ToString(), *AnimBlueprint->GetPathName());
			return false;
		}

		ModifyBoneNode->Modify();
		TMSetOptionalPinVisible(ModifyBoneNode, RotationPinName, true);
		ModifyBoneNode->Node.RotationMode = BMM_Additive;
		ModifyBoneNode->Node.RotationSpace = RotationSpace;
		ModifyBoneNode->ReconstructNode();

		UEdGraphPin* RotationPin = TMFindPinByName(ModifyBoneNode, RotationPinName, EGPD_Input);
		if (!RotationPin)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to expose Rotation pin on ModifyBone node for %s in %s."), *BoneName.ToString(), *AnimBlueprint->GetPathName());
			return false;
		}

		TMRemovePreviousCameraOffsetRotationPatch(AnimBlueprint, Graph, RotationPin);

		UEdGraphPin* ExistingRotationSourcePin = nullptr;
		if (RotationPin->LinkedTo.Num() > 0)
		{
			ExistingRotationSourcePin = RotationPin->LinkedTo[0];
		}

		if (ExistingRotationSourcePin)
		{
			RotationPin->BreakLinkTo(ExistingRotationSourcePin);
		}

		UEdGraphPin* CameraOffsetRotationPin = nullptr;
		if (!TMCreateCameraOffsetRotationGetter(
				AnimBlueprint,
				Graph,
				CameraWeaponOffsetPropertyName,
				ModifyBoneNode->NodePosX - 760,
				ModifyBoneNode->NodePosY + NodeYOffset,
				CameraOffsetRotationPin))
		{
			return false;
		}

		const UEdGraphSchema* Schema = Graph->GetSchema();
		bool bLinkedRotationToBone = false;
		if (ExistingRotationSourcePin)
		{
			UFunction* ComposeRotatorsFunction =
				UKismetMathLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, ComposeRotators));
			if (!ComposeRotatorsFunction)
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to find UKismetMathLibrary::ComposeRotators."));
				return false;
			}

			FGraphNodeCreator<UK2Node_CallFunction> ComposeRotatorsCreator(*Graph);
			UK2Node_CallFunction* ComposeRotatorsNode = ComposeRotatorsCreator.CreateNode();
			ComposeRotatorsNode->SetFromFunction(ComposeRotatorsFunction);
			ComposeRotatorsNode->NodePosX = ModifyBoneNode->NodePosX - 280;
			ComposeRotatorsNode->NodePosY = ModifyBoneNode->NodePosY + NodeYOffset;
			ComposeRotatorsCreator.Finalize();

			UEdGraphPin* ComposeAPin = TMFindPinByName(ComposeRotatorsNode, TEXT("A"), EGPD_Input);
			UEdGraphPin* ComposeBPin = TMFindPinByName(ComposeRotatorsNode, TEXT("B"), EGPD_Input);
			UEdGraphPin* ComposeReturnPin = TMFindPinByName(ComposeRotatorsNode, UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
			if (!ComposeReturnPin)
			{
				ComposeReturnPin = TMFindFirstDataPin(ComposeRotatorsNode, EGPD_Output);
			}

			bLinkedRotationToBone =
				ExistingRotationSourcePin
				&& ComposeAPin
				&& ComposeBPin
				&& ComposeReturnPin
				&& Schema->TryCreateConnection(ExistingRotationSourcePin, ComposeAPin)
				&& Schema->TryCreateConnection(CameraOffsetRotationPin, ComposeBPin)
				&& Schema->TryCreateConnection(ComposeReturnPin, RotationPin);
		}
		else
		{
			bLinkedRotationToBone = CameraOffsetRotationPin && Schema->TryCreateConnection(CameraOffsetRotationPin, RotationPin);
		}

		if (!bLinkedRotationToBone || RotationPin->LinkedTo.Num() == 0)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to drive %s rotation from CameraWeaponOffset in %s."), *BoneName.ToString(), *AnimBlueprint->GetPathName());
			return false;
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("Verified %s: %s Rotation pin has %d link(s)."),
			*AnimBlueprint->GetPathName(),
			*BoneName.ToString(),
			RotationPin->LinkedTo.Num());
		return true;
	}

	bool TMConnectCameraOffsetRotationToModifyBoneNode(
		UAnimBlueprint* AnimBlueprint,
		UEdGraph* Graph,
		UAnimGraphNode_ModifyBone* ModifyBoneNode,
		const FName BoneName,
		const EBoneModificationMode RotationMode,
		const EBoneControlSpace RotationSpace,
		const FName CameraWeaponOffsetPropertyName,
		const int32 NodeYOffset,
		const bool bComposeOriginalRotation)
	{
		static const FName RotationPinName(TEXT("Rotation"));

		if (!AnimBlueprint || !Graph || !ModifyBoneNode)
		{
			return false;
		}

		const FRotator OriginalRotation = ModifyBoneNode->Node.Rotation;

		ModifyBoneNode->Modify();
		TMSetOptionalPinVisible(ModifyBoneNode, RotationPinName, true);
		ModifyBoneNode->Node.RotationMode = RotationMode;
		ModifyBoneNode->Node.RotationSpace = RotationSpace;
		ModifyBoneNode->ReconstructNode();

		UEdGraphPin* RotationPin = TMFindPinByName(ModifyBoneNode, RotationPinName, EGPD_Input);
		if (!RotationPin)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to expose Rotation pin on ModifyBone node for %s in %s."), *BoneName.ToString(), *AnimBlueprint->GetPathName());
			return false;
		}

		TMRemovePreviousCameraOffsetRotationPatch(AnimBlueprint, Graph, RotationPin);

		UEdGraphPin* ExistingRotationSourcePin = nullptr;
		if (RotationPin->LinkedTo.Num() > 0)
		{
			ExistingRotationSourcePin = RotationPin->LinkedTo[0];
			RotationPin->BreakLinkTo(ExistingRotationSourcePin);
		}

		UEdGraphPin* CameraOffsetRotationPin = nullptr;
		if (!TMCreateCameraOffsetRotationGetter(
				AnimBlueprint,
				Graph,
				CameraWeaponOffsetPropertyName,
				ModifyBoneNode->NodePosX - 760,
				ModifyBoneNode->NodePosY + NodeYOffset,
				CameraOffsetRotationPin))
		{
			return false;
		}

		const UEdGraphSchema* Schema = Graph->GetSchema();
		bool bLinkedRotationToBone = false;
		if (ExistingRotationSourcePin || (bComposeOriginalRotation && !OriginalRotation.IsNearlyZero()))
		{
			UFunction* ComposeRotatorsFunction =
				UKismetMathLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, ComposeRotators));
			if (!ComposeRotatorsFunction)
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to find UKismetMathLibrary::ComposeRotators."));
				return false;
			}

			FGraphNodeCreator<UK2Node_CallFunction> ComposeRotatorsCreator(*Graph);
			UK2Node_CallFunction* ComposeRotatorsNode = ComposeRotatorsCreator.CreateNode();
			ComposeRotatorsNode->SetFromFunction(ComposeRotatorsFunction);
			ComposeRotatorsNode->NodePosX = ModifyBoneNode->NodePosX - 280;
			ComposeRotatorsNode->NodePosY = ModifyBoneNode->NodePosY + NodeYOffset;
			ComposeRotatorsCreator.Finalize();

			UEdGraphPin* ComposeAPin = TMFindPinByName(ComposeRotatorsNode, TEXT("A"), EGPD_Input);
			UEdGraphPin* ComposeBPin = TMFindPinByName(ComposeRotatorsNode, TEXT("B"), EGPD_Input);
			UEdGraphPin* ComposeReturnPin = TMFindPinByName(ComposeRotatorsNode, UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
			if (!ComposeReturnPin)
			{
				ComposeReturnPin = TMFindFirstDataPin(ComposeRotatorsNode, EGPD_Output);
			}

			bool bHasComposeBase = false;
			if (ExistingRotationSourcePin)
			{
				bHasComposeBase = ComposeAPin && Schema->TryCreateConnection(ExistingRotationSourcePin, ComposeAPin);
			}
			else if (ComposeAPin)
			{
				Schema->TrySetDefaultValue(*ComposeAPin, OriginalRotation.ToString());
				bHasComposeBase = true;
			}

			bLinkedRotationToBone =
				bHasComposeBase
				&& ComposeBPin
				&& ComposeReturnPin
				&& Schema->TryCreateConnection(CameraOffsetRotationPin, ComposeBPin)
				&& Schema->TryCreateConnection(ComposeReturnPin, RotationPin);
		}
		else
		{
			bLinkedRotationToBone = CameraOffsetRotationPin && Schema->TryCreateConnection(CameraOffsetRotationPin, RotationPin);
		}

		if (!bLinkedRotationToBone || RotationPin->LinkedTo.Num() == 0)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to drive %s rotation from CameraWeaponOffset in %s."), *BoneName.ToString(), *AnimBlueprint->GetPathName());
			return false;
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("Verified %s:%s: %s Rotation pin has %d link(s)."),
			*AnimBlueprint->GetPathName(),
			Graph ? *Graph->GetName() : TEXT("None"),
			*BoneName.ToString(),
			RotationPin->LinkedTo.Num());
		return true;
	}

	bool TMConnectCameraOffsetRotationToAllModifyBoneNodes(
		UAnimBlueprint* AnimBlueprint,
		UEdGraph* Graph,
		const FName BoneName,
		const EBoneModificationMode RotationMode,
		const EBoneControlSpace RotationSpace,
		const FName CameraWeaponOffsetPropertyName,
		const int32 NodeYOffset,
		const bool bComposeOriginalRotation)
	{
		if (!AnimBlueprint || !Graph)
		{
			return false;
		}

		TArray<UAnimGraphNode_ModifyBone*> MatchingNodes;
		for (UEdGraphNode* GraphNode : Graph->Nodes)
		{
			UAnimGraphNode_ModifyBone* ModifyBoneNode = Cast<UAnimGraphNode_ModifyBone>(GraphNode);
			if (!ModifyBoneNode || ModifyBoneNode->Node.BoneToModify.BoneName != BoneName)
			{
				continue;
			}

			MatchingNodes.Add(ModifyBoneNode);
		}

		if (MatchingNodes.Num() == 0)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to find any ModifyBone node for bone %s in %s:%s"), *BoneName.ToString(), *AnimBlueprint->GetPathName(), Graph ? *Graph->GetName() : TEXT("None"));
			return false;
		}

		bool bSuccess = true;
		int32 NodeIndex = 0;
		for (UAnimGraphNode_ModifyBone* ModifyBoneNode : MatchingNodes)
		{
			bSuccess &= TMConnectCameraOffsetRotationToModifyBoneNode(
				AnimBlueprint,
				Graph,
				ModifyBoneNode,
				BoneName,
				RotationMode,
				RotationSpace,
				CameraWeaponOffsetPropertyName,
				NodeYOffset + NodeIndex * 120,
				bComposeOriginalRotation);
			++NodeIndex;
		}

		return bSuccess;
	}

	bool TMClearCameraOffsetRotationPatchFromModifyBoneNode(
		UAnimBlueprint* AnimBlueprint,
		UEdGraph* Graph,
		UAnimGraphNode_ModifyBone* ModifyBoneNode,
		const EBoneModificationMode RotationMode)
	{
		if (!AnimBlueprint || !Graph || !ModifyBoneNode)
		{
			return false;
		}

		static const FName RotationPinName(TEXT("Rotation"));

		ModifyBoneNode->Modify();
		TMSetOptionalPinVisible(ModifyBoneNode, RotationPinName, true);
		ModifyBoneNode->ReconstructNode();

		if (UEdGraphPin* RotationPin = TMFindPinByName(ModifyBoneNode, RotationPinName, EGPD_Input))
		{
			TMRemovePreviousCameraOffsetRotationPatch(AnimBlueprint, Graph, RotationPin);
		}

		ModifyBoneNode->Node.RotationMode = RotationMode;
		ModifyBoneNode->ReconstructNode();
		return true;
	}

	bool TMClearCameraOffsetRotationPatchFromModifyBoneNodes(
		UAnimBlueprint* AnimBlueprint,
		UEdGraph* Graph,
		const FName BoneName,
		const EBoneModificationMode RotationMode)
	{
		if (!AnimBlueprint || !Graph)
		{
			return false;
		}

		TArray<UAnimGraphNode_ModifyBone*> MatchingNodes;
		for (UEdGraphNode* GraphNode : Graph->Nodes)
		{
			UAnimGraphNode_ModifyBone* ModifyBoneNode = Cast<UAnimGraphNode_ModifyBone>(GraphNode);
			if (!ModifyBoneNode || ModifyBoneNode->Node.BoneToModify.BoneName != BoneName)
			{
				continue;
			}

			MatchingNodes.Add(ModifyBoneNode);
		}

		bool bTouchedAnyNode = false;
		for (UAnimGraphNode_ModifyBone* ModifyBoneNode : MatchingNodes)
		{
			bTouchedAnyNode |= TMClearCameraOffsetRotationPatchFromModifyBoneNode(
				AnimBlueprint,
				Graph,
				ModifyBoneNode,
				RotationMode);
		}

		return bTouchedAnyNode;
	}

	UAnimGraphNode_ModifyBone* TMFindSourceModifyBoneNodeForDedicatedOffset(UEdGraph* Graph, const FName BoneName)
	{
		if (!Graph)
		{
			return nullptr;
		}

		for (UEdGraphNode* GraphNode : Graph->Nodes)
		{
			UAnimGraphNode_ModifyBone* ModifyBoneNode = Cast<UAnimGraphNode_ModifyBone>(GraphNode);
			if (!ModifyBoneNode
				|| ModifyBoneNode->Node.BoneToModify.BoneName != BoneName
				|| ModifyBoneNode->Node.TranslationMode == BMM_Ignore
				|| ModifyBoneNode->NodeComment.Contains(TEXT("TM CameraWeaponOffset")))
			{
				continue;
			}

			return ModifyBoneNode;
		}

		return nullptr;
	}

	bool TMIsDedicatedCameraOffsetRotationNode(
		const UAnimGraphNode_ModifyBone* ModifyBoneNode,
		const FName BoneName)
	{
		return ModifyBoneNode
			&& ModifyBoneNode->Node.BoneToModify.BoneName == BoneName
			&& ModifyBoneNode->Node.TranslationMode == BMM_Ignore
			&& ModifyBoneNode->Node.RotationMode == BMM_Additive
			&& ModifyBoneNode->Node.ScaleMode == BMM_Ignore;
	}

	bool TMIsDedicatedCameraWeaponOffsetNode(
		const UAnimGraphNode_ModifyBone* ModifyBoneNode,
		const FName BoneName)
	{
		return ModifyBoneNode
			&& ModifyBoneNode->Node.BoneToModify.BoneName == BoneName
			&& ModifyBoneNode->NodeComment.Contains(TEXT("TM CameraWeaponOffset"))
			&& ModifyBoneNode->Node.RotationMode == BMM_Additive
			&& ModifyBoneNode->Node.ScaleMode == BMM_Ignore;
	}

	void TMConfigureCameraWeaponOffsetNode(
		UAnimGraphNode_ModifyBone* ModifyBoneNode,
		const FName BoneName)
	{
		if (!ModifyBoneNode)
		{
			return;
		}

		ModifyBoneNode->Modify();
		ModifyBoneNode->NodeComment = TEXT("TM CameraWeaponOffset");
		ModifyBoneNode->Node.BoneToModify.BoneName = BoneName;
		ModifyBoneNode->Node.BoneToModify.InvalidateCachedBoneIndex();
		ModifyBoneNode->Node.TranslationMode = BMM_Ignore;
		ModifyBoneNode->Node.TranslationSpace = BCS_BoneSpace;
		ModifyBoneNode->Node.RotationMode = BMM_Additive;
		ModifyBoneNode->Node.RotationSpace = BCS_BoneSpace;
		ModifyBoneNode->Node.ScaleMode = BMM_Ignore;
		ModifyBoneNode->ReconstructNode();
	}

	UAnimGraphNode_ModifyBone* TMFindOrCreateDedicatedCameraOffsetRotationNodeAfter(
		UEdGraph* Graph,
		UAnimGraphNode_ModifyBone* SourceModifyBoneNode,
		const FName BoneName,
		const EBoneControlSpace RotationSpace)
	{
		if (!Graph || !SourceModifyBoneNode)
		{
			return nullptr;
		}

		static const FName PosePinName(TEXT("Pose"));
		UEdGraphPin* SourcePosePin = TMFindPinByName(SourceModifyBoneNode, PosePinName, EGPD_Output);
		if (!SourcePosePin)
		{
			return nullptr;
		}

		for (UEdGraphPin* LinkedPin : SourcePosePin->LinkedTo)
		{
			UAnimGraphNode_ModifyBone* ExistingOffsetNode = Cast<UAnimGraphNode_ModifyBone>(
				LinkedPin ? LinkedPin->GetOwningNode() : nullptr);
			if (TMIsDedicatedCameraOffsetRotationNode(ExistingOffsetNode, BoneName))
			{
				ExistingOffsetNode->Modify();
				ExistingOffsetNode->Node.RotationSpace = RotationSpace;
				return ExistingOffsetNode;
			}
		}

		Graph->Modify();

		TArray<UEdGraphPin*> PreviousTargetPins = SourcePosePin->LinkedTo;
		for (UEdGraphPin* PreviousTargetPin : PreviousTargetPins)
		{
			if (PreviousTargetPin)
			{
				SourcePosePin->BreakLinkTo(PreviousTargetPin);
			}
		}

		FGraphNodeCreator<UAnimGraphNode_ModifyBone> OffsetNodeCreator(*Graph);
		UAnimGraphNode_ModifyBone* OffsetNode = OffsetNodeCreator.CreateNode();
		OffsetNode->NodePosX = SourceModifyBoneNode->NodePosX + 260;
		OffsetNode->NodePosY = SourceModifyBoneNode->NodePosY + 40;
		OffsetNode->Node.BoneToModify.BoneName = BoneName;
		OffsetNode->Node.TranslationMode = BMM_Ignore;
		OffsetNode->Node.RotationMode = BMM_Additive;
		OffsetNode->Node.RotationSpace = RotationSpace;
		OffsetNode->Node.ScaleMode = BMM_Ignore;
		OffsetNodeCreator.Finalize();
		OffsetNode->ReconstructNode();

		const UEdGraphSchema* Schema = Graph->GetSchema();
		UEdGraphPin* OffsetInputPin = TMFindPinByName(OffsetNode, TEXT("ComponentPose"), EGPD_Input);
		UEdGraphPin* OffsetOutputPin = TMFindPinByName(OffsetNode, PosePinName, EGPD_Output);
		if (!Schema || !OffsetInputPin || !OffsetOutputPin || !Schema->TryCreateConnection(SourcePosePin, OffsetInputPin))
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to insert CameraWeaponOffset rotation node after %s for bone %s."), *SourceModifyBoneNode->GetName(), *BoneName.ToString());
			return nullptr;
		}

		for (UEdGraphPin* PreviousTargetPin : PreviousTargetPins)
		{
			if (PreviousTargetPin)
			{
				Schema->TryCreateConnection(OffsetOutputPin, PreviousTargetPin);
			}
		}

		return OffsetNode;
	}

	bool TMInsertDedicatedCameraOffsetRotationAfterSourceModifyBone(
		UAnimBlueprint* AnimBlueprint,
		UEdGraph* Graph,
		const FName BoneName,
		const EBoneControlSpace RotationSpace,
		const FName CameraWeaponOffsetPropertyName,
		const int32 NodeYOffset)
	{
		UAnimGraphNode_ModifyBone* SourceModifyBoneNode = TMFindSourceModifyBoneNodeForDedicatedOffset(Graph, BoneName);
		if (!SourceModifyBoneNode)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to find source ModifyBone node for dedicated CameraWeaponOffset rotation on %s in %s:%s."), *BoneName.ToString(), AnimBlueprint ? *AnimBlueprint->GetPathName() : TEXT("None"), Graph ? *Graph->GetName() : TEXT("None"));
			return false;
		}

		UAnimGraphNode_ModifyBone* OffsetNode = TMFindOrCreateDedicatedCameraOffsetRotationNodeAfter(
			Graph,
			SourceModifyBoneNode,
			BoneName,
			RotationSpace);
		if (!OffsetNode)
		{
			return false;
		}

		return TMConnectCameraOffsetRotationToModifyBoneNode(
			AnimBlueprint,
			Graph,
			OffsetNode,
			BoneName,
			BMM_Additive,
			RotationSpace,
			CameraWeaponOffsetPropertyName,
			NodeYOffset,
			false);
	}

	UAnimGraphNode_ModifyBone* TMFindOrCreateDedicatedCameraWeaponOffsetNodeAfter(
		UEdGraph* Graph,
		UAnimGraphNode_ModifyBone* SourceModifyBoneNode,
		const FName BoneName)
	{
		if (!Graph || !SourceModifyBoneNode)
		{
			return nullptr;
		}

		static const FName PosePinName(TEXT("Pose"));
		UEdGraphPin* SourcePosePin = TMFindPinByName(SourceModifyBoneNode, PosePinName, EGPD_Output);
		if (!SourcePosePin)
		{
			return nullptr;
		}

		for (UEdGraphPin* LinkedPin : SourcePosePin->LinkedTo)
		{
			UAnimGraphNode_ModifyBone* ExistingOffsetNode = Cast<UAnimGraphNode_ModifyBone>(
				LinkedPin ? LinkedPin->GetOwningNode() : nullptr);
			if (TMIsDedicatedCameraWeaponOffsetNode(ExistingOffsetNode, BoneName)
				|| TMIsDedicatedCameraOffsetRotationNode(ExistingOffsetNode, BoneName))
			{
				TMConfigureCameraWeaponOffsetNode(ExistingOffsetNode, BoneName);
				return ExistingOffsetNode;
			}
		}

		Graph->Modify();

		TArray<UEdGraphPin*> PreviousTargetPins = SourcePosePin->LinkedTo;
		for (UEdGraphPin* PreviousTargetPin : PreviousTargetPins)
		{
			if (PreviousTargetPin)
			{
				SourcePosePin->BreakLinkTo(PreviousTargetPin);
			}
		}

		FGraphNodeCreator<UAnimGraphNode_ModifyBone> OffsetNodeCreator(*Graph);
		UAnimGraphNode_ModifyBone* OffsetNode = OffsetNodeCreator.CreateNode();
		OffsetNode->NodePosX = SourceModifyBoneNode->NodePosX + 260;
		OffsetNode->NodePosY = SourceModifyBoneNode->NodePosY + 40;
		OffsetNodeCreator.Finalize();
		TMConfigureCameraWeaponOffsetNode(OffsetNode, BoneName);

		const UEdGraphSchema* Schema = Graph->GetSchema();
		UEdGraphPin* OffsetInputPin = TMFindPinByName(OffsetNode, TEXT("ComponentPose"), EGPD_Input);
		UEdGraphPin* OffsetOutputPin = TMFindPinByName(OffsetNode, PosePinName, EGPD_Output);
		if (!Schema || !OffsetInputPin || !OffsetOutputPin || !Schema->TryCreateConnection(SourcePosePin, OffsetInputPin))
		{
			UE_LOG(LogTemp, Error, TEXT("[TMCameraWeaponOffset] Failed to insert CameraWeaponOffset transform node after %s for bone %s."), *SourceModifyBoneNode->GetName(), *BoneName.ToString());
			return nullptr;
		}

		for (UEdGraphPin* PreviousTargetPin : PreviousTargetPins)
		{
			if (PreviousTargetPin)
			{
				Schema->TryCreateConnection(OffsetOutputPin, PreviousTargetPin);
			}
		}

		return OffsetNode;
	}

	bool TMConnectCameraWeaponOffsetToModifyBoneNode(
		UAnimBlueprint* AnimBlueprint,
		UEdGraph* Graph,
		UAnimGraphNode_ModifyBone* ModifyBoneNode,
		const FName BoneName,
		const FName CameraWeaponOffsetPropertyName)
	{
		static const FName TranslationPinName(TEXT("Translation"));
		static const FName RotationPinName(TEXT("Rotation"));

		if (!AnimBlueprint || !Graph || !ModifyBoneNode)
		{
			return false;
		}

		ModifyBoneNode->Modify();
		TMSetOptionalPinVisible(ModifyBoneNode, TranslationPinName, true);
		TMSetOptionalPinVisible(ModifyBoneNode, RotationPinName, true);
		ModifyBoneNode->Node.TranslationMode = BMM_Additive;
		ModifyBoneNode->Node.TranslationSpace = BCS_BoneSpace;
		ModifyBoneNode->Node.RotationMode = BMM_Additive;
		ModifyBoneNode->Node.RotationSpace = BCS_BoneSpace;
		ModifyBoneNode->Node.ScaleMode = BMM_Ignore;
		ModifyBoneNode->ReconstructNode();

		UEdGraphPin* TranslationPin = TMFindPinByName(ModifyBoneNode, TranslationPinName, EGPD_Input);
		UEdGraphPin* RotationPin = TMFindPinByName(ModifyBoneNode, RotationPinName, EGPD_Input);
		if (!RotationPin)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMCameraWeaponOffset] Failed to expose Rotation pin on %s for bone %s."), *ModifyBoneNode->GetName(), *BoneName.ToString());
			return false;
		}

		if (TranslationPin)
		{
			TMRemovePreviousCameraOffsetRotationPatch(AnimBlueprint, Graph, TranslationPin);
			TranslationPin->BreakAllPinLinks();
		}
		TMRemovePreviousCameraOffsetRotationPatch(AnimBlueprint, Graph, RotationPin);
		RotationPin->BreakAllPinLinks();

		TMSetOptionalPinVisible(ModifyBoneNode, TranslationPinName, false);
		TMSetOptionalPinVisible(ModifyBoneNode, RotationPinName, true);
		ModifyBoneNode->Node.TranslationMode = BMM_Ignore;
		ModifyBoneNode->Node.TranslationSpace = BCS_BoneSpace;
		ModifyBoneNode->Node.RotationMode = BMM_Additive;
		ModifyBoneNode->Node.RotationSpace = BCS_BoneSpace;
		ModifyBoneNode->Node.ScaleMode = BMM_Ignore;
		ModifyBoneNode->ReconstructNode();

		RotationPin = TMFindPinByName(ModifyBoneNode, RotationPinName, EGPD_Input);
		if (!RotationPin)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMCameraWeaponOffset] Failed to expose final Rotation pin on %s for bone %s."), *ModifyBoneNode->GetName(), *BoneName.ToString());
			return false;
		}

		UEdGraphPin* OffsetRotationPin = nullptr;
		if (!TMCreateCameraOffsetRotationGetter(
				AnimBlueprint,
				Graph,
				CameraWeaponOffsetPropertyName,
				ModifyBoneNode->NodePosX - 760,
				ModifyBoneNode->NodePosY - 40,
				OffsetRotationPin))
		{
			return false;
		}

		const UEdGraphSchema* Schema = Graph->GetSchema();
		const bool bLinkedRotation =
			Schema && OffsetRotationPin && Schema->TryCreateConnection(OffsetRotationPin, RotationPin);

		if (!bLinkedRotation)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMCameraWeaponOffset] Failed to drive %s rotation from CameraWeaponOffset in %s."), *BoneName.ToString(), *AnimBlueprint->GetPathName());
			return false;
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMCameraWeaponOffset] Verified %s:%s: %s TranslationMode=Ignore RotationLinks=%d."),
			*AnimBlueprint->GetPathName(),
			Graph ? *Graph->GetName() : TEXT("None"),
			*BoneName.ToString(),
			RotationPin->LinkedTo.Num());
		return true;
	}

	bool TMPatchCameraWeaponOffset(UAnimBlueprint* AnimBlueprint, UEdGraph* Graph, const FName BoneName)
	{
		static const FName CameraWeaponOffsetPropertyName(TEXT("CameraWeaponOffset"));
		if (!AnimBlueprint || !Graph)
		{
			return false;
		}

		if (!TMEnsureTransformVariable(AnimBlueprint, CameraWeaponOffsetPropertyName))
		{
			return false;
		}

		UAnimGraphNode_ModifyBone* SourceModifyBoneNode = TMFindSourceModifyBoneNodeForDedicatedOffset(Graph, BoneName);
		if (!SourceModifyBoneNode)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMCameraWeaponOffset] Failed to find source ModifyBone node for %s in %s:%s."), *BoneName.ToString(), *AnimBlueprint->GetPathName(), Graph ? *Graph->GetName() : TEXT("None"));
			return false;
		}

		UAnimGraphNode_ModifyBone* OffsetNode =
			TMFindOrCreateDedicatedCameraWeaponOffsetNodeAfter(Graph, SourceModifyBoneNode, BoneName);
		if (!OffsetNode)
		{
			return false;
		}

		return TMConnectCameraWeaponOffsetToModifyBoneNode(
			AnimBlueprint,
			Graph,
			OffsetNode,
			BoneName,
			CameraWeaponOffsetPropertyName);
	}

	bool TMIsDedicatedLeftHandWeaponOffsetNode(
		const UAnimGraphNode_ModifyBone* ModifyBoneNode,
		const FName BoneName)
	{
		return ModifyBoneNode
			&& ModifyBoneNode->Node.BoneToModify.BoneName == BoneName
			&& ModifyBoneNode->NodeComment.Contains(TEXT("TM LeftHandWeaponOffset"))
			&& ModifyBoneNode->Node.TranslationMode == BMM_Additive
			&& ModifyBoneNode->Node.RotationMode == BMM_Additive
			&& ModifyBoneNode->Node.ScaleMode == BMM_Ignore;
	}

	UAnimGraphNode_ModifyBone* TMFindSourceLeftHandModifyBoneNode(
		UEdGraph* Graph,
		FName& OutBoneName)
	{
		static const FName CandidateBoneNames[] =
		{
			TEXT("VB Hand_L"),
			TEXT("VB LHS_ik_hand_l"),
			TEXT("ik_hand_l"),
			TEXT("hand_l")
		};

		OutBoneName = NAME_None;
		if (!Graph)
		{
			return nullptr;
		}

		for (const FName CandidateBoneName : CandidateBoneNames)
		{
			for (UEdGraphNode* GraphNode : Graph->Nodes)
			{
				UAnimGraphNode_ModifyBone* ModifyBoneNode = Cast<UAnimGraphNode_ModifyBone>(GraphNode);
				if (!ModifyBoneNode
					|| ModifyBoneNode->Node.BoneToModify.BoneName != CandidateBoneName
					|| TMIsDedicatedLeftHandWeaponOffsetNode(ModifyBoneNode, CandidateBoneName))
				{
					continue;
				}

				OutBoneName = CandidateBoneName;
				return ModifyBoneNode;
			}
		}

		return nullptr;
	}

	UAnimGraphNode_ModifyBone* TMFindOrCreateDedicatedLeftHandWeaponOffsetNodeAfter(
		UEdGraph* Graph,
		UAnimGraphNode_ModifyBone* SourceModifyBoneNode,
		const FName BoneName)
	{
		if (!Graph || !SourceModifyBoneNode)
		{
			return nullptr;
		}

		static const FName PosePinName(TEXT("Pose"));
		UEdGraphPin* SourcePosePin = TMFindPinByName(SourceModifyBoneNode, PosePinName, EGPD_Output);
		if (!SourcePosePin)
		{
			return nullptr;
		}

		for (UEdGraphPin* LinkedPin : SourcePosePin->LinkedTo)
		{
			UAnimGraphNode_ModifyBone* ExistingOffsetNode = Cast<UAnimGraphNode_ModifyBone>(
				LinkedPin ? LinkedPin->GetOwningNode() : nullptr);
			if (TMIsDedicatedLeftHandWeaponOffsetNode(ExistingOffsetNode, BoneName))
			{
				ExistingOffsetNode->Modify();
				ExistingOffsetNode->Node.TranslationSpace = BCS_BoneSpace;
				ExistingOffsetNode->Node.RotationSpace = BCS_BoneSpace;
				return ExistingOffsetNode;
			}
		}

		Graph->Modify();

		TArray<UEdGraphPin*> PreviousTargetPins = SourcePosePin->LinkedTo;
		for (UEdGraphPin* PreviousTargetPin : PreviousTargetPins)
		{
			if (PreviousTargetPin)
			{
				SourcePosePin->BreakLinkTo(PreviousTargetPin);
			}
		}

		FGraphNodeCreator<UAnimGraphNode_ModifyBone> OffsetNodeCreator(*Graph);
		UAnimGraphNode_ModifyBone* OffsetNode = OffsetNodeCreator.CreateNode();
		OffsetNode->NodePosX = SourceModifyBoneNode->NodePosX + 260;
		OffsetNode->NodePosY = SourceModifyBoneNode->NodePosY + 120;
		OffsetNode->NodeComment = TEXT("TM LeftHandWeaponOffset");
		OffsetNode->Node.BoneToModify.BoneName = BoneName;
		OffsetNode->Node.BoneToModify.InvalidateCachedBoneIndex();
		OffsetNode->Node.TranslationMode = BMM_Additive;
		OffsetNode->Node.TranslationSpace = BCS_BoneSpace;
		OffsetNode->Node.RotationMode = BMM_Additive;
		OffsetNode->Node.RotationSpace = BCS_BoneSpace;
		OffsetNode->Node.ScaleMode = BMM_Ignore;
		OffsetNodeCreator.Finalize();
		OffsetNode->ReconstructNode();

		const UEdGraphSchema* Schema = Graph->GetSchema();
		UEdGraphPin* OffsetInputPin = TMFindPinByName(OffsetNode, TEXT("ComponentPose"), EGPD_Input);
		UEdGraphPin* OffsetOutputPin = TMFindPinByName(OffsetNode, PosePinName, EGPD_Output);
		if (!Schema || !OffsetInputPin || !OffsetOutputPin || !Schema->TryCreateConnection(SourcePosePin, OffsetInputPin))
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLeftHandOffset] Failed to insert LeftHandWeaponOffset node after %s for bone %s."), *SourceModifyBoneNode->GetName(), *BoneName.ToString());
			return nullptr;
		}

		for (UEdGraphPin* PreviousTargetPin : PreviousTargetPins)
		{
			if (PreviousTargetPin)
			{
				Schema->TryCreateConnection(OffsetOutputPin, PreviousTargetPin);
			}
		}

		return OffsetNode;
	}

	bool TMConnectLeftHandWeaponOffsetToModifyBoneNode(
		UAnimBlueprint* AnimBlueprint,
		UEdGraph* Graph,
		UAnimGraphNode_ModifyBone* ModifyBoneNode,
		const FName BoneName,
		const FName LeftHandWeaponOffsetPropertyName)
	{
		static const FName TranslationPinName(TEXT("Translation"));
		static const FName RotationPinName(TEXT("Rotation"));

		if (!AnimBlueprint || !Graph || !ModifyBoneNode)
		{
			return false;
		}

		ModifyBoneNode->Modify();
		TMSetOptionalPinVisible(ModifyBoneNode, TranslationPinName, true);
		TMSetOptionalPinVisible(ModifyBoneNode, RotationPinName, true);
		ModifyBoneNode->Node.TranslationMode = BMM_Additive;
		ModifyBoneNode->Node.TranslationSpace = BCS_BoneSpace;
		ModifyBoneNode->Node.RotationMode = BMM_Additive;
		ModifyBoneNode->Node.RotationSpace = BCS_BoneSpace;
		ModifyBoneNode->Node.ScaleMode = BMM_Ignore;
		ModifyBoneNode->ReconstructNode();

		UEdGraphPin* TranslationPin = TMFindPinByName(ModifyBoneNode, TranslationPinName, EGPD_Input);
		UEdGraphPin* RotationPin = TMFindPinByName(ModifyBoneNode, RotationPinName, EGPD_Input);
		if (!TranslationPin || !RotationPin)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLeftHandOffset] Failed to expose Translation/Rotation pins on %s for bone %s."), *ModifyBoneNode->GetName(), *BoneName.ToString());
			return false;
		}

		TMRemovePreviousCameraOffsetRotationPatch(AnimBlueprint, Graph, TranslationPin);
		TMRemovePreviousCameraOffsetRotationPatch(AnimBlueprint, Graph, RotationPin);
		TranslationPin->BreakAllPinLinks();
		RotationPin->BreakAllPinLinks();

		UEdGraphPin* OffsetLocationPin = nullptr;
		UEdGraphPin* OffsetRotationPin = nullptr;
		if (!TMCreateTransformBreakGetter(
				AnimBlueprint,
				Graph,
				LeftHandWeaponOffsetPropertyName,
				ModifyBoneNode->NodePosX - 760,
				ModifyBoneNode->NodePosY + 80,
				OffsetLocationPin,
				OffsetRotationPin))
		{
			return false;
		}

		const UEdGraphSchema* Schema = Graph->GetSchema();
		const bool bLinkedTranslation =
			Schema && OffsetLocationPin && Schema->TryCreateConnection(OffsetLocationPin, TranslationPin);
		const bool bLinkedRotation =
			Schema && OffsetRotationPin && Schema->TryCreateConnection(OffsetRotationPin, RotationPin);

		if (!bLinkedTranslation || !bLinkedRotation)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLeftHandOffset] Failed to drive %s from LeftHandWeaponOffset in %s."), *BoneName.ToString(), *AnimBlueprint->GetPathName());
			return false;
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMLeftHandOffset] Verified %s:%s: %s TranslationLinks=%d RotationLinks=%d."),
			*AnimBlueprint->GetPathName(),
			Graph ? *Graph->GetName() : TEXT("None"),
			*BoneName.ToString(),
			TranslationPin->LinkedTo.Num(),
			RotationPin->LinkedTo.Num());
		return true;
	}

	bool TMPatchLeftHandWeaponOffset(UAnimBlueprint* AnimBlueprint, UEdGraph* Graph)
	{
		static const FName LeftHandWeaponOffsetPropertyName(TEXT("LeftHandWeaponOffset"));
		if (!AnimBlueprint || !Graph)
		{
			return false;
		}

		if (!TMEnsureTransformVariable(AnimBlueprint, LeftHandWeaponOffsetPropertyName))
		{
			return false;
		}

		FName BoneName = NAME_None;
		UAnimGraphNode_ModifyBone* SourceModifyBoneNode = TMFindSourceLeftHandModifyBoneNode(Graph, BoneName);
		if (!SourceModifyBoneNode)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLeftHandOffset] Failed to find left hand ModifyBone source in %s:%s."), *AnimBlueprint->GetPathName(), Graph ? *Graph->GetName() : TEXT("None"));
			return false;
		}

		UAnimGraphNode_ModifyBone* OffsetNode =
			TMFindOrCreateDedicatedLeftHandWeaponOffsetNodeAfter(Graph, SourceModifyBoneNode, BoneName);
		if (!OffsetNode)
		{
			return false;
		}

		return TMConnectLeftHandWeaponOffsetToModifyBoneNode(
			AnimBlueprint,
			Graph,
			OffsetNode,
			BoneName,
			LeftHandWeaponOffsetPropertyName);
	}

	bool TMPatchLeftHandWeaponOffsetOnly(const TCHAR* TargetAnimBlueprintPath)
	{
		static const FName FabrikateTransformsGraphName(TEXT("FabrikateTransforms"));
		static const FName LeftHandWeaponOffsetPropertyName(TEXT("LeftHandWeaponOffset"));

		UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, TargetAnimBlueprintPath);
		if (!AnimBlueprint)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLeftHandOffset] Failed to load anim blueprint: %s"), TargetAnimBlueprintPath);
			return false;
		}

		if (!TMEnsureTransformVariable(AnimBlueprint, LeftHandWeaponOffsetPropertyName))
		{
			return false;
		}

		UEdGraph* Graph = TMFindGraphByName(AnimBlueprint, FabrikateTransformsGraphName);
		if (!Graph)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLeftHandOffset] Failed to find graph %s in %s"), *FabrikateTransformsGraphName.ToString(), TargetAnimBlueprintPath);
			return false;
		}

		AnimBlueprint->Modify();
		Graph->Modify();

		TMDumpRelevantAnimGraphLinks(AnimBlueprint, TEXT("BeforeLeftHandOffsetPatch"));

		if (!TMPatchLeftHandWeaponOffset(AnimBlueprint, Graph))
		{
			return false;
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
		FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);

		TMDumpRelevantAnimGraphLinks(AnimBlueprint, TEXT("AfterLeftHandOffsetPatch"));

		UPackage* Package = AnimBlueprint->GetOutermost();
		const FString PackageFilename = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;

		if (!UPackage::SavePackage(Package, nullptr, *PackageFilename, SaveArgs))
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLeftHandOffset] Failed to save package: %s"), *PackageFilename);
			return false;
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMLeftHandOffset] Patched %s: LeftHandWeaponOffset now drives a dedicated additive left hand node."),
			TargetAnimBlueprintPath);
		return true;
	}

	bool TMVerifyDedicatedCameraWeaponOffsetRotationLink(
		const UAnimBlueprint* AnimBlueprint,
		UEdGraph* Graph,
		const FName BoneName)
	{
		static const FName PosePinName(TEXT("Pose"));
		static const FName RotationPinName(TEXT("Rotation"));

		UAnimGraphNode_ModifyBone* SourceModifyBoneNode = TMFindSourceModifyBoneNodeForDedicatedOffset(Graph, BoneName);
		UEdGraphPin* SourcePosePin = SourceModifyBoneNode ? TMFindPinByName(SourceModifyBoneNode, PosePinName, EGPD_Output) : nullptr;
		UAnimGraphNode_ModifyBone* OffsetNode = nullptr;
		if (SourcePosePin)
		{
			for (UEdGraphPin* LinkedPin : SourcePosePin->LinkedTo)
			{
				UAnimGraphNode_ModifyBone* LinkedModifyBoneNode = Cast<UAnimGraphNode_ModifyBone>(
					LinkedPin ? LinkedPin->GetOwningNode() : nullptr);
				if (TMIsDedicatedCameraWeaponOffsetNode(LinkedModifyBoneNode, BoneName))
				{
					OffsetNode = LinkedModifyBoneNode;
					break;
				}
			}
		}

		UEdGraphPin* RotationPin = OffsetNode ? TMFindPinByName(OffsetNode, RotationPinName, EGPD_Input) : nullptr;
		const int32 RotationLinkCount = RotationPin ? RotationPin->LinkedTo.Num() : 0;
		const bool bTranslationIgnored = OffsetNode && OffsetNode->Node.TranslationMode == BMM_Ignore;
		UE_LOG(
			LogTemp,
			Display,
			TEXT("Post-compile verify %s:%s dedicated %s CameraWeaponOffset TranslationIgnored=%d Rotation=%d link(s)."),
			AnimBlueprint ? *AnimBlueprint->GetPathName() : TEXT("None"),
			Graph ? *Graph->GetName() : TEXT("None"),
			*BoneName.ToString(),
			bTranslationIgnored ? 1 : 0,
			RotationLinkCount);
		return bTranslationIgnored && RotationLinkCount > 0;
	}

	bool TMVerifyModifyBoneRotationLink(
		const UAnimBlueprint* AnimBlueprint,
		UEdGraph* Graph,
		const FName BoneName)
	{
		static const FName RotationPinName(TEXT("Rotation"));

		UAnimGraphNode_ModifyBone* ModifyBoneNode = TMFindModifyBoneNode(Graph, BoneName);
		UEdGraphPin* RotationPin = ModifyBoneNode ? TMFindPinByName(ModifyBoneNode, RotationPinName, EGPD_Input) : nullptr;
		const int32 LinkCount = RotationPin ? RotationPin->LinkedTo.Num() : 0;
		UE_LOG(
			LogTemp,
			Display,
			TEXT("Post-compile verify %s: %s Rotation pin has %d link(s)."),
			AnimBlueprint ? *AnimBlueprint->GetPathName() : TEXT("None"),
			*BoneName.ToString(),
			LinkCount);
		return LinkCount > 0;
	}

	FString TMNormalizePinLabel(FString Label)
	{
		Label.ToLowerInline();
		Label.ReplaceInline(TEXT(" "), TEXT(""));
		Label.ReplaceInline(TEXT("_"), TEXT(""));
		Label.ReplaceInline(TEXT("-"), TEXT(""));
		return Label;
	}

	FString TMDescribePin(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return TEXT("<null>");
		}

		const UEdGraphNode* OwningNode = Pin->GetOwningNode();
		return FString::Printf(
			TEXT("%s.%s"),
			OwningNode ? *OwningNode->GetName() : TEXT("<null-node>"),
			*Pin->PinName.ToString());
	}

	bool TMPinMatchesDataTableField(const UEdGraphPin* Pin, const TCHAR* FieldName)
	{
		if (!Pin)
		{
			return false;
		}

		const FString FieldToken = TMNormalizePinLabel(FieldName);
		const FString PinNameToken = TMNormalizePinLabel(Pin->PinName.ToString());
		const FString FriendlyToken = TMNormalizePinLabel(Pin->PinFriendlyName.ToString());
		return PinNameToken.Contains(FieldToken) || FriendlyToken.Contains(FieldToken);
	}

	void TMCollectPinsMatchingDataTableField(UEdGraphPin* Pin, const TCHAR* FieldName, TArray<UEdGraphPin*>& OutPins)
	{
		if (!Pin)
		{
			return;
		}

		if (Pin->Direction == EGPD_Output
			&& Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec
			&& TMPinMatchesDataTableField(Pin, FieldName))
		{
			OutPins.Add(Pin);
		}

		for (UEdGraphPin* SubPin : Pin->SubPins)
		{
			TMCollectPinsMatchingDataTableField(SubPin, FieldName, OutPins);
		}
	}

	UEdGraphPin* TMFindDataTableFieldOutputPin(UEdGraphNode* Node, const TCHAR* FieldName)
	{
		if (!Node)
		{
			return nullptr;
		}

		TArray<UEdGraphPin*> MatchingPins;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			TMCollectPinsMatchingDataTableField(Pin, FieldName, MatchingPins);
		}

		for (UEdGraphPin* Pin : MatchingPins)
		{
			if (Pin && Pin->LinkedTo.Num() == 0)
			{
				return Pin;
			}
		}

		return MatchingPins.Num() > 0 ? MatchingPins[0] : nullptr;
	}

	void TMDumpDataTableRowNodePins(const UK2Node_GetDataTableRow* Node, const TCHAR* Label)
	{
		if (!Node)
		{
			return;
		}

		UE_LOG(LogTemp, Display, TEXT("[TMMagazineFix] %s Node=%s RowStruct=%s"), Label, *Node->GetName(), *GetNameSafe(Node->GetDataTableRowStructType()));
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin)
			{
				continue;
			}

			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMMagazineFix]   Pin=%s Friendly=%s Dir=%s Category=%s SubPins=%d Links=%d"),
				*Pin->PinName.ToString(),
				*Pin->PinFriendlyName.ToString(),
				Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"),
				*Pin->PinType.PinCategory.ToString(),
				Pin->SubPins.Num(),
				Pin->LinkedTo.Num());

			for (const UEdGraphPin* SubPin : Pin->SubPins)
			{
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMMagazineFix]     SubPin=%s Friendly=%s Dir=%s Category=%s Links=%d"),
					*SubPin->PinName.ToString(),
					*SubPin->PinFriendlyName.ToString(),
					SubPin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"),
					*SubPin->PinType.PinCategory.ToString(),
					SubPin->LinkedTo.Num());
			}
		}
	}

	bool TMCollectAndBreakFieldLinks(
		UK2Node_GetDataTableRow* Node,
		const TCHAR* FieldName,
		TArray<UEdGraphPin*>& OutLinkedPins)
	{
		TArray<UEdGraphPin*> FieldPins;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			TMCollectPinsMatchingDataTableField(Pin, FieldName, FieldPins);
		}

		for (UEdGraphPin* FieldPin : FieldPins)
		{
			if (!FieldPin)
			{
				continue;
			}

			for (UEdGraphPin* LinkedPin : FieldPin->LinkedTo)
			{
				if (LinkedPin)
				{
					OutLinkedPins.AddUnique(LinkedPin);
					UE_LOG(LogTemp, Display, TEXT("[TMMagazineFix] Preserved %s link to %s"), FieldName, *TMDescribePin(LinkedPin));
				}
			}
			FieldPin->BreakAllPinLinks(false);
		}

		return OutLinkedPins.Num() > 0;
	}

	bool TMReconnectFieldLinks(
		const UEdGraphSchema* Schema,
		UEdGraphPin* SourcePin,
		const TCHAR* FieldName,
		const TArray<UEdGraphPin*>& LinkedPins)
	{
		if (!Schema || !SourcePin)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMagazineFix] Missing source pin for %s."), FieldName);
			return false;
		}

		bool bSuccess = true;
		for (UEdGraphPin* LinkedPin : LinkedPins)
		{
			if (!LinkedPin)
			{
				continue;
			}

			if (LinkedPin->LinkedTo.Contains(SourcePin))
			{
				continue;
			}

			const bool bConnected = Schema->TryCreateConnection(SourcePin, LinkedPin);
			if (bConnected)
			{
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMMagazineFix] Reconnect %s: %s -> %s : OK"),
					FieldName,
					*TMDescribePin(SourcePin),
					*TMDescribePin(LinkedPin));
			}
			else
			{
				UE_LOG(
					LogTemp,
					Error,
					TEXT("[TMMagazineFix] Reconnect %s: %s -> %s : FAILED"),
					FieldName,
					*TMDescribePin(SourcePin),
					*TMDescribePin(LinkedPin));
			}
			bSuccess &= bConnected;
		}

		return bSuccess;
	}

	UEdGraphPin* TMFindFirstInputDataPin(UEdGraphNode* Node)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin
				&& Pin->Direction == EGPD_Input
				&& Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				return Pin;
			}
		}

		return nullptr;
	}

	void TMCollectBreakStructInputPinsByStructName(UEdGraph* Graph, const TCHAR* StructName, TArray<UEdGraphPin*>& OutPins)
	{
		if (!Graph)
		{
			return;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_BreakStruct* BreakStructNode = Cast<UK2Node_BreakStruct>(Node);
			if (!BreakStructNode || !BreakStructNode->StructType)
			{
				continue;
			}

			if (!BreakStructNode->StructType->GetName().Contains(StructName))
			{
				continue;
			}

			if (UEdGraphPin* InputPin = TMFindFirstInputDataPin(BreakStructNode))
			{
				OutPins.AddUnique(InputPin);
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMMagazineFix] Fallback target %s -> %s"),
					StructName,
					*TMDescribePin(InputPin));
			}
		}
	}

	UK2Node_BreakStruct* TMFindOrCreateWeaponDataBreakNode(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		UK2Node_GetDataTableRow* RowNode,
		UScriptStruct* RowStruct)
	{
		if (!Blueprint || !Graph || !RowNode || !RowStruct)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_BreakStruct* BreakStructNode = Cast<UK2Node_BreakStruct>(Node);
			if (BreakStructNode && BreakStructNode->StructType == RowStruct)
			{
				UE_LOG(LogTemp, Display, TEXT("[TMMagazineFix] Reusing existing Break ST_WeaponData node=%s"), *BreakStructNode->GetName());
				return BreakStructNode;
			}
		}

		FGraphNodeCreator<UK2Node_BreakStruct> NodeCreator(*Graph);
		UK2Node_BreakStruct* BreakStructNode = NodeCreator.CreateNode();
		BreakStructNode->StructType = RowStruct;
		BreakStructNode->NodePosX = RowNode->NodePosX + 280;
		BreakStructNode->NodePosY = RowNode->NodePosY + 40;
		NodeCreator.Finalize();

		BreakStructNode->Modify();
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		UE_LOG(LogTemp, Display, TEXT("[TMMagazineFix] Created Break ST_WeaponData node=%s"), *BreakStructNode->GetName());
		return BreakStructNode;
	}

	bool TMFixMagazineMasterDataTableNode(const bool bSaveAsset)
	{
		static const TCHAR* MagazineBlueprintPath =
			TEXT("/Game/MP_System_V3/Game/Blueprints/Core/BP_Magazine_Master.BP_Magazine_Master");
		static const FName DataTableGraphName(TEXT("DataTable"));

		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, MagazineBlueprintPath);
		if (!Blueprint)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMagazineFix] Failed to load %s."), MagazineBlueprintPath);
			return false;
		}

		UEdGraph* Graph = TMFindGraphByName(Blueprint, DataTableGraphName);
		if (!Graph)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMagazineFix] Failed to find %s graph."), *DataTableGraphName.ToString());
			return false;
		}

		UK2Node_GetDataTableRow* RowNode = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_GetDataTableRow* CandidateNode = Cast<UK2Node_GetDataTableRow>(Node);
			if (!CandidateNode)
			{
				continue;
			}

			UEdGraphPin* DataTablePin = CandidateNode->GetDataTablePin();
			const UObject* DataTableObject = DataTablePin ? DataTablePin->DefaultObject : nullptr;
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMMagazineFix] Found GetDataTableRow node=%s DataTable=%s"),
				*CandidateNode->GetName(),
				*GetNameSafe(DataTableObject));

			if (!RowNode || GetNameSafe(DataTableObject).Contains(TEXT("DT_Weapons")))
			{
				RowNode = CandidateNode;
			}
		}

		if (!RowNode)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMagazineFix] No UK2Node_GetDataTableRow found in BP_Magazine_Master.DataTable."));
			return false;
		}

		UScriptStruct* RowStruct = RowNode->GetDataTableRowStructType();
		UE_LOG(LogTemp, Display, TEXT("[TMMagazineFix] RowStruct=%s"), *GetNameSafe(RowStruct));
		if (RowStruct)
		{
			for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
			{
				UE_LOG(LogTemp, Display, TEXT("[TMMagazineFix]   RowField=%s Type=%s"), *It->GetName(), *It->GetClass()->GetName());
			}
		}

		Blueprint->Modify();
		Graph->Modify();
		RowNode->Modify();

		TMDumpDataTableRowNodePins(RowNode, TEXT("Before"));

		TArray<UEdGraphPin*> CoreDataLinkedPins;
		TArray<UEdGraphPin*> ParametersLinkedPins;
		TMCollectAndBreakFieldLinks(RowNode, TEXT("CoreData"), CoreDataLinkedPins);
		TMCollectAndBreakFieldLinks(RowNode, TEXT("Parameters"), ParametersLinkedPins);

		if (CoreDataLinkedPins.Num() == 0)
		{
			TMCollectBreakStructInputPinsByStructName(Graph, TEXT("ST_WeaponCore"), CoreDataLinkedPins);
		}
		if (ParametersLinkedPins.Num() == 0)
		{
			TMCollectBreakStructInputPinsByStructName(Graph, TEXT("ST_Weapon_Params"), ParametersLinkedPins);
		}

		RowNode->ReconstructNode();

		const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
		UEdGraphPin* CoreDataSourcePin = TMFindDataTableFieldOutputPin(RowNode, TEXT("CoreData"));
		UEdGraphPin* ParametersSourcePin = TMFindDataTableFieldOutputPin(RowNode, TEXT("Parameters"));

		if ((!CoreDataSourcePin || !ParametersSourcePin) && Schema)
		{
			if (UEdGraphPin* ResultPin = RowNode->GetResultPin())
			{
				if (ResultPin->SubPins.Num() == 0)
				{
					Schema->SplitPin(ResultPin, true);
				}
			}

			CoreDataSourcePin = TMFindDataTableFieldOutputPin(RowNode, TEXT("CoreData"));
			ParametersSourcePin = TMFindDataTableFieldOutputPin(RowNode, TEXT("Parameters"));
		}

		TMDumpDataTableRowNodePins(RowNode, TEXT("AfterReconstruct"));

		if ((!CoreDataSourcePin || !ParametersSourcePin) && Schema)
		{
			UK2Node_BreakStruct* WeaponDataBreakNode = TMFindOrCreateWeaponDataBreakNode(Blueprint, Graph, RowNode, RowStruct);
			if (WeaponDataBreakNode)
			{
				WeaponDataBreakNode->Modify();
				UEdGraphPin* ResultPin = RowNode->GetResultPin();
				UEdGraphPin* BreakInputPin = TMFindFirstInputDataPin(WeaponDataBreakNode);
				if (ResultPin && BreakInputPin && !BreakInputPin->LinkedTo.Contains(ResultPin))
				{
					const bool bConnected = Schema->TryCreateConnection(ResultPin, BreakInputPin);
					if (bConnected)
					{
						UE_LOG(LogTemp, Display, TEXT("[TMMagazineFix] Connect OutRow -> Break ST_WeaponData: OK"));
					}
					else
					{
						UE_LOG(LogTemp, Error, TEXT("[TMMagazineFix] Connect OutRow -> Break ST_WeaponData: FAILED"));
					}
				}

				CoreDataSourcePin = TMFindDataTableFieldOutputPin(WeaponDataBreakNode, TEXT("CoreData"));
				ParametersSourcePin = TMFindDataTableFieldOutputPin(WeaponDataBreakNode, TEXT("Parameters"));
				TMDumpNodePinsVerbose(WeaponDataBreakNode, TEXT("[TMMagazineFix] BreakWeaponData"));
			}
		}

		bool bSuccess = true;
		bSuccess &= TMReconnectFieldLinks(Schema, CoreDataSourcePin, TEXT("CoreData"), CoreDataLinkedPins);
		bSuccess &= TMReconnectFieldLinks(Schema, ParametersSourcePin, TEXT("Parameters"), ParametersLinkedPins);

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		UE_LOG(LogTemp, Display, TEXT("[TMMagazineFix] Blueprint status after compile: %d"), static_cast<int32>(Blueprint->Status));
		bSuccess &= Blueprint->Status != BS_Error;

		if (!bSuccess)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMagazineFix] Fix did not compile cleanly; not saving package."));
			return false;
		}

		if (bSaveAsset)
		{
			UPackage* Package = Blueprint->GetOutermost();
			const FString PackageFilename = FPackageName::LongPackageNameToFilename(
				Package->GetName(),
				FPackageName::GetAssetPackageExtension());

			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;

			if (!UPackage::SavePackage(Package, nullptr, *PackageFilename, SaveArgs))
			{
				UE_LOG(LogTemp, Error, TEXT("[TMMagazineFix] Failed to save package: %s"), *PackageFilename);
				return false;
			}
		}

		return bSuccess;
	}

	bool TMRefreshCompileAndSaveBlueprint(const TCHAR* BlueprintPath, const TCHAR* LogPrefix)
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, BlueprintPath);
		if (!Blueprint)
		{
			UE_LOG(LogTemp, Error, TEXT("[%s] Failed to load blueprint: %s"), LogPrefix, BlueprintPath);
			return false;
		}

		Blueprint->Modify();
		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			if (Graph)
			{
				Graph->Modify();
			}
		}
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph)
			{
				Graph->Modify();
			}
		}
		for (UEdGraph* Graph : Blueprint->MacroGraphs)
		{
			if (Graph)
			{
				Graph->Modify();
			}
		}

		FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[%s] Blueprint status after refresh/compile: %d"),
			LogPrefix,
			static_cast<int32>(Blueprint->Status));

		if (Blueprint->Status == BS_Error)
		{
			UE_LOG(LogTemp, Error, TEXT("[%s] Blueprint did not compile cleanly: %s"), LogPrefix, BlueprintPath);
			return false;
		}

		UPackage* Package = Blueprint->GetOutermost();
		const FString PackageFilename = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;

		if (!UPackage::SavePackage(Package, nullptr, *PackageFilename, SaveArgs))
		{
			UE_LOG(LogTemp, Error, TEXT("[%s] Failed to save package: %s"), LogPrefix, *PackageFilename);
			return false;
		}

		UE_LOG(LogTemp, Display, TEXT("[%s] Refreshed, compiled, and saved %s."), LogPrefix, *PackageFilename);
		return true;
	}

	bool TMFixBrokenDataTableRowStructBlueprints()
	{
		bool bSuccess = true;
		bSuccess &= TMFixMagazineMasterDataTableNode(true);
		bSuccess &= TMRefreshCompileAndSaveBlueprint(
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Loadout.W_Loadout"),
			TEXT("TMLoadoutFix"));
		return bSuccess;
	}

	FString TMGetNamePinDefaultText(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return FString();
		}

		FString DefaultText = Pin->DefaultValue;
		if (DefaultText.IsEmpty())
		{
			DefaultText = Pin->AutogeneratedDefaultValue;
		}

		DefaultText.TrimStartAndEndInline();
		DefaultText.RemoveFromStart(TEXT("\""));
		DefaultText.RemoveFromEnd(TEXT("\""));
		DefaultText.RemoveFromStart(TEXT("'"));
		DefaultText.RemoveFromEnd(TEXT("'"));
		return DefaultText;
	}

	bool TMNamePinMatches(const UEdGraphPin* Pin, const FName ExpectedName)
	{
		const FString DefaultText = TMGetNamePinDefaultText(Pin);
		const FString ExpectedText = ExpectedName.ToString();
		return DefaultText.Equals(ExpectedText, ESearchCase::IgnoreCase)
			|| DefaultText.Contains(ExpectedText, ESearchCase::IgnoreCase);
	}

	const UEdGraphPin* TMFindPinByNameConst(
		const UEdGraphNode* Node,
		const FName PinName,
		const EEdGraphPinDirection Direction)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->PinName == PinName && Pin->Direction == Direction)
			{
				return Pin;
			}
		}

		return nullptr;
	}

	bool TMCallFunctionIsGetBoneOffsetForLogicalBone(const UK2Node_CallFunction* CallFunctionNode, const FName LogicalBoneName)
	{
		if (!CallFunctionNode
			|| CallFunctionNode->FunctionReference.GetMemberName() != TEXT("GetBoneOffset"))
		{
			return false;
		}

		const UEdGraphPin* BoneNamePin = TMFindPinByNameConst(CallFunctionNode, TEXT("BoneName"), EGPD_Input);
		return TMNamePinMatches(BoneNamePin, LogicalBoneName);
	}

	bool TMNodeOrUpstreamCallsGetBoneOffsetForLogicalBone(
		const UEdGraphNode* Node,
		const FName LogicalBoneName,
		TSet<const UEdGraphNode*>& VisitedNodes)
	{
		if (!Node || VisitedNodes.Contains(Node))
		{
			return false;
		}

		VisitedNodes.Add(Node);
		if (const UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node))
		{
			if (TMCallFunctionIsGetBoneOffsetForLogicalBone(CallFunctionNode, LogicalBoneName))
			{
				return true;
			}
		}

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input)
			{
				continue;
			}

			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				const UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
				if (TMNodeOrUpstreamCallsGetBoneOffsetForLogicalBone(LinkedNode, LogicalBoneName, VisitedNodes))
				{
					return true;
				}
			}
		}

		return false;
	}

	bool TMModifyBoneNodeUsesLogicalOffset(const UAnimGraphNode_ModifyBone* ModifyBoneNode, const FName LogicalBoneName)
	{
		TSet<const UEdGraphNode*> VisitedNodes;
		return TMNodeOrUpstreamCallsGetBoneOffsetForLogicalBone(ModifyBoneNode, LogicalBoneName, VisitedNodes);
	}

	void TMDumpGeneratedModifyBoneNodes(const UAnimBlueprint* AnimBlueprint, const TCHAR* Label)
	{
		if (!AnimBlueprint || !AnimBlueprint->GeneratedClass)
		{
			return;
		}

		const UObject* DefaultObject = AnimBlueprint->GeneratedClass->GetDefaultObject();
		if (!DefaultObject)
		{
			return;
		}

		int32 ModifyBoneCount = 0;
		for (TFieldIterator<FStructProperty> It(AnimBlueprint->GeneratedClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			const FStructProperty* StructProperty = *It;
			if (!StructProperty || StructProperty->Struct != FAnimNode_ModifyBone::StaticStruct())
			{
				continue;
			}

			const FAnimNode_ModifyBone* ModifyBoneNode = StructProperty->ContainerPtrToValuePtr<FAnimNode_ModifyBone>(DefaultObject);
			if (!ModifyBoneNode)
			{
				continue;
			}

			++ModifyBoneCount;
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMFakeGun] %s GeneratedClassProperty=%s Type=FAnimNode_ModifyBone Bone=%s TM=%d RM=%d TS=%d RS=%d Loc=%s Rot=%s"),
				Label,
				*StructProperty->GetName(),
				*ModifyBoneNode->BoneToModify.BoneName.ToString(),
				static_cast<int32>(ModifyBoneNode->TranslationMode),
				static_cast<int32>(ModifyBoneNode->RotationMode),
				static_cast<int32>(ModifyBoneNode->TranslationSpace),
				static_cast<int32>(ModifyBoneNode->RotationSpace),
				*ModifyBoneNode->Translation.ToString(),
				*ModifyBoneNode->Rotation.ToString());
		}

		UE_LOG(LogTemp, Display, TEXT("[TMFakeGun] %s GeneratedClass ModifyBone count=%d"), Label, ModifyBoneCount);
	}

	int32 TMPatchGeneratedModifyBoneTarget(UAnimBlueprint* AnimBlueprint, const FName OldTargetBone, const FName NewTargetBone)
	{
		if (!AnimBlueprint || !AnimBlueprint->GeneratedClass)
		{
			return 0;
		}

		UObject* DefaultObject = AnimBlueprint->GeneratedClass->GetDefaultObject();
		if (!DefaultObject)
		{
			return 0;
		}

		TArray<FStructProperty*> OldTargetProperties;
		int32 NewTargetPropertyCount = 0;
		for (TFieldIterator<FStructProperty> It(AnimBlueprint->GeneratedClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			FStructProperty* StructProperty = *It;
			if (!StructProperty || StructProperty->Struct != FAnimNode_ModifyBone::StaticStruct())
			{
				continue;
			}

			FAnimNode_ModifyBone* ModifyBoneNode = StructProperty->ContainerPtrToValuePtr<FAnimNode_ModifyBone>(DefaultObject);
			if (!ModifyBoneNode)
			{
				continue;
			}

			if (ModifyBoneNode->BoneToModify.BoneName == OldTargetBone)
			{
				OldTargetProperties.Add(StructProperty);
			}
			else if (ModifyBoneNode->BoneToModify.BoneName == NewTargetBone)
			{
				++NewTargetPropertyCount;
			}
		}

		if (OldTargetProperties.Num() == 0)
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMFakeGun] GeneratedClass has no %s ModifyBone properties; %s properties=%d."),
				*OldTargetBone.ToString(),
				*NewTargetBone.ToString(),
				NewTargetPropertyCount);
			return 0;
		}

		if (OldTargetProperties.Num() > 1)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMFakeGun] Refusing to patch generated class: found %d %s ModifyBone properties."),
				OldTargetProperties.Num(),
				*OldTargetBone.ToString());
			return -1;
		}

		DefaultObject->Modify();
		FStructProperty* StructProperty = OldTargetProperties[0];
		FAnimNode_ModifyBone* ModifyBoneNode = StructProperty->ContainerPtrToValuePtr<FAnimNode_ModifyBone>(DefaultObject);
		ModifyBoneNode->BoneToModify.BoneName = NewTargetBone;
		ModifyBoneNode->BoneToModify.InvalidateCachedBoneIndex();
		DefaultObject->MarkPackageDirty();

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMFakeGun] Patched generated ModifyBone property %s target %s -> %s."),
			*StructProperty->GetName(),
			*OldTargetBone.ToString(),
			*NewTargetBone.ToString());
		return 1;
	}

	void TMDumpFakeGunAnimBlueprint(const UAnimBlueprint* AnimBlueprint, const TCHAR* Label)
	{
		if (!AnimBlueprint)
		{
			return;
		}

		UE_LOG(LogTemp, Display, TEXT("[TMFakeGun] %s Blueprint=%s"), Label, *AnimBlueprint->GetPathName());

		if (const UClass* GeneratedClass = AnimBlueprint->GeneratedClass)
		{
			const UFakeGunAnimInstance* DefaultAnimInstance = Cast<UFakeGunAnimInstance>(GeneratedClass->GetDefaultObject());
			if (DefaultAnimInstance)
			{
				static const FName MagazineLogicalBone(TEXT("Magazine"));
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMFakeGun] %s CDO BoneReferences[%s]=%s"),
					Label,
					*MagazineLogicalBone.ToString(),
					*DefaultAnimInstance->EditorGetBoneReferenceTarget(MagazineLogicalBone).ToString());
			}
		}

		TMDumpGeneratedModifyBoneNodes(AnimBlueprint, Label);

		TArray<UEdGraph*> Graphs;
		AnimBlueprint->GetAllGraphs(Graphs);
		UE_LOG(LogTemp, Display, TEXT("[TMFakeGun] %s Source graph count=%d"), Label, Graphs.Num());
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}

			UE_LOG(LogTemp, Display, TEXT("[TMFakeGun] %s SourceGraph=%s NodeCount=%d"), Label, *Graph->GetName(), Graph->Nodes.Num());

			for (UEdGraphNode* GraphNode : Graph->Nodes)
			{
				if (const UAnimGraphNode_ModifyBone* ModifyBoneNode = Cast<UAnimGraphNode_ModifyBone>(GraphNode))
				{
					const FName BoneName = ModifyBoneNode->Node.BoneToModify.BoneName;
					const bool bUsesMagazineOffset = TMModifyBoneNodeUsesLogicalOffset(ModifyBoneNode, TEXT("Magazine"));
					UE_LOG(
						LogTemp,
						Display,
						TEXT("[TMFakeGun] %s Graph=%s Node=%s Type=ModifyBone Bone=%s UsesMagazineOffset=%d TM=%d RM=%d TS=%d RS=%d Loc=%s Rot=%s"),
						Label,
						*Graph->GetName(),
						*ModifyBoneNode->GetName(),
						*BoneName.ToString(),
						bUsesMagazineOffset ? 1 : 0,
						static_cast<int32>(ModifyBoneNode->Node.TranslationMode),
						static_cast<int32>(ModifyBoneNode->Node.RotationMode),
						static_cast<int32>(ModifyBoneNode->Node.TranslationSpace),
						static_cast<int32>(ModifyBoneNode->Node.RotationSpace),
						*ModifyBoneNode->Node.Translation.ToString(),
						*ModifyBoneNode->Node.Rotation.ToString());
					TMDumpNodePins(ModifyBoneNode);
				}
				else if (const UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(GraphNode))
				{
					if (CallFunctionNode->FunctionReference.GetMemberName() == TEXT("GetBoneOffset"))
					{
						const UEdGraphPin* BoneNamePin = TMFindPinByNameConst(CallFunctionNode, TEXT("BoneName"), EGPD_Input);
						UE_LOG(
							LogTemp,
							Display,
							TEXT("[TMFakeGun] %s Graph=%s Node=%s Type=GetBoneOffset BoneNameDefault=%s Links=%s"),
							Label,
							*Graph->GetName(),
							*CallFunctionNode->GetName(),
							*TMGetNamePinDefaultText(BoneNamePin),
							*TMDescribePinLinks(BoneNamePin));
					}
				}
			}
		}
	}

	UEdGraphPin* TMFindPinBySuffix(UEdGraphNode* Node, const TCHAR* Suffix, const EEdGraphPinDirection Direction)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin
				&& Pin->Direction == Direction
				&& Pin->PinName.ToString().EndsWith(Suffix, ESearchCase::IgnoreCase))
			{
				return Pin;
			}
		}

		return nullptr;
	}

	UAnimGraphNode_Root* TMFindAnimGraphRootNode(UEdGraph* Graph)
	{
		if (!Graph)
		{
			return nullptr;
		}

		for (UEdGraphNode* GraphNode : Graph->Nodes)
		{
			if (UAnimGraphNode_Root* RootNode = Cast<UAnimGraphNode_Root>(GraphNode))
			{
				return RootNode;
			}
		}

		return nullptr;
	}

	template <typename NodeType>
	NodeType* TMCreateAnimNode(UEdGraph* Graph, const int32 NodePosX, const int32 NodePosY)
	{
		FGraphNodeCreator<NodeType> NodeCreator(*Graph);
		NodeType* Node = NodeCreator.CreateNode();
		Node->NodePosX = NodePosX;
		Node->NodePosY = NodePosY;
		NodeCreator.Finalize();
		Node->ReconstructNode();
		return Node;
	}

	bool TMCreateAndConnectFakeGunOffsetGetter(
		UAnimBlueprint* AnimBlueprint,
		UEdGraph* Graph,
		UAnimGraphNode_ModifyBone* ModifyBoneNode,
		const FName LogicalBoneName,
		const int32 NodePosX,
		const int32 NodePosY)
	{
		if (!AnimBlueprint || !Graph || !ModifyBoneNode)
		{
			return false;
		}

		const UEdGraphSchema* Schema = Graph->GetSchema();
		UFunction* GetBoneOffsetFunction = UFakeGunAnimInstance::StaticClass()->FindFunctionByName(TEXT("GetBoneOffset"));
		if (!Schema || !GetBoneOffsetFunction)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Failed to find GetBoneOffset function or graph schema."));
			return false;
		}

		FGraphNodeCreator<UK2Node_CallFunction> GetOffsetCreator(*Graph);
		UK2Node_CallFunction* GetOffsetNode = GetOffsetCreator.CreateNode();
		GetOffsetNode->SetFromFunction(GetBoneOffsetFunction);
		GetOffsetNode->NodePosX = NodePosX;
		GetOffsetNode->NodePosY = NodePosY;
		GetOffsetCreator.Finalize();
		GetOffsetNode->ReconstructNode();

		if (UEdGraphPin* BoneNamePin = TMFindPinByName(GetOffsetNode, TEXT("BoneName"), EGPD_Input))
		{
			Schema->TrySetDefaultValue(*BoneNamePin, LogicalBoneName.ToString());
		}

		UEdGraphPin* ReturnValuePin = TMFindPinByName(GetOffsetNode, TEXT("ReturnValue"), EGPD_Output);
		if (ReturnValuePin && ReturnValuePin->SubPins.Num() == 0)
		{
			Schema->SplitPin(ReturnValuePin, true);
		}

		if (UEdGraphPin* ReturnValueLocationPin = TMFindPinByName(GetOffsetNode, TEXT("ReturnValue_Location"), EGPD_Output))
		{
			if (ReturnValueLocationPin->SubPins.Num() == 0)
			{
				Schema->SplitPin(ReturnValueLocationPin, true);
			}
		}

		ModifyBoneNode->Modify();
		TMSetOptionalPinVisible(ModifyBoneNode, TEXT("Translation"), true);
		ModifyBoneNode->ReconstructNode();

		UEdGraphPin* OffsetLocationXPin = TMFindPinByName(GetOffsetNode, TEXT("ReturnValue_Location_X"), EGPD_Output);
		UEdGraphPin* OffsetLocationYPin = TMFindPinByName(GetOffsetNode, TEXT("ReturnValue_Location_Y"), EGPD_Output);
		UEdGraphPin* OffsetLocationZPin = TMFindPinByName(GetOffsetNode, TEXT("ReturnValue_Location_Z"), EGPD_Output);
		if (!OffsetLocationXPin)
		{
			OffsetLocationXPin = TMFindPinBySuffix(GetOffsetNode, TEXT("Location_X"), EGPD_Output);
		}
		if (!OffsetLocationYPin)
		{
			OffsetLocationYPin = TMFindPinBySuffix(GetOffsetNode, TEXT("Location_Y"), EGPD_Output);
		}
		if (!OffsetLocationZPin)
		{
			OffsetLocationZPin = TMFindPinBySuffix(GetOffsetNode, TEXT("Location_Z"), EGPD_Output);
		}

		UEdGraphPin* TranslationPin = TMFindPinByName(ModifyBoneNode, TEXT("Translation"), EGPD_Input);
		if (TranslationPin && TranslationPin->SubPins.Num() == 0)
		{
			Schema->SplitPin(TranslationPin, true);
		}

		UEdGraphPin* TranslationXPin = TMFindPinByName(ModifyBoneNode, TEXT("Translation_X"), EGPD_Input);
		UEdGraphPin* TranslationYPin = TMFindPinByName(ModifyBoneNode, TEXT("Translation_Y"), EGPD_Input);
		UEdGraphPin* TranslationZPin = TMFindPinByName(ModifyBoneNode, TEXT("Translation_Z"), EGPD_Input);
		if (!TranslationXPin)
		{
			TranslationXPin = TMFindPinBySuffix(ModifyBoneNode, TEXT("Translation_X"), EGPD_Input);
		}
		if (!TranslationYPin)
		{
			TranslationYPin = TMFindPinBySuffix(ModifyBoneNode, TEXT("Translation_Y"), EGPD_Input);
		}
		if (!TranslationZPin)
		{
			TranslationZPin = TMFindPinBySuffix(ModifyBoneNode, TEXT("Translation_Z"), EGPD_Input);
		}

		const bool bConnected =
			OffsetLocationXPin
			&& OffsetLocationYPin
			&& OffsetLocationZPin
			&& TranslationXPin
			&& TranslationYPin
			&& TranslationZPin
			&& Schema->TryCreateConnection(OffsetLocationYPin, TranslationXPin)
			&& Schema->TryCreateConnection(OffsetLocationXPin, TranslationYPin)
			&& Schema->TryCreateConnection(OffsetLocationZPin, TranslationZPin);

		if (!bConnected)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMFakeGun] Failed to wire GetBoneOffset(%s) to ModifyBone %s translation pins."),
				*LogicalBoneName.ToString(),
				*ModifyBoneNode->GetName());
			return false;
		}

		return true;
	}

	UAnimGraphNode_ModifyBone* TMCreateFakeGunModifyBoneNode(
		UAnimBlueprint* AnimBlueprint,
		UEdGraph* Graph,
		const FName LogicalBoneName,
		const FName TargetBoneName,
		const int32 NodePosX,
		const int32 NodePosY)
	{
		UAnimGraphNode_ModifyBone* ModifyBoneNode = TMCreateAnimNode<UAnimGraphNode_ModifyBone>(Graph, NodePosX, NodePosY);
		ModifyBoneNode->Modify();
		ModifyBoneNode->Node.BoneToModify.BoneName = TargetBoneName;
		ModifyBoneNode->Node.BoneToModify.InvalidateCachedBoneIndex();
		ModifyBoneNode->Node.TranslationMode = BMM_Additive;
		ModifyBoneNode->Node.TranslationSpace = BCS_BoneSpace;
		ModifyBoneNode->Node.RotationMode = BMM_Ignore;
		ModifyBoneNode->Node.RotationSpace = BCS_ComponentSpace;
		ModifyBoneNode->Node.ScaleMode = BMM_Ignore;
		ModifyBoneNode->Node.ScaleSpace = BCS_ComponentSpace;
		ModifyBoneNode->ReconstructNode();

		if (!TMCreateAndConnectFakeGunOffsetGetter(
				AnimBlueprint,
				Graph,
				ModifyBoneNode,
				LogicalBoneName,
				NodePosX,
				NodePosY + 180))
		{
			ModifyBoneNode->DestroyNode();
			return nullptr;
		}

		return ModifyBoneNode;
	}

	bool TMBuildScarFakeAnimGraph(UAnimBlueprint* AnimBlueprint)
	{
		static const FName AnimGraphName(TEXT("AnimGraph"));
		if (!AnimBlueprint)
		{
			return false;
		}

		UEdGraph* Graph = TMFindGraphByName(AnimBlueprint, AnimGraphName);
		if (!Graph)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Failed to find AnimGraph in %s."), *AnimBlueprint->GetPathName());
			return false;
		}

		TArray<UAnimGraphNode_ModifyBone*> ExistingModifyBoneNodes;
		for (UEdGraphNode* GraphNode : Graph->Nodes)
		{
			if (UAnimGraphNode_ModifyBone* ModifyBoneNode = Cast<UAnimGraphNode_ModifyBone>(GraphNode))
			{
				ExistingModifyBoneNodes.Add(ModifyBoneNode);
			}
		}

		if (ExistingModifyBoneNodes.Num() > 0)
		{
			UE_LOG(LogTemp, Display, TEXT("[TMFakeGun] Scar AnimGraph already has %d ModifyBone node(s); not rebuilding graph."), ExistingModifyBoneNodes.Num());
			return true;
		}

		UAnimGraphNode_Root* RootNode = TMFindAnimGraphRootNode(Graph);
		if (!RootNode)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Failed to find AnimGraph root node."));
			return false;
		}

		AnimBlueprint->Modify();
		Graph->Modify();

		const UEdGraphSchema* Schema = Graph->GetSchema();
		UEdGraphPin* RootInputPin = TMFindFirstDataPin(RootNode, EGPD_Input);
		if (!Schema || !RootInputPin)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Failed to find AnimGraph root input pin."));
			return false;
		}

		RootInputPin->BreakAllPinLinks(false);

		UAnimGraphNode_LocalRefPose* RefPoseNode = TMCreateAnimNode<UAnimGraphNode_LocalRefPose>(Graph, -900, -80);
		UAnimGraphNode_LocalToComponentSpace* LocalToComponentNode =
			TMCreateAnimNode<UAnimGraphNode_LocalToComponentSpace>(Graph, -680, -80);
		UAnimGraphNode_ModifyBone* ChargingHandleNode = TMCreateFakeGunModifyBoneNode(
			AnimBlueprint,
			Graph,
			TEXT("Charging_handle"),
			TEXT("Charging_handle"),
			-420,
			-220);
		UAnimGraphNode_ModifyBone* MagazineNode = TMCreateFakeGunModifyBoneNode(
			AnimBlueprint,
			Graph,
			TEXT("Magazine"),
			TEXT("Tag_Mag_attach"),
			-160,
			-80);
		UAnimGraphNode_ModifyBone* TriggerNode = TMCreateFakeGunModifyBoneNode(
			AnimBlueprint,
			Graph,
			TEXT("Trigger"),
			TEXT("Trigger"),
			100,
			60);
		UAnimGraphNode_ComponentToLocalSpace* ComponentToLocalNode =
			TMCreateAnimNode<UAnimGraphNode_ComponentToLocalSpace>(Graph, 360, -80);

		if (!RefPoseNode
			|| !LocalToComponentNode
			|| !ChargingHandleNode
			|| !MagazineNode
			|| !TriggerNode
			|| !ComponentToLocalNode)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Failed to create one or more fake anim graph nodes."));
			return false;
		}

		UEdGraphPin* RefPoseOutputPin = TMFindPinByName(RefPoseNode, TEXT("Pose"), EGPD_Output);
		UEdGraphPin* LocalToComponentInputPin = TMFindPinByName(LocalToComponentNode, TEXT("LocalPose"), EGPD_Input);
		if (!LocalToComponentInputPin)
		{
			LocalToComponentInputPin = TMFindPinByName(LocalToComponentNode, TEXT("Pose"), EGPD_Input);
		}
		UEdGraphPin* LocalToComponentOutputPin = TMFindPinByName(LocalToComponentNode, TEXT("ComponentPose"), EGPD_Output);
		UEdGraphPin* ChargingInputPin = TMFindPinByName(ChargingHandleNode, TEXT("ComponentPose"), EGPD_Input);
		UEdGraphPin* ChargingOutputPin = TMFindPinByName(ChargingHandleNode, TEXT("Pose"), EGPD_Output);
		UEdGraphPin* MagazineInputPin = TMFindPinByName(MagazineNode, TEXT("ComponentPose"), EGPD_Input);
		UEdGraphPin* MagazineOutputPin = TMFindPinByName(MagazineNode, TEXT("Pose"), EGPD_Output);
		UEdGraphPin* TriggerInputPin = TMFindPinByName(TriggerNode, TEXT("ComponentPose"), EGPD_Input);
		UEdGraphPin* TriggerOutputPin = TMFindPinByName(TriggerNode, TEXT("Pose"), EGPD_Output);
		UEdGraphPin* ComponentToLocalInputPin = TMFindPinByName(ComponentToLocalNode, TEXT("ComponentPose"), EGPD_Input);
		UEdGraphPin* ComponentToLocalOutputPin = TMFindPinByName(ComponentToLocalNode, TEXT("Pose"), EGPD_Output);

		auto TryConnectPosePins = [Schema](UEdGraphPin* FromPin, UEdGraphPin* ToPin, const TCHAR* Label)
		{
			if (!FromPin || !ToPin)
			{
				UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Missing pose pin for %s: From=%s To=%s"), Label, *TMDescribePin(FromPin), *TMDescribePin(ToPin));
				return false;
			}

			const bool bResult = Schema->TryCreateConnection(FromPin, ToPin);
			UE_LOG(LogTemp, Display, TEXT("[TMFakeGun] Connect %s: %s -> %s : %s"), Label, *TMDescribePin(FromPin), *TMDescribePin(ToPin), bResult ? TEXT("OK") : TEXT("FAILED"));
			return bResult;
		};

		const bool bConnected =
			TryConnectPosePins(RefPoseOutputPin, LocalToComponentInputPin, TEXT("RefPose->LocalToComponent"))
			&& TryConnectPosePins(LocalToComponentOutputPin, ChargingInputPin, TEXT("LocalToComponent->Charging"))
			&& TryConnectPosePins(ChargingOutputPin, MagazineInputPin, TEXT("Charging->Magazine"))
			&& TryConnectPosePins(MagazineOutputPin, TriggerInputPin, TEXT("Magazine->Trigger"))
			&& TryConnectPosePins(TriggerOutputPin, ComponentToLocalInputPin, TEXT("Trigger->ComponentToLocal"))
			&& TryConnectPosePins(ComponentToLocalOutputPin, RootInputPin, TEXT("ComponentToLocal->Root"));

		if (!bConnected)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Failed to connect fake anim graph pose chain."));
			return false;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
		FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
		UE_LOG(LogTemp, Display, TEXT("[TMFakeGun] Blueprint status after fake anim graph build: %d"), static_cast<int32>(AnimBlueprint->Status));
		if (AnimBlueprint->Status == BS_Error)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Scar fake anim graph did not compile cleanly."));
			return false;
		}

		return true;
	}

	bool TMClearAnimGraphExceptRoot(UEdGraph* Graph)
	{
		if (!Graph)
		{
			return false;
		}

		TArray<UEdGraphNode*> NodesToRemove;
		for (UEdGraphNode* GraphNode : Graph->Nodes)
		{
			if (GraphNode && !GraphNode->IsA<UAnimGraphNode_Root>())
			{
				NodesToRemove.Add(GraphNode);
			}
		}

		for (UEdGraphNode* GraphNode : NodesToRemove)
		{
			GraphNode->Modify();
			GraphNode->DestroyNode();
		}

		UE_LOG(LogTemp, Display, TEXT("[TMFakeGun] Removed %d non-root node(s) from %s."), NodesToRemove.Num(), *Graph->GetName());
		return true;
	}

	bool TMBuildKrissFakeAnimGraph(UAnimBlueprint* AnimBlueprint)
	{
		static const FName AnimGraphName(TEXT("AnimGraph"));
		if (!AnimBlueprint)
		{
			return false;
		}

		UEdGraph* Graph = TMFindGraphByName(AnimBlueprint, AnimGraphName);
		if (!Graph)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Failed to find AnimGraph in %s."), *AnimBlueprint->GetPathName());
			return false;
		}

		UAnimGraphNode_Root* RootNode = TMFindAnimGraphRootNode(Graph);
		if (!RootNode)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Failed to find AnimGraph root node in %s."), *AnimBlueprint->GetPathName());
			return false;
		}

		AnimBlueprint->Modify();
		Graph->Modify();

		const UEdGraphSchema* Schema = Graph->GetSchema();
		UEdGraphPin* RootInputPin = TMFindFirstDataPin(RootNode, EGPD_Input);
		if (!Schema || !RootInputPin)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Failed to find AnimGraph root input pin."));
			return false;
		}

		RootInputPin->BreakAllPinLinks(false);
		if (!TMClearAnimGraphExceptRoot(Graph))
		{
			return false;
		}

		UAnimGraphNode_LocalRefPose* RefPoseNode = TMCreateAnimNode<UAnimGraphNode_LocalRefPose>(Graph, -900, -80);
		UAnimGraphNode_LocalToComponentSpace* LocalToComponentNode =
			TMCreateAnimNode<UAnimGraphNode_LocalToComponentSpace>(Graph, -680, -80);
		UAnimGraphNode_ModifyBone* ChargingHandleNode = TMCreateFakeGunModifyBoneNode(
			AnimBlueprint,
			Graph,
			TEXT("Charging_handle"),
			TEXT("Charge"),
			-420,
			-220);
		UAnimGraphNode_ModifyBone* MagazineNode = TMCreateFakeGunModifyBoneNode(
			AnimBlueprint,
			Graph,
			TEXT("Magazine"),
			TEXT("Mag"),
			-160,
			-80);
		UAnimGraphNode_ModifyBone* TriggerNode = TMCreateFakeGunModifyBoneNode(
			AnimBlueprint,
			Graph,
			TEXT("Trigger"),
			TEXT("Trigger"),
			100,
			60);
		UAnimGraphNode_ComponentToLocalSpace* ComponentToLocalNode =
			TMCreateAnimNode<UAnimGraphNode_ComponentToLocalSpace>(Graph, 360, -80);

		if (!RefPoseNode
			|| !LocalToComponentNode
			|| !ChargingHandleNode
			|| !MagazineNode
			|| !TriggerNode
			|| !ComponentToLocalNode)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Failed to create one or more Kriss fake anim graph nodes."));
			return false;
		}

		UEdGraphPin* RefPoseOutputPin = TMFindPinByName(RefPoseNode, TEXT("Pose"), EGPD_Output);
		UEdGraphPin* LocalToComponentInputPin = TMFindPinByName(LocalToComponentNode, TEXT("LocalPose"), EGPD_Input);
		if (!LocalToComponentInputPin)
		{
			LocalToComponentInputPin = TMFindPinByName(LocalToComponentNode, TEXT("Pose"), EGPD_Input);
		}
		UEdGraphPin* LocalToComponentOutputPin = TMFindPinByName(LocalToComponentNode, TEXT("ComponentPose"), EGPD_Output);
		UEdGraphPin* ChargingInputPin = TMFindPinByName(ChargingHandleNode, TEXT("ComponentPose"), EGPD_Input);
		UEdGraphPin* ChargingOutputPin = TMFindPinByName(ChargingHandleNode, TEXT("Pose"), EGPD_Output);
		UEdGraphPin* MagazineInputPin = TMFindPinByName(MagazineNode, TEXT("ComponentPose"), EGPD_Input);
		UEdGraphPin* MagazineOutputPin = TMFindPinByName(MagazineNode, TEXT("Pose"), EGPD_Output);
		UEdGraphPin* TriggerInputPin = TMFindPinByName(TriggerNode, TEXT("ComponentPose"), EGPD_Input);
		UEdGraphPin* TriggerOutputPin = TMFindPinByName(TriggerNode, TEXT("Pose"), EGPD_Output);
		UEdGraphPin* ComponentToLocalInputPin = TMFindPinByName(ComponentToLocalNode, TEXT("ComponentPose"), EGPD_Input);
		UEdGraphPin* ComponentToLocalOutputPin = TMFindPinByName(ComponentToLocalNode, TEXT("Pose"), EGPD_Output);

		auto TryConnectPosePins = [Schema](UEdGraphPin* FromPin, UEdGraphPin* ToPin, const TCHAR* Label)
		{
			if (!FromPin || !ToPin)
			{
				UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Missing pose pin for %s: From=%s To=%s"), Label, *TMDescribePin(FromPin), *TMDescribePin(ToPin));
				return false;
			}

			const bool bResult = Schema->TryCreateConnection(FromPin, ToPin);
			UE_LOG(LogTemp, Display, TEXT("[TMFakeGun] Connect %s: %s -> %s : %s"), Label, *TMDescribePin(FromPin), *TMDescribePin(ToPin), bResult ? TEXT("OK") : TEXT("FAILED"));
			return bResult;
		};

		const bool bConnected =
			TryConnectPosePins(RefPoseOutputPin, LocalToComponentInputPin, TEXT("RefPose->LocalToComponent"))
			&& TryConnectPosePins(LocalToComponentOutputPin, ChargingInputPin, TEXT("LocalToComponent->Charging"))
			&& TryConnectPosePins(ChargingOutputPin, MagazineInputPin, TEXT("Charging->Magazine"))
			&& TryConnectPosePins(MagazineOutputPin, TriggerInputPin, TEXT("Magazine->Trigger"))
			&& TryConnectPosePins(TriggerOutputPin, ComponentToLocalInputPin, TEXT("Trigger->ComponentToLocal"))
			&& TryConnectPosePins(ComponentToLocalOutputPin, RootInputPin, TEXT("ComponentToLocal->Root"));

		if (!bConnected)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Failed to connect Kriss fake anim graph pose chain."));
			return false;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
		FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
		UE_LOG(LogTemp, Display, TEXT("[TMFakeGun] Blueprint status after Kriss fake anim graph build: %d"), static_cast<int32>(AnimBlueprint->Status));
		if (AnimBlueprint->Status == BS_Error)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Kriss fake anim graph did not compile cleanly."));
			return false;
		}

		return true;
	}

	bool TMPatchKrissFakeAnimGraph()
	{
		static const TCHAR* KrissFakeAnimBlueprintPath =
			TEXT("/Game/Weapons/Mesh/SMG/Kriss_FakeAnimBP.Kriss_FakeAnimBP");

		UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, KrissFakeAnimBlueprintPath);
		if (!AnimBlueprint)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Failed to load anim blueprint: %s"), KrissFakeAnimBlueprintPath);
			return false;
		}

		TMDumpFakeGunAnimBlueprint(AnimBlueprint, TEXT("KrissBeforePatch"));
		if (!TMBuildKrissFakeAnimGraph(AnimBlueprint))
		{
			return false;
		}

		UFakeGunAnimInstance* DefaultAnimInstance = AnimBlueprint->GeneratedClass
			? Cast<UFakeGunAnimInstance>(AnimBlueprint->GeneratedClass->GetDefaultObject())
			: nullptr;
		if (!DefaultAnimInstance)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] %s is not based on UFakeGunAnimInstance."), KrissFakeAnimBlueprintPath);
			return false;
		}

		const TPair<FName, FName> ExpectedTargets[] =
		{
			{ TEXT("Charging_handle"), TEXT("Charge") },
			{ TEXT("Magazine"), TEXT("Mag") },
			{ TEXT("Trigger"), TEXT("Trigger") },
		};

		for (const TPair<FName, FName>& ExpectedTarget : ExpectedTargets)
		{
			const FName CurrentTargetBone = DefaultAnimInstance->EditorGetBoneReferenceTarget(ExpectedTarget.Key);
			if (CurrentTargetBone != ExpectedTarget.Value)
			{
				if (!DefaultAnimInstance->EditorSetBoneReferenceTarget(ExpectedTarget.Key, ExpectedTarget.Value))
				{
					UE_LOG(
						LogTemp,
						Error,
						TEXT("[TMFakeGun] BoneReferences has no logical key %s."),
						*ExpectedTarget.Key.ToString());
					return false;
				}

				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMFakeGun] Patched CDO BoneReferences[%s] target %s -> %s."),
					*ExpectedTarget.Key.ToString(),
					*CurrentTargetBone.ToString(),
					*ExpectedTarget.Value.ToString());
			}
		}

		TMDumpFakeGunAnimBlueprint(AnimBlueprint, TEXT("KrissAfterPatch"));

		UPackage* Package = AnimBlueprint->GetOutermost();
		const FString PackageFilename = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;

		if (!UPackage::SavePackage(Package, nullptr, *PackageFilename, SaveArgs))
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Failed to save package: %s"), *PackageFilename);
			return false;
		}

		UE_LOG(LogTemp, Display, TEXT("[TMFakeGun] Saved Kriss fake anim graph patch to %s."), *PackageFilename);
		return true;
	}

	bool TMSaveAssetPackage(UObject* Asset, const TCHAR* Label)
	{
		if (!Asset)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Cannot save null asset for %s."), Label);
			return false;
		}

		UPackage* Package = Asset->GetOutermost();
		const FString PackageFilename = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;

		if (!UPackage::SavePackage(Package, nullptr, *PackageFilename, SaveArgs))
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Failed to save %s package: %s"), Label, *PackageFilename);
			return false;
		}

		UE_LOG(LogTemp, Display, TEXT("[TMFakeGun] Saved %s package: %s"), Label, *PackageFilename);
		return true;
	}

	bool TMSetObjectProperty(UObject* Object, const FName PropertyName, UObject* Value)
	{
		FObjectPropertyBase* Property = Object
			? FindFProperty<FObjectPropertyBase>(Object->GetClass(), PropertyName)
			: nullptr;
		if (!Property)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Missing object property %s on %s."), *PropertyName.ToString(), Object ? *Object->GetPathName() : TEXT("None"));
			return false;
		}

		Property->SetObjectPropertyValue_InContainer(Object, Value);
		UE_LOG(LogTemp, Display, TEXT("[TMFakeGun] Set %s=%s on %s."), *PropertyName.ToString(), Value ? *Value->GetPathName() : TEXT("None"), *Object->GetPathName());
		return true;
	}

	bool TMSetClassProperty(UObject* Object, const FName PropertyName, UClass* Value)
	{
		FClassProperty* Property = Object
			? FindFProperty<FClassProperty>(Object->GetClass(), PropertyName)
			: nullptr;
		if (!Property)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Missing class property %s on %s."), *PropertyName.ToString(), Object ? *Object->GetPathName() : TEXT("None"));
			return false;
		}

		Property->SetPropertyValue_InContainer(Object, Value);
		UE_LOG(LogTemp, Display, TEXT("[TMFakeGun] Set %s=%s on %s."), *PropertyName.ToString(), Value ? *Value->GetPathName() : TEXT("None"), *Object->GetPathName());
		return true;
	}

	bool TMSetNameProperty(UObject* Object, const FName PropertyName, const FName Value)
	{
		FNameProperty* Property = Object
			? FindFProperty<FNameProperty>(Object->GetClass(), PropertyName)
			: nullptr;
		if (!Property)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Missing name property %s on %s."), *PropertyName.ToString(), Object ? *Object->GetPathName() : TEXT("None"));
			return false;
		}

		Property->SetPropertyValue_InContainer(Object, Value);
		UE_LOG(LogTemp, Display, TEXT("[TMFakeGun] Set %s=%s on %s."), *PropertyName.ToString(), *Value.ToString(), *Object->GetPathName());
		return true;
	}

	bool TMSetBoolProperty(UObject* Object, const FName PropertyName, const bool bValue)
	{
		FBoolProperty* Property = Object
			? FindFProperty<FBoolProperty>(Object->GetClass(), PropertyName)
			: nullptr;
		if (!Property)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Missing bool property %s on %s."), *PropertyName.ToString(), Object ? *Object->GetPathName() : TEXT("None"));
			return false;
		}

		Property->SetPropertyValue_InContainer(Object, bValue);
		UE_LOG(LogTemp, Display, TEXT("[TMFakeGun] Set %s=%d on %s."), *PropertyName.ToString(), bValue ? 1 : 0, *Object->GetPathName());
		return true;
	}

	bool TMSetTransformProperty(UObject* Object, const FName PropertyName, const FTransform& Value)
	{
		FStructProperty* Property = Object
			? FindFProperty<FStructProperty>(Object->GetClass(), PropertyName)
			: nullptr;
		if (!Property || Property->Struct != TBaseStructure<FTransform>::Get())
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Missing transform property %s on %s."), *PropertyName.ToString(), Object ? *Object->GetPathName() : TEXT("None"));
			return false;
		}

		Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Object), &Value);
		UE_LOG(LogTemp, Display, TEXT("[TMFakeGun] Set %s=%s on %s."), *PropertyName.ToString(), *Value.ToHumanReadableString(), *Object->GetPathName());
		return true;
	}

	bool TMPatchKrissFakeYaxisMagazine()
	{
		static const TCHAR* KrissBlueprintPath =
			TEXT("/Game/MP_System_V3/Game/Weapons/Primary/Kriss/BP_Kriss.BP_Kriss");
		static const TCHAR* KrissFakeAnimBlueprintPath =
			TEXT("/Game/Weapons/Mesh/SMG/Kriss_FakeAnimBP.Kriss_FakeAnimBP");
		static const TCHAR* KrissFakeAnimClassPath =
			TEXT("/Game/Weapons/Mesh/SMG/Kriss_FakeAnimBP.Kriss_FakeAnimBP_C");
		static const TCHAR* KrissYaxisMeshPath =
			TEXT("/Game/Weapons/Mesh/SMG/SK_V014_SMG_Yaxis.SK_V014_SMG_Yaxis");
		static const TCHAR* KrissMagEmptyMeshPath =
			TEXT("/Game/Weapons/Mesh/SMG/SK_V014_MagEmpty.SK_V014_MagEmpty");

		if (!TMPatchKrissFakeAnimGraph())
		{
			return false;
		}

		USkeletalMesh* KrissYaxisMesh = LoadObject<USkeletalMesh>(nullptr, KrissYaxisMeshPath);
		USkeletalMesh* KrissMagEmptyMesh = LoadObject<USkeletalMesh>(nullptr, KrissMagEmptyMeshPath);
		UAnimBlueprint* FakeAnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, KrissFakeAnimBlueprintPath);
		UClass* FakeAnimClass = LoadClass<UFakeGunAnimInstance>(nullptr, KrissFakeAnimClassPath);
		UBlueprint* KrissBlueprint = LoadObject<UBlueprint>(nullptr, KrissBlueprintPath);

		if (!KrissYaxisMesh || !KrissMagEmptyMesh || !FakeAnimBlueprint || !FakeAnimClass || !KrissBlueprint)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMFakeGun] Failed to load Kriss fake Y-axis assets. Mesh=%s Mag=%s AnimBP=%s AnimClass=%s BP=%s"),
				KrissYaxisMesh ? *KrissYaxisMesh->GetPathName() : TEXT("None"),
				KrissMagEmptyMesh ? *KrissMagEmptyMesh->GetPathName() : TEXT("None"),
				FakeAnimBlueprint ? *FakeAnimBlueprint->GetPathName() : TEXT("None"),
				FakeAnimClass ? *FakeAnimClass->GetPathName() : TEXT("None"),
				KrissBlueprint ? *KrissBlueprint->GetPathName() : TEXT("None"));
			return false;
		}

		USkeleton* YaxisSkeleton = KrissYaxisMesh->GetSkeleton();
		if (!YaxisSkeleton)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] %s has no skeleton."), *KrissYaxisMesh->GetPathName());
			return false;
		}

		if (FakeAnimBlueprint->TargetSkeleton != YaxisSkeleton)
		{
			FakeAnimBlueprint->Modify();
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMFakeGun] Kriss fake anim target skeleton %s -> %s."),
				FakeAnimBlueprint->TargetSkeleton ? *FakeAnimBlueprint->TargetSkeleton->GetPathName() : TEXT("None"),
				*YaxisSkeleton->GetPathName());
			FakeAnimBlueprint->TargetSkeleton = YaxisSkeleton;
			FBlueprintEditorUtils::MarkBlueprintAsModified(FakeAnimBlueprint);
			FKismetEditorUtilities::CompileBlueprint(FakeAnimBlueprint);

			if (FakeAnimBlueprint->Status == BS_Error)
			{
				UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Kriss fake anim blueprint failed after Y-axis skeleton switch."));
				return false;
			}

			if (!TMSaveAssetPackage(FakeAnimBlueprint, TEXT("Kriss fake anim Y-axis skeleton")))
			{
				return false;
			}
		}

		FKismetEditorUtilities::CompileBlueprint(KrissBlueprint);

		AGun* KrissDefaultObject = KrissBlueprint->GeneratedClass
			? Cast<AGun>(KrissBlueprint->GeneratedClass->GetDefaultObject())
			: nullptr;
		if (!KrissDefaultObject)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] %s generated class is not an AGun."), KrissBlueprintPath);
			return false;
		}

		KrissBlueprint->Modify();
		KrissDefaultObject->Modify();

		const bool bPatchedDefaults =
			TMSetBoolProperty(KrissDefaultObject, TEXT("bFakeMode"), true)
			&& TMSetObjectProperty(KrissDefaultObject, TEXT("FakeSkeletalMesh"), KrissYaxisMesh)
			&& TMSetClassProperty(KrissDefaultObject, TEXT("FakeAnimInstanceClass"), FakeAnimClass)
			&& TMSetObjectProperty(KrissDefaultObject, TEXT("FakeAttachedSkeletalMesh"), KrissMagEmptyMesh)
			&& TMSetNameProperty(KrissDefaultObject, TEXT("FakeAttachedSkeletalMeshSocketName"), TEXT("Mag"))
			&& TMSetTransformProperty(KrissDefaultObject, TEXT("FakeAttachedSkeletalMeshOffset"), FTransform::Identity);

		if (!bPatchedDefaults)
		{
			return false;
		}

		KrissBlueprint->MarkPackageDirty();
		KrissDefaultObject->MarkPackageDirty();
		return TMSaveAssetPackage(KrissBlueprint, TEXT("Kriss weapon fake Y-axis magazine"));
	}

	bool TMPatchScarFakeMagazineBone()
	{
		static const TCHAR* ScarFakeAnimBlueprintPath =
			TEXT("/Game/Modular_AR_Pack/Mesh/SCAL/Scar_FakeAnimBP.Scar_FakeAnimBP");
		static const FName LogicalMagazineBone(TEXT("Magazine"));
		static const FName OldTargetBone(TEXT("Mag_release"));
		static const FName NewTargetBone(TEXT("Tag_Mag_attach"));

		UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, ScarFakeAnimBlueprintPath);
		if (!AnimBlueprint)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Failed to load anim blueprint: %s"), ScarFakeAnimBlueprintPath);
			return false;
		}

		TMDumpFakeGunAnimBlueprint(AnimBlueprint, TEXT("BeforePatch"));
		if (!TMBuildScarFakeAnimGraph(AnimBlueprint))
		{
			return false;
		}

		AnimBlueprint->Modify();
		TArray<UAnimGraphNode_ModifyBone*> OldTargetNodes;
		TArray<UAnimGraphNode_ModifyBone*> OldTargetNodesUsingMagazine;
		TArray<UAnimGraphNode_ModifyBone*> NewTargetNodesUsingMagazine;

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
				UAnimGraphNode_ModifyBone* ModifyBoneNode = Cast<UAnimGraphNode_ModifyBone>(GraphNode);
				if (!ModifyBoneNode)
				{
					continue;
				}

				const FName BoneName = ModifyBoneNode->Node.BoneToModify.BoneName;
				const bool bUsesMagazineOffset = TMModifyBoneNodeUsesLogicalOffset(ModifyBoneNode, LogicalMagazineBone);
				if (BoneName == OldTargetBone)
				{
					OldTargetNodes.Add(ModifyBoneNode);
					if (bUsesMagazineOffset)
					{
						OldTargetNodesUsingMagazine.Add(ModifyBoneNode);
					}
				}
				else if (BoneName == NewTargetBone && bUsesMagazineOffset)
				{
					NewTargetNodesUsingMagazine.Add(ModifyBoneNode);
				}
			}
		}

		TArray<UAnimGraphNode_ModifyBone*> NodesToPatch;
		if (OldTargetNodesUsingMagazine.Num() > 0)
		{
			NodesToPatch = OldTargetNodesUsingMagazine;
		}
		else if (OldTargetNodes.Num() == 1)
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMFakeGun] Could not prove upstream GetBoneOffset(%s), but exactly one %s ModifyBone node exists; patching it."),
				*LogicalMagazineBone.ToString(),
				*OldTargetBone.ToString());
			NodesToPatch = OldTargetNodes;
		}
		else if (NewTargetNodesUsingMagazine.Num() > 0)
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMFakeGun] AnimGraph already routes %s to %s."),
				*LogicalMagazineBone.ToString(),
				*NewTargetBone.ToString());
		}
		else
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[TMFakeGun] Source AnimGraph did not expose a patchable fake magazine node: old target nodes=%d old target nodes using magazine=%d new target nodes using magazine=%d. Falling back to generated class/default data."),
				OldTargetNodes.Num(),
				OldTargetNodesUsingMagazine.Num(),
				NewTargetNodesUsingMagazine.Num());
		}

		bool bGraphChanged = false;
		for (UAnimGraphNode_ModifyBone* ModifyBoneNode : NodesToPatch)
		{
			if (!ModifyBoneNode)
			{
				continue;
			}

			if (UEdGraph* Graph = ModifyBoneNode->GetGraph())
			{
				Graph->Modify();
			}

			ModifyBoneNode->Modify();
			ModifyBoneNode->Node.BoneToModify.BoneName = NewTargetBone;
			ModifyBoneNode->Node.BoneToModify.InvalidateCachedBoneIndex();
			ModifyBoneNode->ReconstructNode();
			bGraphChanged = true;

			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMFakeGun] Patched ModifyBone node %s target %s -> %s."),
				*ModifyBoneNode->GetName(),
				*OldTargetBone.ToString(),
				*NewTargetBone.ToString());
		}

		if (bGraphChanged)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
			FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
			UE_LOG(LogTemp, Display, TEXT("[TMFakeGun] Blueprint status after graph patch: %d"), static_cast<int32>(AnimBlueprint->Status));
			if (AnimBlueprint->Status == BS_Error)
			{
				UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Graph patch did not compile cleanly; not saving."));
				return false;
			}
		}

		if (!AnimBlueprint->GeneratedClass)
		{
			FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
		}

		const int32 GeneratedPatchCount = TMPatchGeneratedModifyBoneTarget(AnimBlueprint, OldTargetBone, NewTargetBone);
		if (GeneratedPatchCount < 0)
		{
			return false;
		}

		UFakeGunAnimInstance* DefaultAnimInstance = AnimBlueprint->GeneratedClass
			? Cast<UFakeGunAnimInstance>(AnimBlueprint->GeneratedClass->GetDefaultObject())
			: nullptr;
		if (!DefaultAnimInstance)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] %s is not based on UFakeGunAnimInstance."), ScarFakeAnimBlueprintPath);
			return false;
		}

		const FName CurrentTargetBone = DefaultAnimInstance->EditorGetBoneReferenceTarget(LogicalMagazineBone);
		if (CurrentTargetBone != NewTargetBone)
		{
			if (!DefaultAnimInstance->EditorSetBoneReferenceTarget(LogicalMagazineBone, NewTargetBone))
			{
				UE_LOG(
					LogTemp,
					Error,
					TEXT("[TMFakeGun] BoneReferences has no logical key %s."),
					*LogicalMagazineBone.ToString());
				return false;
			}

			AnimBlueprint->MarkPackageDirty();
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMFakeGun] Patched CDO BoneReferences[%s] target %s -> %s."),
				*LogicalMagazineBone.ToString(),
				*CurrentTargetBone.ToString(),
				*NewTargetBone.ToString());
		}
		else
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMFakeGun] CDO BoneReferences[%s] already targets %s."),
				*LogicalMagazineBone.ToString(),
				*NewTargetBone.ToString());
		}

		TMDumpFakeGunAnimBlueprint(AnimBlueprint, TEXT("AfterPatch"));

		UPackage* Package = AnimBlueprint->GetOutermost();
		const FString PackageFilename = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;

		if (!UPackage::SavePackage(Package, nullptr, *PackageFilename, SaveArgs))
		{
			UE_LOG(LogTemp, Error, TEXT("[TMFakeGun] Failed to save package: %s"), *PackageFilename);
			return false;
		}

		UE_LOG(LogTemp, Display, TEXT("[TMFakeGun] Saved fake magazine patch to %s."), *PackageFilename);
		return true;
	}

	bool TMPatchAnimBlueprint(const TCHAR* TargetAnimBlueprintPath)
	{
		UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, TargetAnimBlueprintPath);
		if (!AnimBlueprint)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to load anim blueprint: %s"), TargetAnimBlueprintPath);
			return false;
		}

		static const FName MainAnimGraphName(TEXT("AnimGraph"));
		static const FName FabrikateTransformsGraphName(TEXT("FabrikateTransforms"));
		static const FName PoseAimTargetGraphName(TEXT("Pose_AimTarget"));
		static const FName CameraFPBoneName(TEXT("Camera_FP"));
		static const FName VisualPivotBoneName(TEXT("VB Control"));
		static const FName WeaponBoneName(TEXT("Weapon"));
		static const FName CameraWeaponOffsetPropertyName(TEXT("CameraWeaponOffset"));
		static const FName CameraWeaponOffsetAimingPropertyName(TEXT("CameraWeaponOffsetAiming"));
		static const FName LeftHandWeaponOffsetPropertyName(TEXT("LeftHandWeaponOffset"));

		if (!TMEnsureTransformVariable(AnimBlueprint, CameraWeaponOffsetPropertyName)
			|| !TMEnsureTransformVariable(AnimBlueprint, CameraWeaponOffsetAimingPropertyName)
			|| !TMEnsureTransformVariable(AnimBlueprint, LeftHandWeaponOffsetPropertyName))
		{
			return false;
		}

		UEdGraph* MainAnimGraph = TMFindGraphByName(AnimBlueprint, MainAnimGraphName);
		if (!MainAnimGraph)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to find graph %s in %s"), *MainAnimGraphName.ToString(), TargetAnimBlueprintPath);
			return false;
		}

		UEdGraph* Graph = TMFindGraphByName(AnimBlueprint, FabrikateTransformsGraphName);
		if (!Graph)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to find graph %s in %s"), *FabrikateTransformsGraphName.ToString(), TargetAnimBlueprintPath);
			return false;
		}

		UEdGraph* PoseAimTargetGraph = TMFindGraphByName(AnimBlueprint, PoseAimTargetGraphName);

		AnimBlueprint->Modify();
		MainAnimGraph->Modify();
		Graph->Modify();
		if (PoseAimTargetGraph)
		{
			PoseAimTargetGraph->Modify();
		}

		TMDumpRelevantAnimGraphLinks(AnimBlueprint, TEXT("BeforePatch"));

		TMClearCameraOffsetRotationPatchFromModifyBoneNodes(
			AnimBlueprint,
			MainAnimGraph,
			CameraFPBoneName,
			BMM_Ignore);
		TMClearCameraOffsetRotationPatchFromModifyBoneNodes(
			AnimBlueprint,
			Graph,
			VisualPivotBoneName,
			BMM_Additive);
		TMClearCameraOffsetRotationPatchFromModifyBoneNodes(
			AnimBlueprint,
			Graph,
			WeaponBoneName,
			BMM_Additive);

		if (!TMPatchCameraWeaponOffset(AnimBlueprint, Graph, VisualPivotBoneName))
		{
			return false;
		}

		if (!TMPatchLeftHandWeaponOffset(AnimBlueprint, Graph))
		{
			return false;
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
		FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);

		TMDumpRelevantAnimGraphLinks(AnimBlueprint, TEXT("AfterCompile"));

		if (!TMVerifyDedicatedCameraWeaponOffsetRotationLink(AnimBlueprint, Graph, VisualPivotBoneName))
		{
			return false;
		}

		UPackage* Package = AnimBlueprint->GetOutermost();
		const FString PackageFilename = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;

		if (!UPackage::SavePackage(Package, nullptr, *PackageFilename, SaveArgs))
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to save package: %s"), *PackageFilename);
			return false;
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("Patched %s: CameraWeaponOffset now drives dedicated VB Control rotation in FabrikateTransforms."),
			TargetAnimBlueprintPath);
		return true;
	}

	bool TMDumpTargetAnimBlueprintGraphs()
	{
		bool bSuccess = true;
		for (const TCHAR* TargetAnimBlueprintPath : TargetAnimBlueprintPaths)
		{
			UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, TargetAnimBlueprintPath);
			if (!AnimBlueprint)
			{
				UE_LOG(LogTemp, Error, TEXT("[TMAnimGraphDump] Failed to load anim blueprint: %s"), TargetAnimBlueprintPath);
				bSuccess = false;
				continue;
			}

			TMDumpRelevantAnimGraphLinks(AnimBlueprint, TEXT("ReadOnlyDump"));
		}

		return bSuccess;
	}

	bool TMIsFullAnimGraphDumpRelevant(const UEdGraph* Graph, const UEdGraphNode* Node)
	{
		if (!Graph || !Node)
		{
			return false;
		}

		const FString GraphPath = Graph->GetPathName();
		const FString GraphName = Graph->GetName();
		const FString NodeClassName = Node->GetClass() ? Node->GetClass()->GetName() : FString();
		const FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		const FString NodeText = GraphPath + TEXT(" ") + GraphName + TEXT(" ") + Node->GetName() + TEXT(" ")
			+ NodeClassName + TEXT(" ") + NodeTitle + TEXT(" ") + Node->NodeComment;

		static const TCHAR* InterestingText[] =
		{
			TEXT("Aim In"),
			TEXT("Aiming"),
			TEXT("Aim Out"),
			TEXT("Movement"),
			TEXT("UpperBody"),
			TEXT("AimOffset"),
			TEXT("Pose_AimTarget"),
			TEXT("SpineLayer"),
			TEXT("SpineLayers"),
			TEXT("Locally"),
			TEXT("Local")
		};

		for (const TCHAR* Needle : InterestingText)
		{
			if (NodeText.Contains(Needle, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		static const TCHAR* InterestingClasses[] =
		{
			TEXT("AnimGraphNode_StateResult"),
			TEXT("AnimGraphNode_SequencePlayer"),
			TEXT("AnimGraphNode_SequenceEvaluator"),
			TEXT("AnimGraphNode_ApplyAdditive"),
			TEXT("AnimGraphNode_BlendListByBool"),
			TEXT("AnimGraphNode_LayeredBoneBlend"),
			TEXT("AnimGraphNode_SaveCachedPose"),
			TEXT("AnimGraphNode_UseCachedPose"),
			TEXT("AnimGraphNode_Slot"),
			TEXT("AnimGraphNode_StateMachine"),
			TEXT("AnimGraphNode_LinkedAnimGraph"),
			TEXT("AnimGraphNode_LinkedAnimLayer")
		};

		for (const TCHAR* ClassNeedle : InterestingClasses)
		{
			if (NodeClassName.Contains(ClassNeedle, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}

	void TMDumpFullAnimGraphNodePins(const UEdGraphNode* Node)
	{
		if (!Node)
		{
			return;
		}

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin)
			{
				continue;
			}

			const UObject* SubCategoryObject = Pin->PinType.PinSubCategoryObject.Get();
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMFullAnimGraphDump]     Pin=%s Friendly=%s Dir=%s Cat=%s SubCat=%s Obj=%s Default={%s} Auto={%s} Links=%s Parent=%s SubPins=%d"),
				*Pin->PinName.ToString(),
				*Pin->PinFriendlyName.ToString(),
				Pin->Direction == EGPD_Input ? TEXT("In") : TEXT("Out"),
				*Pin->PinType.PinCategory.ToString(),
				*Pin->PinType.PinSubCategory.ToString(),
				*GetNameSafe(SubCategoryObject),
				*Pin->DefaultValue,
				*Pin->AutogeneratedDefaultValue,
				*TMDescribePinLinks(Pin),
				Pin->ParentPin ? *Pin->ParentPin->PinName.ToString() : TEXT("None"),
				Pin->SubPins.Num());
		}
	}

	bool TMDumpTargetAnimBlueprintFullGraphs()
	{
		bool bSuccess = true;
		for (const TCHAR* TargetAnimBlueprintPath : TargetAnimBlueprintPaths)
		{
			UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, TargetAnimBlueprintPath);
			if (!AnimBlueprint)
			{
				UE_LOG(LogTemp, Error, TEXT("[TMFullAnimGraphDump] Failed to load anim blueprint: %s"), TargetAnimBlueprintPath);
				bSuccess = false;
				continue;
			}

			TArray<UEdGraph*> Graphs;
			AnimBlueprint->GetAllGraphs(Graphs);
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMFullAnimGraphDump] Asset=%s Graphs=%d"),
				TargetAnimBlueprintPath,
				Graphs.Num());

			for (UEdGraph* Graph : Graphs)
			{
				if (!Graph)
				{
					continue;
				}

				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (!TMIsFullAnimGraphDumpRelevant(Graph, Node))
					{
						continue;
					}

					UE_LOG(
						LogTemp,
						Display,
						TEXT("[TMFullAnimGraphDump] Graph=%s GraphPath=%s Node=%s Class=%s Title={%s} Comment={%s} Pins=%d"),
						*Graph->GetName(),
						*Graph->GetPathName(),
						*Node->GetName(),
						Node->GetClass() ? *Node->GetClass()->GetName() : TEXT("None"),
						*Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString(),
						*Node->NodeComment,
						Node->Pins.Num());
					TMDumpFullAnimGraphNodePins(Node);
				}
			}
		}

		return bSuccess;
	}

	const TCHAR* TMLocalAimHeadRestoreComment = TEXT("TM Local Aim Head Restore");
	const FName TMLocalAimExcludedBranchBoneName(TEXT("neck_01"));

	bool TMIsLocalAimStateGraph(const UEdGraph* Graph)
	{
		if (!Graph)
		{
			return false;
		}

		static const FName AimInStateName(TEXT("Aim In"));
		static const FName AimingStateName(TEXT("Aiming"));
		static const FName AimOutStateName(TEXT("Aim Out"));

		const FName GraphName = Graph->GetFName();
		if (GraphName != AimInStateName && GraphName != AimingStateName && GraphName != AimOutStateName)
		{
			return false;
		}

		const FString GraphPath = Graph->GetPathName();
		return GraphPath.Contains(TEXT("UpperBody"), ESearchCase::IgnoreCase)
			&& GraphPath.Contains(TEXT("Movement"), ESearchCase::IgnoreCase);
	}

	UAnimGraphNode_SaveCachedPose* TMFindSaveCachedPoseNode(UAnimBlueprint* AnimBlueprint, const FString& CacheName)
	{
		if (!AnimBlueprint)
		{
			return nullptr;
		}

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
				UAnimGraphNode_SaveCachedPose* SaveCachedPoseNode = Cast<UAnimGraphNode_SaveCachedPose>(GraphNode);
				if (SaveCachedPoseNode && SaveCachedPoseNode->CacheName.Equals(CacheName, ESearchCase::CaseSensitive))
				{
					return SaveCachedPoseNode;
				}
			}
		}

		return nullptr;
	}

	UAnimGraphNode_StateResult* TMFindStateResultNode(UEdGraph* Graph)
	{
		if (!Graph)
		{
			return nullptr;
		}

		UAnimGraphNode_StateResult* FoundNode = nullptr;
		int32 FoundCount = 0;
		for (UEdGraphNode* GraphNode : Graph->Nodes)
		{
			if (UAnimGraphNode_StateResult* StateResultNode = Cast<UAnimGraphNode_StateResult>(GraphNode))
			{
				FoundNode = FoundNode ? FoundNode : StateResultNode;
				++FoundCount;
			}
		}

		if (FoundCount > 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TMLocalAimHeadRestore] Graph %s has %d state result nodes; using %s."), *Graph->GetPathName(), FoundCount, *GetNameSafe(FoundNode));
		}

		return FoundNode;
	}

	UAnimGraphNode_LayeredBoneBlend* TMFindLocalAimHeadRestoreNode(const UEdGraphPin* ResultPin)
	{
		if (!ResultPin || ResultPin->LinkedTo.Num() != 1 || !ResultPin->LinkedTo[0])
		{
			return nullptr;
		}

		UAnimGraphNode_LayeredBoneBlend* SourceNode = Cast<UAnimGraphNode_LayeredBoneBlend>(ResultPin->LinkedTo[0]->GetOwningNode());
		return SourceNode && SourceNode->NodeComment.Equals(TMLocalAimHeadRestoreComment, ESearchCase::CaseSensitive)
			? SourceNode
			: nullptr;
	}

	bool TMEnsureLocalAimExcludedBranch(UAnimGraphNode_LayeredBoneBlend* LayerNode)
	{
		if (!LayerNode)
		{
			return false;
		}

		bool bChanged = false;
		auto MarkChanged = [&bChanged, LayerNode]()
		{
			if (!bChanged)
			{
				LayerNode->Modify();
				bChanged = true;
			}
		};

		if (LayerNode->Node.BlendMode != ELayeredBoneBlendMode::BranchFilter)
		{
			MarkChanged();
			LayerNode->Node.BlendMode = ELayeredBoneBlendMode::BranchFilter;
		}

		if (!LayerNode->Node.bMeshSpaceRotationBlend)
		{
			MarkChanged();
			LayerNode->Node.bMeshSpaceRotationBlend = true;
		}

		if (LayerNode->Node.LayerSetup.Num() == 0)
		{
			MarkChanged();
			LayerNode->Node.LayerSetup.AddDefaulted();
		}

		FInputBlendPose& FirstLayer = LayerNode->Node.LayerSetup[0];
		const bool bNeedsFilterUpdate =
			FirstLayer.BranchFilters.Num() != 1
			|| FirstLayer.BranchFilters[0].BoneName != TMLocalAimExcludedBranchBoneName
			|| FirstLayer.BranchFilters[0].BlendDepth != 0;

		if (bNeedsFilterUpdate)
		{
			MarkChanged();
			FirstLayer.BranchFilters.Reset();

			FBranchFilter NeckBranchFilter;
			NeckBranchFilter.BoneName = TMLocalAimExcludedBranchBoneName;
			NeckBranchFilter.BlendDepth = 0;
			FirstLayer.BranchFilters.Add(NeckBranchFilter);
		}

		if (bChanged)
		{
			LayerNode->Node.InvalidatePerBoneBlendWeights();
			LayerNode->ReconstructNode();
		}

		return bChanged;
	}

	UEdGraphPin* TMFindVariableGetOutputPin(UK2Node_VariableGet* VariableGetNode, const FName VariableName)
	{
		if (UEdGraphPin* NamedPin = TMFindPinByName(VariableGetNode, VariableName, EGPD_Output))
		{
			return NamedPin;
		}

		return TMFindFirstDataPin(VariableGetNode, EGPD_Output);
	}

	void TMDestroyCreatedGraphNodes(const TArray<UEdGraphNode*>& Nodes)
	{
		for (UEdGraphNode* Node : Nodes)
		{
			if (Node)
			{
				Node->Modify();
				Node->DestroyNode();
			}
		}
	}

	bool TMPatchLocalAimStateGraph(
		UAnimBlueprint* AnimBlueprint,
		UEdGraph* Graph,
		UAnimGraphNode_SaveCachedPose* HeadSourceCachedPoseNode,
		bool& bOutChanged)
	{
		bOutChanged = false;
		if (!AnimBlueprint || !Graph || !HeadSourceCachedPoseNode)
		{
			return false;
		}

		const UEdGraphSchema* Schema = Graph->GetSchema();
		UAnimGraphNode_StateResult* StateResultNode = TMFindStateResultNode(Graph);
		UEdGraphPin* ResultPin = StateResultNode ? TMFindPinByName(StateResultNode, TEXT("Result"), EGPD_Input) : nullptr;
		if (!Schema || !StateResultNode || !ResultPin)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLocalAimHeadRestore] Missing schema or result pin in graph %s."), *Graph->GetPathName());
			return false;
		}

		if (UAnimGraphNode_LayeredBoneBlend* ExistingLayerNode = TMFindLocalAimHeadRestoreNode(ResultPin))
		{
			const bool bUpdated = TMEnsureLocalAimExcludedBranch(ExistingLayerNode);
			if (bUpdated)
			{
				AnimBlueprint->Modify();
				Graph->Modify();
				bOutChanged = true;
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMLocalAimHeadRestore] Updated %s to restore branch '%s' from cached pose when IsLocalPlayer is true."),
					*Graph->GetPathName(),
					*TMLocalAimExcludedBranchBoneName.ToString());
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("[TMLocalAimHeadRestore] %s already patched."), *Graph->GetPathName());
			}
			return true;
		}

		if (ResultPin->LinkedTo.Num() != 1 || !ResultPin->LinkedTo[0])
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMLocalAimHeadRestore] Expected one source link for %s.Result, found %d."),
				*StateResultNode->GetName(),
				ResultPin->LinkedTo.Num());
			return false;
		}

		UEdGraphPin* OriginalPosePin = ResultPin->LinkedTo[0];
		const int32 LayerNodeX = StateResultNode->NodePosX - 300;
		const int32 LayerNodeY = StateResultNode->NodePosY;
		const int32 BaseNodeX = LayerNodeX - 320;
		const int32 BaseNodeY = LayerNodeY + 220;
		const int32 LocalNodeX = LayerNodeX - 560;
		const int32 LocalNodeY = LayerNodeY + 430;
		const int32 BoolToFloatNodeX = LayerNodeX - 280;
		const int32 BoolToFloatNodeY = LayerNodeY + 430;

		AnimBlueprint->Modify();
		Graph->Modify();

		TArray<UEdGraphNode*> CreatedNodes;
		UAnimGraphNode_LayeredBoneBlend* LayerNode = TMCreateAnimNode<UAnimGraphNode_LayeredBoneBlend>(Graph, LayerNodeX, LayerNodeY);
		UAnimGraphNode_UseCachedPose* UseHeadSourcePoseNode = TMCreateAnimNode<UAnimGraphNode_UseCachedPose>(Graph, BaseNodeX, BaseNodeY);
		CreatedNodes.Add(LayerNode);
		CreatedNodes.Add(UseHeadSourcePoseNode);

		UFunction* BoolToDoubleFunction =
			UKismetMathLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Conv_BoolToDouble));
		if (!LayerNode || !UseHeadSourcePoseNode || !BoolToDoubleFunction)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLocalAimHeadRestore] Failed to create required anim nodes in %s."), *Graph->GetPathName());
			TMDestroyCreatedGraphNodes(CreatedNodes);
			return false;
		}

		LayerNode->Modify();
		LayerNode->NodeComment = TMLocalAimHeadRestoreComment;
		LayerNode->Node.BlendMode = ELayeredBoneBlendMode::BranchFilter;
		LayerNode->Node.BlendWeights.Reset();
		LayerNode->Node.BlendPoses.Reset();
		LayerNode->Node.BlendMasks.Reset();
		LayerNode->Node.LayerSetup.Reset();
		LayerNode->Node.AddPose();
		LayerNode->Node.bMeshSpaceRotationBlend = true;
		LayerNode->Node.LayerSetup[0].BranchFilters.Reset();

		TMEnsureLocalAimExcludedBranch(LayerNode);

		UseHeadSourcePoseNode->Modify();
		UseHeadSourcePoseNode->NodeComment = TEXT("TM Local Aim Head Restore: source pose for neck/head");
		UseHeadSourcePoseNode->SaveCachedPoseNode = HeadSourceCachedPoseNode;
		UseHeadSourcePoseNode->ReconstructNode();

		FGraphNodeCreator<UK2Node_VariableGet> LocalPlayerCreator(*Graph);
		UK2Node_VariableGet* LocalPlayerNode = LocalPlayerCreator.CreateNode();
		LocalPlayerNode->VariableReference.SetSelfMember(TEXT("IsLocalPlayer"));
		LocalPlayerNode->NodePosX = LocalNodeX;
		LocalPlayerNode->NodePosY = LocalNodeY;
		LocalPlayerNode->NodeComment = TEXT("TM Local Aim Head Restore: local player gate");
		LocalPlayerCreator.Finalize();
		LocalPlayerNode->ReconstructNode();
		CreatedNodes.Add(LocalPlayerNode);

		FGraphNodeCreator<UK2Node_CallFunction> BoolToFloatCreator(*Graph);
		UK2Node_CallFunction* BoolToFloatNode = BoolToFloatCreator.CreateNode();
		BoolToFloatNode->SetFromFunction(BoolToDoubleFunction);
		BoolToFloatNode->NodePosX = BoolToFloatNodeX;
		BoolToFloatNode->NodePosY = BoolToFloatNodeY;
		BoolToFloatNode->NodeComment = TEXT("TM Local Aim Head Restore: bool to weight");
		BoolToFloatCreator.Finalize();
		BoolToFloatNode->ReconstructNode();
		CreatedNodes.Add(BoolToFloatNode);

		UEdGraphPin* LayerBasePosePin = TMFindPinByName(LayerNode, TEXT("BasePose"), EGPD_Input);
		UEdGraphPin* LayerHeadPosePin = TMFindPinByName(LayerNode, TEXT("BlendPoses_0"), EGPD_Input);
		UEdGraphPin* LayerWeightPin = TMFindPinByName(LayerNode, TEXT("BlendWeights_0"), EGPD_Input);
		UEdGraphPin* LayerOutputPosePin = TMFindPinByName(LayerNode, TEXT("Pose"), EGPD_Output);
		UEdGraphPin* HeadSourcePosePin = TMFindPinByName(UseHeadSourcePoseNode, TEXT("Pose"), EGPD_Output);
		UEdGraphPin* LocalPlayerPin = TMFindVariableGetOutputPin(LocalPlayerNode, TEXT("IsLocalPlayer"));
		UEdGraphPin* BoolInputPin = TMFindPinByName(BoolToFloatNode, TEXT("InBool"), EGPD_Input);
		UEdGraphPin* WeightOutputPin = TMFindPinByName(BoolToFloatNode, TEXT("ReturnValue"), EGPD_Output);

		if (!LayerBasePosePin
			|| !LayerHeadPosePin
			|| !LayerWeightPin
			|| !LayerOutputPosePin
			|| !HeadSourcePosePin
			|| !LocalPlayerPin
			|| !BoolInputPin
			|| !WeightOutputPin)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMLocalAimHeadRestore] Missing pins in %s. LayerBase=%s Head=%s Weight=%s Out=%s Base=%s Local=%s BoolIn=%s WeightOut=%s"),
				*Graph->GetPathName(),
				*TMDescribePin(LayerBasePosePin),
				*TMDescribePin(LayerHeadPosePin),
				*TMDescribePin(LayerWeightPin),
				*TMDescribePin(LayerOutputPosePin),
				*TMDescribePin(HeadSourcePosePin),
				*TMDescribePin(LocalPlayerPin),
				*TMDescribePin(BoolInputPin),
				*TMDescribePin(WeightOutputPin));
			TMDestroyCreatedGraphNodes(CreatedNodes);
			return false;
		}

		ResultPin->Modify();
		ResultPin->BreakLinkTo(OriginalPosePin);

		const bool bConnected =
			Schema->TryCreateConnection(OriginalPosePin, LayerBasePosePin)
			&& Schema->TryCreateConnection(HeadSourcePosePin, LayerHeadPosePin)
			&& Schema->TryCreateConnection(LocalPlayerPin, BoolInputPin)
			&& Schema->TryCreateConnection(WeightOutputPin, LayerWeightPin)
			&& Schema->TryCreateConnection(LayerOutputPosePin, ResultPin);

		if (!bConnected)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLocalAimHeadRestore] Failed to wire local aim head restore in %s."), *Graph->GetPathName());
			ResultPin->BreakAllPinLinks(false);
			Schema->TryCreateConnection(OriginalPosePin, ResultPin);
			TMDestroyCreatedGraphNodes(CreatedNodes);
			return false;
		}

		bOutChanged = true;
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMLocalAimHeadRestore] Patched %s: %s now restores branch '%s' from cached pose '%s' when IsLocalPlayer is true."),
			*AnimBlueprint->GetPathName(),
			*Graph->GetName(),
			*TMLocalAimExcludedBranchBoneName.ToString(),
			*HeadSourceCachedPoseNode->CacheName);
		return true;
	}

	bool TMPatchLocalAimHeadRestore()
	{
		bool bSuccess = true;
		for (const TCHAR* TargetAnimBlueprintPath : TargetAnimBlueprintPaths)
		{
			UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, TargetAnimBlueprintPath);
			if (!AnimBlueprint)
			{
				UE_LOG(LogTemp, Error, TEXT("[TMLocalAimHeadRestore] Failed to load anim blueprint: %s"), TargetAnimBlueprintPath);
				bSuccess = false;
				continue;
			}

			UAnimGraphNode_SaveCachedPose* HeadSourceCachedPoseNode = TMFindSaveCachedPoseNode(AnimBlueprint, TEXT("LowerBody"));
			if (!HeadSourceCachedPoseNode)
			{
				UE_LOG(LogTemp, Error, TEXT("[TMLocalAimHeadRestore] %s has no SaveCachedPose named LowerBody."), TargetAnimBlueprintPath);
				bSuccess = false;
				continue;
			}

			bool bAssetSuccess = true;
			bool bAssetChanged = false;
			int32 CandidateGraphCount = 0;
			TArray<UEdGraph*> Graphs;
			AnimBlueprint->GetAllGraphs(Graphs);
			for (UEdGraph* Graph : Graphs)
			{
				if (!TMIsLocalAimStateGraph(Graph))
				{
					continue;
				}

				++CandidateGraphCount;
				bool bGraphChanged = false;
				if (!TMPatchLocalAimStateGraph(AnimBlueprint, Graph, HeadSourceCachedPoseNode, bGraphChanged))
				{
					bAssetSuccess = false;
					break;
				}

				bAssetChanged |= bGraphChanged;
			}

			if (CandidateGraphCount == 0)
			{
				UE_LOG(LogTemp, Error, TEXT("[TMLocalAimHeadRestore] Found no Aim In/Aiming/Aim Out state graphs in %s."), TargetAnimBlueprintPath);
				bAssetSuccess = false;
			}

			if (!bAssetSuccess)
			{
				bSuccess = false;
				continue;
			}

			if (!bAssetChanged)
			{
				UE_LOG(LogTemp, Display, TEXT("[TMLocalAimHeadRestore] %s already up to date."), TargetAnimBlueprintPath);
				continue;
			}

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
			FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
			UE_LOG(LogTemp, Display, TEXT("[TMLocalAimHeadRestore] Blueprint status after compile for %s: %d"), TargetAnimBlueprintPath, static_cast<int32>(AnimBlueprint->Status));
			if (AnimBlueprint->Status == BS_Error)
			{
				UE_LOG(LogTemp, Error, TEXT("[TMLocalAimHeadRestore] %s did not compile cleanly; not saving."), TargetAnimBlueprintPath);
				bSuccess = false;
				continue;
			}

			if (!TMSavePackageForAsset(AnimBlueprint, TEXT("TMLocalAimHeadRestore")))
			{
				bSuccess = false;
			}
		}

		return bSuccess;
	}

	UEdGraphPin* TMFindFabrikateAlphaSourcePin(UEdGraph* Graph)
	{
		if (!Graph)
		{
			return nullptr;
		}

		UEdGraphPin* FallbackPin = nullptr;
		for (UEdGraphNode* GraphNode : Graph->Nodes)
		{
			UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(GraphNode);
			if (!CallFunctionNode || CallFunctionNode->FunctionReference.GetMemberName() != TEXT("MapRangeClamped"))
			{
				continue;
			}

			UEdGraphPin* ReturnPin = TMFindPinByName(CallFunctionNode, UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
			if (!ReturnPin)
			{
				continue;
			}

			if (CallFunctionNode->GetName() == TEXT("K2Node_CallFunction_14"))
			{
				return ReturnPin;
			}

			if (!FallbackPin)
			{
				FallbackPin = ReturnPin;
			}
		}

		return FallbackPin;
	}

	bool TMRestoreAlphaPinToSource(UEdGraph* Graph, UEdGraphNode* Node, UEdGraphPin* SourcePin)
	{
		UEdGraphPin* AlphaPin = TMFindPinByName(Node, TEXT("Alpha"), EGPD_Input);
		if (!Graph || !AlphaPin || !SourcePin)
		{
			return false;
		}

		bool bChanged = false;
		const bool bAlreadyLinkedToSource = AlphaPin->LinkedTo.Num() == 1 && AlphaPin->LinkedTo[0] == SourcePin;
		if (!bAlreadyLinkedToSource)
		{
			AlphaPin->Modify();
			AlphaPin->BreakAllPinLinks();
			const UEdGraphSchema* Schema = Graph->GetSchema();
			if (!Schema || !Schema->TryCreateConnection(SourcePin, AlphaPin))
			{
				UE_LOG(LogTemp, Error, TEXT("[TMRestoreFabrikAlpha] Failed to connect %s.%s -> %s.%s"),
					*GetNameSafe(SourcePin->GetOwningNode()),
					*SourcePin->PinName.ToString(),
					*GetNameSafe(Node),
					*AlphaPin->PinName.ToString());
				return false;
			}
			bChanged = true;
		}

		if (AlphaPin->DefaultValue != TEXT("0.5"))
		{
			AlphaPin->Modify();
			AlphaPin->DefaultValue = TEXT("0.5");
			bChanged = true;
		}

		return bChanged;
	}

	bool TMPatchFabrikateTransformsRestoreHalfAlpha(const TCHAR* TargetAnimBlueprintPath)
	{
		UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, TargetAnimBlueprintPath);
		if (!AnimBlueprint)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMRestoreFabrikAlpha] Failed to load anim blueprint: %s"), TargetAnimBlueprintPath);
			return false;
		}

		TArray<UEdGraph*> Graphs;
		AnimBlueprint->GetAllGraphs(Graphs);

		bool bChanged = false;
		int32 PatchedCount = 0;
		TMDumpRelevantAnimGraphLinks(AnimBlueprint, TEXT("BeforeRestoreFabrikAlpha"));

		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph || Graph->GetName() != TEXT("FabrikateTransforms"))
			{
				continue;
			}

			UEdGraphPin* AlphaSourcePin = TMFindFabrikateAlphaSourcePin(Graph);
			if (!AlphaSourcePin)
			{
				UE_LOG(LogTemp, Error, TEXT("[TMRestoreFabrikAlpha] Failed to find MapRangeClamped alpha source in %s"), TargetAnimBlueprintPath);
				return false;
			}

			for (UEdGraphNode* GraphNode : Graph->Nodes)
			{
				UAnimGraphNode_Fabrik* FabrikNode = Cast<UAnimGraphNode_Fabrik>(GraphNode);
				if (!FabrikNode)
				{
					continue;
				}

				const FName TipBoneName = FabrikNode->Node.TipBone.BoneName;
				const FName EffectorTargetName = FabrikNode->Node.EffectorTarget.bUseSocket
					? FabrikNode->Node.EffectorTarget.SocketReference.SocketName
					: FabrikNode->Node.EffectorTarget.BoneReference.BoneName;
				const bool bIsMPHandFabrik =
					((TipBoneName == TEXT("hand_l") && EffectorTargetName == TEXT("VB Hand_L"))
						|| (TipBoneName == TEXT("hand_r") && EffectorTargetName == TEXT("VB Hand_R")));
				if (!bIsMPHandFabrik)
				{
					continue;
				}

				FabrikNode->Modify();
				if (FabrikNode->Node.AlphaInputType != EAnimAlphaInputType::Float)
				{
					FabrikNode->Node.AlphaInputType = EAnimAlphaInputType::Float;
					bChanged = true;
				}
				if (!FMath::IsNearlyEqual(FabrikNode->Node.Alpha, 0.5f))
				{
					FabrikNode->Node.Alpha = 0.5f;
					bChanged = true;
				}
				if (!FabrikNode->Node.AlphaCurveName.IsNone())
				{
					FabrikNode->Node.AlphaCurveName = NAME_None;
					bChanged = true;
				}

				if (TMRestoreAlphaPinToSource(Graph, FabrikNode, AlphaSourcePin))
				{
					bChanged = true;
				}

				++PatchedCount;
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMRestoreFabrikAlpha] Restored half alpha for %s Graph=%s Node=%s Tip=%s Effector=%s Source=%s.%s"),
					TargetAnimBlueprintPath,
					*Graph->GetName(),
					*FabrikNode->GetName(),
					*TipBoneName.ToString(),
					*EffectorTargetName.ToString(),
					*GetNameSafe(AlphaSourcePin->GetOwningNode()),
					*AlphaSourcePin->PinName.ToString());
			}
		}

		if (PatchedCount == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TMRestoreFabrikAlpha] No hand FABRIK nodes found in FabrikateTransforms: %s"), TargetAnimBlueprintPath);
			return true;
		}

		if (!bChanged)
		{
			UE_LOG(LogTemp, Display, TEXT("[TMRestoreFabrikAlpha] Already restored: %s"), TargetAnimBlueprintPath);
			return true;
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
		FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
		TMDumpRelevantAnimGraphLinks(AnimBlueprint, TEXT("AfterRestoreFabrikAlpha"));

		return TMSavePackageForAsset(AnimBlueprint, TEXT("TMRestoreFabrikAlpha"));
	}

	bool TMSetAlphaPinConstant(UEdGraphNode* Node, const TCHAR* DefaultValue)
	{
		UEdGraphPin* AlphaPin = TMFindPinByName(Node, TEXT("Alpha"), EGPD_Input);
		if (!AlphaPin)
		{
			return false;
		}

		bool bChanged = false;
		if (AlphaPin->LinkedTo.Num() > 0)
		{
			AlphaPin->Modify();
			AlphaPin->BreakAllPinLinks();
			bChanged = true;
		}

		if (AlphaPin->DefaultValue != DefaultValue)
		{
			AlphaPin->Modify();
			AlphaPin->DefaultValue = DefaultValue;
			bChanged = true;
		}

		return bChanged;
	}

	bool TMPatchActiveLeftHandFabrikConstantAlpha()
	{
		static const TCHAR* ActiveAnimBlueprintPath =
			TEXT("/Game/Test/MPVS_SkeletonProbe/ImportedOnALS/ABP_UE5_MPSBones_OnALS.ABP_UE5_MPSBones_OnALS");

		UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, ActiveAnimBlueprintPath);
		if (!AnimBlueprint)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLeftHandFabrikAlpha] Failed to load anim blueprint: %s"), ActiveAnimBlueprintPath);
			return false;
		}

		TArray<UEdGraph*> Graphs;
		AnimBlueprint->GetAllGraphs(Graphs);

		bool bChanged = false;
		bool bFoundLeftHandFabrik = false;
		TMDumpRelevantAnimGraphLinks(AnimBlueprint, TEXT("BeforeLeftHandFabrikAlpha"));

		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph || Graph->GetName() != TEXT("FabrikateTransforms"))
			{
				continue;
			}

			for (UEdGraphNode* GraphNode : Graph->Nodes)
			{
				UAnimGraphNode_Fabrik* FabrikNode = Cast<UAnimGraphNode_Fabrik>(GraphNode);
				if (!FabrikNode)
				{
					continue;
				}

				const FName TipBoneName = FabrikNode->Node.TipBone.BoneName;
				const FName EffectorTargetName = FabrikNode->Node.EffectorTarget.bUseSocket
					? FabrikNode->Node.EffectorTarget.SocketReference.SocketName
					: FabrikNode->Node.EffectorTarget.BoneReference.BoneName;
				if (TipBoneName != TEXT("hand_l") || EffectorTargetName != TEXT("VB Hand_L"))
				{
					continue;
				}

				bFoundLeftHandFabrik = true;
				FabrikNode->Modify();
				if (FabrikNode->Node.AlphaInputType != EAnimAlphaInputType::Float)
				{
					FabrikNode->Node.AlphaInputType = EAnimAlphaInputType::Float;
					bChanged = true;
				}
				if (!FMath::IsNearlyEqual(FabrikNode->Node.Alpha, 0.5f))
				{
					FabrikNode->Node.Alpha = 0.5f;
					bChanged = true;
				}
				if (!FabrikNode->Node.AlphaCurveName.IsNone())
				{
					FabrikNode->Node.AlphaCurveName = NAME_None;
					bChanged = true;
				}
				if (TMSetAlphaPinConstant(FabrikNode, TEXT("0.5")))
				{
					bChanged = true;
				}

				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMLeftHandFabrikAlpha] Constant left alpha for Graph=%s Node=%s Tip=%s Effector=%s"),
					*Graph->GetName(),
					*FabrikNode->GetName(),
					*TipBoneName.ToString(),
					*EffectorTargetName.ToString());
			}
		}

		if (!bFoundLeftHandFabrik)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLeftHandFabrikAlpha] No left hand FABRIK node found in active anim blueprint."));
			return false;
		}

		if (!bChanged)
		{
			UE_LOG(LogTemp, Display, TEXT("[TMLeftHandFabrikAlpha] Already patched."));
			return true;
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
		FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
		TMDumpRelevantAnimGraphLinks(AnimBlueprint, TEXT("AfterLeftHandFabrikAlpha"));

		return TMSavePackageForAsset(AnimBlueprint, TEXT("TMLeftHandFabrikAlpha"));
	}

	bool TMRefreshAttachmentAssets()
	{
		UDataTable* MuzzleTable = LoadObject<UDataTable>(
			nullptr,
			TEXT("/Game/MP_System_V3/Game/Blueprints/DataTables/DT_Muzzle.DT_Muzzle"));
		if (MuzzleTable)
		{
			const TArray<FName> RowNames = MuzzleTable->GetRowNames();
			FString RowList;
			for (const FName RowName : RowNames)
			{
				RowList += RowName.ToString();
				RowList += TEXT(" ");
			}

			UE_LOG(LogTemp, Display, TEXT("[TMAttachmentRefresh] DT_Muzzle rows: %s"), *RowList);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[TMAttachmentRefresh] Failed to load DT_Muzzle."));
			return false;
		}

		UEnum* MuzzleEnum = LoadObject<UEnum>(
			nullptr,
			TEXT("/Game/MP_System_V3/Game/Blueprints/Enums/Attachments/ENUM_Muzzle.ENUM_Muzzle"));
		if (MuzzleEnum)
		{
			FString EnumList;
			for (int32 Index = 0; Index < MuzzleEnum->NumEnums(); ++Index)
			{
				EnumList += MuzzleEnum->GetNameStringByIndex(Index);
				EnumList += TEXT(" ");
			}

			UE_LOG(LogTemp, Display, TEXT("[TMAttachmentRefresh] ENUM_Muzzle values: %s"), *EnumList);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[TMAttachmentRefresh] Failed to load ENUM_Muzzle."));
			return false;
		}

		const TCHAR* AttachmentBlueprintPaths[] =
		{
			TEXT("/Game/MP_System_V3/Game/Blueprints/Core/BP_Weapon_Master.BP_Weapon_Master"),
			TEXT("/Game/MP_System_V3/Game/Blueprints/Core/MainMenuPawn/BP_MenuViewer.BP_MenuViewer"),
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Attachments.W_Attachments"),
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Loadout.W_Loadout")
		};

		bool bSuccess = true;
		for (const TCHAR* BlueprintPath : AttachmentBlueprintPaths)
		{
			UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, BlueprintPath);
			if (!Blueprint)
			{
				UE_LOG(LogTemp, Error, TEXT("[TMAttachmentRefresh] Failed to load blueprint: %s"), BlueprintPath);
				bSuccess = false;
				continue;
			}

			UE_LOG(LogTemp, Display, TEXT("[TMAttachmentRefresh] Refreshing %s"), *Blueprint->GetPathName());
			FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMAttachmentRefresh] Blueprint status after compile: %d"),
				static_cast<int32>(Blueprint->Status));

			if (Blueprint->Status == BS_Error)
			{
				bSuccess = false;
				continue;
			}

			bSuccess &= TMSavePackageForAsset(Blueprint, TEXT("TMAttachmentRefresh"));
		}

		return bSuccess;
	}

	int32 TMFindEnumIndexByDisplayName(const UEnum* Enum, const TCHAR* DisplayName)
	{
		if (!Enum)
		{
			return INDEX_NONE;
		}

		for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
		{
			const FString NameString = Enum->GetNameStringByIndex(Index);
			const FString DisplayNameString = Enum->GetDisplayNameTextByIndex(Index).ToString();
			if (NameString.Equals(DisplayName, ESearchCase::IgnoreCase)
				|| DisplayNameString.Equals(DisplayName, ESearchCase::IgnoreCase))
			{
				return Index;
			}
		}

		return INDEX_NONE;
	}

	bool TMPropertyMatchesAuthoredName(const FProperty* Property, const TCHAR* ExpectedName)
	{
		if (!Property || !ExpectedName)
		{
			return false;
		}

		const FString Expected(ExpectedName);
		const FString PropertyName = Property->GetName();
		const FString AuthoredName = Property->GetAuthoredName();
		const FString DisplayName = Property->GetMetaData(TEXT("DisplayName"));

		return PropertyName.Equals(Expected, ESearchCase::IgnoreCase)
			|| PropertyName.StartsWith(Expected + TEXT("_"), ESearchCase::IgnoreCase)
			|| AuthoredName.Equals(Expected, ESearchCase::IgnoreCase)
			|| DisplayName.Equals(Expected, ESearchCase::IgnoreCase);
	}

	FProperty* TMFindStructPropertyByAuthoredName(UScriptStruct* Struct, const TCHAR* ExpectedName)
	{
		if (!Struct)
		{
			return nullptr;
		}

		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			FProperty* Property = *It;
			if (TMPropertyMatchesAuthoredName(Property, ExpectedName))
			{
				return Property;
			}
		}

		return nullptr;
	}

	bool TMExportStructPropertyText(FProperty* Property, const void* ContainerPtr, FString& OutText)
	{
		if (!Property || !ContainerPtr)
		{
			return false;
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ContainerPtr);
		Property->ExportTextItem_Direct(OutText, ValuePtr, nullptr, nullptr, PPF_None);
		return true;
	}

	bool TMSetEnumPropertyByDisplayName(FProperty* Property, void* ContainerPtr, const TCHAR* DisplayName)
	{
		if (!Property || !ContainerPtr || !DisplayName)
		{
			return false;
		}

		UEnum* Enum = nullptr;
		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			Enum = EnumProperty->GetEnum();
		}
		else if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			Enum = ByteProperty->Enum;
		}

		const int32 EnumIndex = TMFindEnumIndexByDisplayName(Enum, DisplayName);
		if (!Enum || EnumIndex == INDEX_NONE)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMLoadoutOffset] Failed to find enum value '%s' on property %s enum=%s."),
				DisplayName,
				*GetNameSafe(Property),
				*GetNameSafe(Enum));
			return false;
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ContainerPtr);
		const int64 EnumValue = Enum->GetValueByIndex(EnumIndex);
		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, EnumValue);
			return true;
		}

		if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			NumericProperty->SetIntPropertyValue(ValuePtr, EnumValue);
			return true;
		}

		return false;
	}

	bool TMParseDoubleParam(const FString& Params, const TCHAR* Key, double& OutValue)
	{
		FString ValueString;
		if (!FParse::Value(*Params, Key, ValueString))
		{
			return false;
		}

		OutValue = FCString::Atod(*ValueString);
		return true;
	}

	bool TMSetACWILoadoutViewOffset(const FString& Params)
	{
		double X = -12.5;
		double Y = -17.5;
		double Z = -5.0;
		TMParseDoubleParam(Params, TEXT("ACWILoadoutX="), X);
		TMParseDoubleParam(Params, TEXT("ACWILoadoutY="), Y);
		TMParseDoubleParam(Params, TEXT("ACWILoadoutZ="), Z);

		UDataTable* WeaponsTable = LoadObject<UDataTable>(
			nullptr,
			TEXT("/Game/MP_System_V3/Game/Blueprints/DataTables/DT_Weapons.DT_Weapons"));
		if (!WeaponsTable)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLoadoutOffset] Failed to load DT_Weapons."));
			return false;
		}

		UScriptStruct* RowStruct = const_cast<UScriptStruct*>(WeaponsTable->GetRowStruct());
		uint8* const* RowPtr = WeaponsTable->GetRowMap().Find(TEXT("ACWI"));
		if (!RowStruct || !RowPtr || !*RowPtr)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLoadoutOffset] Failed to find ACWI row in DT_Weapons."));
			return false;
		}

		FStructProperty* ParametersProperty =
			CastField<FStructProperty>(TMFindStructPropertyByAuthoredName(RowStruct, TEXT("Parameters")));
		if (!ParametersProperty)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLoadoutOffset] Failed to find Parameters field on %s."), *GetNameSafe(RowStruct));
			return false;
		}

		void* ParametersPtr = ParametersProperty->ContainerPtrToValuePtr<void>(*RowPtr);
		FStructProperty* ViewOffsetProperty =
			CastField<FStructProperty>(TMFindStructPropertyByAuthoredName(ParametersProperty->Struct, TEXT("ViewOffset")));
		FProperty* WeaponTypeProperty =
			TMFindStructPropertyByAuthoredName(ParametersProperty->Struct, TEXT("WeaponType"));
		if (!ViewOffsetProperty || ViewOffsetProperty->Struct != TBaseStructure<FVector>::Get())
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLoadoutOffset] Failed to find Parameters.ViewOffset FVector field."));
			return false;
		}
		if (!WeaponTypeProperty)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLoadoutOffset] Failed to find Parameters.WeaponType field."));
			return false;
		}

		FString OldParametersText;
		TMExportStructPropertyText(ParametersProperty, *RowPtr, OldParametersText);

		WeaponsTable->Modify();
		FVector* ViewOffset = ViewOffsetProperty->ContainerPtrToValuePtr<FVector>(ParametersPtr);
		*ViewOffset = FVector(X, Y, Z);

		if (!TMSetEnumPropertyByDisplayName(WeaponTypeProperty, ParametersPtr, TEXT("Assault Rifle")))
		{
			return false;
		}

		FString NewParametersText;
		TMExportStructPropertyText(ParametersProperty, *RowPtr, NewParametersText);

		WeaponsTable->HandleDataTableChanged(TEXT("ACWI"));
		WeaponsTable->MarkPackageDirty();

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMLoadoutOffset] ACWI Parameters: %s -> %s"),
			*OldParametersText,
			*NewParametersText);

		return TMSavePackageForAsset(WeaponsTable, TEXT("TMLoadoutOffset"));
	}

	bool TMEnsureAttachmentEnumHasSilencerco(UUserDefinedEnum*& OutEnum, int64& OutSilencercoValue)
	{
		OutEnum = LoadObject<UUserDefinedEnum>(
			nullptr,
			TEXT("/Game/MP_System_V3/Game/Blueprints/Enums/ENUM_Attachments.ENUM_Attachments"));
		if (!OutEnum)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMSilencerco] Failed to load ENUM_Attachments."));
			return false;
		}

		int32 SilencercoIndex = TMFindEnumIndexByDisplayName(OutEnum, TEXT("Silencerco"));
		if (SilencercoIndex == INDEX_NONE)
		{
			OutEnum->Modify();
			FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(OutEnum);
			SilencercoIndex = OutEnum->NumEnums() - 2;
			if (SilencercoIndex < 0
				|| !FEnumEditorUtils::SetEnumeratorDisplayName(OutEnum, SilencercoIndex, FText::FromString(TEXT("Silencerco"))))
			{
				UE_LOG(LogTemp, Error, TEXT("[TMSilencerco] Failed to add Silencerco to ENUM_Attachments."));
				return false;
			}

			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMSilencerco] Added Silencerco to ENUM_Attachments at index %d."),
				SilencercoIndex);
			TMSavePackageForAsset(OutEnum, TEXT("TMSilencerco"));
		}
		else
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMSilencerco] Silencerco already present in ENUM_Attachments at index %d."),
				SilencercoIndex);
		}

		OutSilencercoValue = OutEnum->GetValueByIndex(SilencercoIndex);
		FString EnumList;
		for (int32 Index = 0; Index < OutEnum->NumEnums(); ++Index)
		{
			EnumList += FString::Printf(
				TEXT("%d:%s/%s=%lld "),
				Index,
				*OutEnum->GetNameStringByIndex(Index),
				*OutEnum->GetDisplayNameTextByIndex(Index).ToString(),
				static_cast<long long>(OutEnum->GetValueByIndex(Index)));
		}
		UE_LOG(LogTemp, Display, TEXT("[TMSilencerco] ENUM_Attachments values: %s"), *EnumList);
		return true;
	}

	FArrayProperty* TMFindArrayPropertyByName(UClass* Class, const TCHAR* ExpectedName)
	{
		if (!Class)
		{
			return nullptr;
		}

		for (TFieldIterator<FArrayProperty> It(Class); It; ++It)
		{
			FArrayProperty* Property = *It;
			if (!Property)
			{
				continue;
			}

			const FString PropertyName = Property->GetName();
			if (PropertyName.Equals(ExpectedName, ESearchCase::IgnoreCase)
				|| PropertyName.StartsWith(FString(ExpectedName) + TEXT("_"), ESearchCase::IgnoreCase)
				|| Property->GetAuthoredName().Equals(ExpectedName, ESearchCase::IgnoreCase))
			{
				return Property;
			}
		}

		return nullptr;
	}

	bool TMReadEnumArrayValue(const FProperty* InnerProperty, const void* ElementPtr, int64& OutValue)
	{
		if (!InnerProperty || !ElementPtr)
		{
			return false;
		}

		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(InnerProperty))
		{
			OutValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ElementPtr);
			return true;
		}

		if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(InnerProperty))
		{
			OutValue = NumericProperty->GetSignedIntPropertyValue(ElementPtr);
			return true;
		}

		return false;
	}

	bool TMWriteEnumArrayValue(FProperty* InnerProperty, void* ElementPtr, const int64 Value)
	{
		if (!InnerProperty || !ElementPtr)
		{
			return false;
		}

		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(InnerProperty))
		{
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(ElementPtr, Value);
			return true;
		}

		if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(InnerProperty))
		{
			NumericProperty->SetIntPropertyValue(ElementPtr, Value);
			return true;
		}

		return false;
	}

	bool TMAddEnumValueToCompatibleAttachment(UBlueprint* Blueprint, UEnum* Enum, const int64 EnumValue)
	{
		if (!Blueprint || !Blueprint->GeneratedClass || !Enum)
		{
			return false;
		}

		UObject* DefaultObject = Blueprint->GeneratedClass->GetDefaultObject();
		FArrayProperty* CompatibleAttachmentProperty =
			TMFindArrayPropertyByName(DefaultObject ? DefaultObject->GetClass() : nullptr, TEXT("CompatibleAttachment"));
		if (!DefaultObject || !CompatibleAttachmentProperty)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMSilencerco] CompatibleAttachment not found on %s."), *GetNameSafe(Blueprint));
			return false;
		}

		FScriptArrayHelper ArrayHelper(CompatibleAttachmentProperty, CompatibleAttachmentProperty->ContainerPtrToValuePtr<void>(DefaultObject));
		FString BeforeValues;
		for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
		{
			int64 ExistingValue = 0;
			if (TMReadEnumArrayValue(CompatibleAttachmentProperty->Inner, ArrayHelper.GetRawPtr(Index), ExistingValue))
			{
				BeforeValues += FString::Printf(
					TEXT("%lld/%s "),
					static_cast<long long>(ExistingValue),
					*Enum->GetDisplayNameTextByValue(ExistingValue).ToString());
				if (ExistingValue == EnumValue)
				{
					UE_LOG(
						LogTemp,
						Display,
						TEXT("[TMSilencerco] %s already has Silencerco in CompatibleAttachment. Values: %s"),
						*Blueprint->GetPathName(),
						*BeforeValues);
					return true;
				}
			}
		}

		Blueprint->Modify();
		DefaultObject->Modify();

		const int32 NewIndex = ArrayHelper.AddValue();
		if (!TMWriteEnumArrayValue(CompatibleAttachmentProperty->Inner, ArrayHelper.GetRawPtr(NewIndex), EnumValue))
		{
			UE_LOG(LogTemp, Error, TEXT("[TMSilencerco] Failed to write Silencerco enum value on %s."), *Blueprint->GetPathName());
			return false;
		}

		FString AfterValues;
		for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
		{
			int64 ExistingValue = 0;
			if (TMReadEnumArrayValue(CompatibleAttachmentProperty->Inner, ArrayHelper.GetRawPtr(Index), ExistingValue))
			{
				AfterValues += FString::Printf(
					TEXT("%lld/%s "),
					static_cast<long long>(ExistingValue),
					*Enum->GetDisplayNameTextByValue(ExistingValue).ToString());
			}
		}

		FPropertyChangedEvent PropertyChangedEvent(CompatibleAttachmentProperty);
		DefaultObject->PostEditChangeProperty(PropertyChangedEvent);
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMSilencerco] Added Silencerco to %s CompatibleAttachment. Before: %s After: %s"),
			*Blueprint->GetPathName(),
			*BeforeValues,
			*AfterValues);
		return Blueprint->Status != BS_Error && TMSavePackageForAsset(Blueprint, TEXT("TMSilencerco"));
	}

	FString TMDescribeCompatibleAttachmentValues(FArrayProperty* ArrayProperty, FScriptArrayHelper& ArrayHelper, const UEnum* Enum)
	{
		FString Values;
		if (!ArrayProperty || !Enum)
		{
			return Values;
		}

		for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
		{
			int64 ExistingValue = 0;
			if (TMReadEnumArrayValue(ArrayProperty->Inner, ArrayHelper.GetRawPtr(Index), ExistingValue))
			{
				Values += FString::Printf(
					TEXT("%lld/%s "),
					static_cast<long long>(ExistingValue),
					*Enum->GetDisplayNameTextByValue(ExistingValue).ToString());
			}
		}

		return Values;
	}

	bool TMShouldKeepDEMuzzleAttachment(const FString& AttachmentName)
	{
		return AttachmentName.Equals(TEXT("Silencerco"), ESearchCase::IgnoreCase)
			|| AttachmentName.Equals(TEXT("Empty"), ESearchCase::IgnoreCase);
	}

	bool TMCollectDEMuzzleValuesToRemove(const UEnum* AttachmentEnum, const int64 SilencercoValue, TSet<int64>& OutValuesToRemove)
	{
		if (!AttachmentEnum)
		{
			return false;
		}

		UDataTable* MuzzleTable = LoadObject<UDataTable>(
			nullptr,
			TEXT("/Game/MP_System_V3/Game/Blueprints/DataTables/DT_Muzzle.DT_Muzzle"));
		if (!MuzzleTable)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMDEMuzzle] Failed to load DT_Muzzle."));
			return false;
		}

		FString MuzzleRows;
		for (const FName RowName : MuzzleTable->GetRowNames())
		{
			const FString RowNameString = RowName.ToString();
			MuzzleRows += RowNameString + TEXT(" ");

			if (TMShouldKeepDEMuzzleAttachment(RowNameString))
			{
				continue;
			}

			const int32 AttachmentIndex = TMFindEnumIndexByDisplayName(AttachmentEnum, *RowNameString);
			if (AttachmentIndex == INDEX_NONE)
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[TMDEMuzzle] DT_Muzzle row has no matching ENUM_Attachments value: %s"),
					*RowNameString);
				continue;
			}

			const int64 AttachmentValue = AttachmentEnum->GetValueByIndex(AttachmentIndex);
			if (AttachmentValue != SilencercoValue)
			{
				OutValuesToRemove.Add(AttachmentValue);
			}
		}

		UE_LOG(LogTemp, Display, TEXT("[TMDEMuzzle] DT_Muzzle rows: %s"), *MuzzleRows);
		return true;
	}

	bool TMPatchDESilencercoOnly()
	{
		UUserDefinedEnum* AttachmentEnum = nullptr;
		int64 SilencercoValue = 0;
		if (!TMEnsureAttachmentEnumHasSilencerco(AttachmentEnum, SilencercoValue))
		{
			return false;
		}

		TSet<int64> MuzzleValuesToRemove;
		if (!TMCollectDEMuzzleValuesToRemove(AttachmentEnum, SilencercoValue, MuzzleValuesToRemove))
		{
			return false;
		}

		UBlueprint* Blueprint = LoadObject<UBlueprint>(
			nullptr,
			TEXT("/Game/MP_System_V3/Game/Weapons/Secondary/DE/BP_DE.BP_DE"));
		if (!Blueprint || !Blueprint->GeneratedClass)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMDEMuzzle] Failed to load BP_DE."));
			return false;
		}

		UObject* DefaultObject = Blueprint->GeneratedClass->GetDefaultObject();
		FArrayProperty* CompatibleAttachmentProperty =
			TMFindArrayPropertyByName(DefaultObject ? DefaultObject->GetClass() : nullptr, TEXT("CompatibleAttachment"));
		if (!DefaultObject || !CompatibleAttachmentProperty)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMDEMuzzle] CompatibleAttachment not found on BP_DE."));
			return false;
		}

		FScriptArrayHelper ArrayHelper(
			CompatibleAttachmentProperty,
			CompatibleAttachmentProperty->ContainerPtrToValuePtr<void>(DefaultObject));
		const FString BeforeValues = TMDescribeCompatibleAttachmentValues(CompatibleAttachmentProperty, ArrayHelper, AttachmentEnum);
		bool bChanged = false;
		bool bHasSilencerco = false;

		for (int32 Index = ArrayHelper.Num() - 1; Index >= 0; --Index)
		{
			int64 ExistingValue = 0;
			if (!TMReadEnumArrayValue(CompatibleAttachmentProperty->Inner, ArrayHelper.GetRawPtr(Index), ExistingValue))
			{
				continue;
			}

			if (ExistingValue == SilencercoValue)
			{
				if (bHasSilencerco)
				{
					ArrayHelper.RemoveValues(Index);
					bChanged = true;
				}
				else
				{
					bHasSilencerco = true;
				}
				continue;
			}

			if (MuzzleValuesToRemove.Contains(ExistingValue))
			{
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMDEMuzzle] Removing muzzle attachment from BP_DE: %lld/%s"),
					static_cast<long long>(ExistingValue),
					*AttachmentEnum->GetDisplayNameTextByValue(ExistingValue).ToString());
				ArrayHelper.RemoveValues(Index);
				bChanged = true;
			}
		}

		if (!bHasSilencerco)
		{
			const int32 NewIndex = ArrayHelper.AddValue();
			if (!TMWriteEnumArrayValue(CompatibleAttachmentProperty->Inner, ArrayHelper.GetRawPtr(NewIndex), SilencercoValue))
			{
				UE_LOG(LogTemp, Error, TEXT("[TMDEMuzzle] Failed to write Silencerco enum value on BP_DE."));
				return false;
			}
			bChanged = true;
		}

		const FString AfterValues = TMDescribeCompatibleAttachmentValues(CompatibleAttachmentProperty, ArrayHelper, AttachmentEnum);
		UE_LOG(LogTemp, Display, TEXT("[TMDEMuzzle] BP_DE CompatibleAttachment before: %s"), *BeforeValues);
		UE_LOG(LogTemp, Display, TEXT("[TMDEMuzzle] BP_DE CompatibleAttachment after: %s"), *AfterValues);

		bool bSuccess = true;
		if (bChanged)
		{
			Blueprint->Modify();
			DefaultObject->Modify();
			FPropertyChangedEvent PropertyChangedEvent(CompatibleAttachmentProperty);
			DefaultObject->PostEditChangeProperty(PropertyChangedEvent);
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			bSuccess &= Blueprint->Status != BS_Error && TMSavePackageForAsset(Blueprint, TEXT("TMDEMuzzle"));
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("[TMDEMuzzle] BP_DE already has only Silencerco muzzle compatibility."));
		}

		bSuccess &= TMRefreshCompileAndSaveBlueprint(
			TEXT("/Game/MP_System_V3/Game/Blueprints/Core/BP_Weapon_Master.BP_Weapon_Master"),
			TEXT("TMDEMuzzle"));
		bSuccess &= TMRefreshCompileAndSaveBlueprint(
			TEXT("/Game/MP_System_V3/Game/Blueprints/Core/MainMenuPawn/BP_MenuViewer.BP_MenuViewer"),
			TEXT("TMDEMuzzle"));
		bSuccess &= TMRefreshCompileAndSaveBlueprint(
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Attachments.W_Attachments"),
			TEXT("TMDEMuzzle"));
		bSuccess &= TMRefreshCompileAndSaveBlueprint(
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Loadout.W_Loadout"),
			TEXT("TMDEMuzzle"));

		return bSuccess;
	}

	bool TMPatchSilencercoM4TarCompatibility()
	{
		UUserDefinedEnum* AttachmentEnum = nullptr;
		int64 SilencercoValue = 0;
		if (!TMEnsureAttachmentEnumHasSilencerco(AttachmentEnum, SilencercoValue))
		{
			return false;
		}

		const TCHAR* WeaponBlueprintPaths[] =
		{
			TEXT("/Game/MP_System_V3/Game/Weapons/Primary/M4/BP_M4.BP_M4"),
			TEXT("/Game/MP_System_V3/Game/Weapons/Primary/TAR/BP_TAR.BP_TAR")
		};

		bool bSuccess = true;
		for (const TCHAR* BlueprintPath : WeaponBlueprintPaths)
		{
			UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, BlueprintPath);
			if (!Blueprint)
			{
				UE_LOG(LogTemp, Error, TEXT("[TMSilencerco] Failed to load blueprint: %s"), BlueprintPath);
				bSuccess = false;
				continue;
			}

			bSuccess &= TMAddEnumValueToCompatibleAttachment(Blueprint, AttachmentEnum, SilencercoValue);
		}

		bSuccess &= TMRefreshCompileAndSaveBlueprint(
			TEXT("/Game/MP_System_V3/Game/Blueprints/Core/BP_Weapon_Master.BP_Weapon_Master"),
			TEXT("TMSilencerco"));
		bSuccess &= TMRefreshCompileAndSaveBlueprint(
			TEXT("/Game/MP_System_V3/Game/Blueprints/Core/MainMenuPawn/BP_MenuViewer.BP_MenuViewer"),
			TEXT("TMSilencerco"));
		bSuccess &= TMRefreshCompileAndSaveBlueprint(
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Attachments.W_Attachments"),
			TEXT("TMSilencerco"));
		bSuccess &= TMRefreshCompileAndSaveBlueprint(
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Loadout.W_Loadout"),
			TEXT("TMSilencerco"));

		return bSuccess;
	}

	FLinearColor TMGetDirtyFolderYellow()
	{
		return FLinearColor::FromSRGBColor(FColor(214, 166, 44, 255));
	}

	FString TMFormatLinearColorDefault(const FLinearColor& Color)
	{
		return FString::Printf(
			TEXT("(R=%.6f,G=%.6f,B=%.6f,A=%.6f)"),
			Color.R,
			Color.G,
			Color.B,
			Color.A);
	}

	FString TMFormatSlateColorDefault(const FLinearColor& Color)
	{
		return FString::Printf(
			TEXT("(SpecifiedColor=%s,ColorUseRule=UseColor_Specified)"),
			*TMFormatLinearColorDefault(Color));
	}

	bool TMSetLinkedPinDefault(UEdGraphPin* Pin, const FString& DefaultValue)
	{
		if (!Pin)
		{
			return false;
		}

		const bool bHadLinks = Pin->LinkedTo.Num() > 0;
		const bool bChangedDefault = Pin->DefaultValue != DefaultValue;
		if (!bHadLinks && !bChangedDefault)
		{
			return false;
		}

		Pin->Modify();
		if (bHadLinks)
		{
			Pin->BreakAllPinLinks(false);
		}

		bool bSetBySchema = false;
		if (const UEdGraphNode* Node = Pin->GetOwningNode())
		{
			if (const UEdGraph* Graph = Node->GetGraph())
			{
				if (const UEdGraphSchema* Schema = Graph->GetSchema())
				{
					Schema->TrySetDefaultValue(*Pin, DefaultValue);
					bSetBySchema = true;
				}
			}
		}

		if (!bSetBySchema)
		{
			Pin->DefaultValue = DefaultValue;
		}

		return true;
	}

	bool TMSetGraphSlateColorPinYellow(UEdGraphPin* SlateColorPin, const FLinearColor& Yellow)
	{
		if (!SlateColorPin)
		{
			return false;
		}

		bool bChanged = false;
		const FString LinearDefault = TMFormatLinearColorDefault(Yellow);
		const FString SlateDefault = TMFormatSlateColorDefault(Yellow);

		UEdGraphPin* SpecifiedColorPin = nullptr;
		UEdGraphPin* ColorUseRulePin = nullptr;
		for (UEdGraphPin* SubPin : SlateColorPin->SubPins)
		{
			if (!SubPin)
			{
				continue;
			}

			const FString SubPinName = SubPin->PinName.ToString();
			if (SubPinName.Contains(TEXT("SpecifiedColor"), ESearchCase::IgnoreCase))
			{
				SpecifiedColorPin = SubPin;
			}
			else if (SubPinName.Contains(TEXT("ColorUseRule"), ESearchCase::IgnoreCase))
			{
				ColorUseRulePin = SubPin;
			}
		}

		if (!SpecifiedColorPin)
		{
			SpecifiedColorPin = TMFindPinByName(
				SlateColorPin->GetOwningNode(),
				*FString::Printf(TEXT("%s_SpecifiedColor"), *SlateColorPin->PinName.ToString()),
				EGPD_Input);
		}

		if (!ColorUseRulePin)
		{
			ColorUseRulePin = TMFindPinByName(
				SlateColorPin->GetOwningNode(),
				*FString::Printf(TEXT("%s_ColorUseRule"), *SlateColorPin->PinName.ToString()),
				EGPD_Input);
		}

		if (SpecifiedColorPin)
		{
			bChanged |= TMSetLinkedPinDefault(SpecifiedColorPin, LinearDefault);
			if (SlateColorPin->LinkedTo.Num() > 0)
			{
				SlateColorPin->Modify();
				SlateColorPin->BreakAllPinLinks(false);
				bChanged = true;
			}
		}
		else
		{
			bChanged |= TMSetLinkedPinDefault(SlateColorPin, SlateDefault);
		}

		if (ColorUseRulePin)
		{
			bChanged |= TMSetLinkedPinDefault(ColorUseRulePin, TEXT("UseColor_Specified"));
		}

		return bChanged;
	}

	bool TMSetButtonForegroundStyleYellow(UButton* Button, const FSlateColor& SlateYellow)
	{
		if (!Button)
		{
			return false;
		}

		FButtonStyle Style = Button->GetStyle();
		Style
			.SetNormalForeground(SlateYellow)
			.SetHoveredForeground(SlateYellow)
			.SetPressedForeground(SlateYellow)
			.SetDisabledForeground(SlateYellow);
		Button->SetStyle(Style);
		return true;
	}

	bool TMPatchYellowWidgetGraphColors(UBlueprint* Blueprint, int32& OutTextGraphCount, int32& OutButtonForegroundGraphCount)
	{
		if (!Blueprint)
		{
			return false;
		}

		const FLinearColor Yellow = TMGetDirtyFolderYellow();
		bool bChanged = false;

		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);

		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node);
				if (!CallFunctionNode)
				{
					continue;
				}

				const FName FunctionName = CallFunctionNode->FunctionReference.GetMemberName();
				UFunction* TargetFunction = CallFunctionNode->GetTargetFunction();
				UClass* OwnerClass = TargetFunction ? TargetFunction->GetOwnerClass() : nullptr;

				if (FunctionName == TEXT("SetColorAndOpacity")
					&& OwnerClass
					&& OwnerClass->IsChildOf(UTextBlock::StaticClass()))
				{
					if (UEdGraphPin* ColorPin = TMFindPinByName(CallFunctionNode, TEXT("InColorAndOpacity"), EGPD_Input))
					{
						if (TMSetGraphSlateColorPinYellow(ColorPin, Yellow))
						{
							++OutTextGraphCount;
							bChanged = true;
							UE_LOG(
								LogTemp,
								Display,
								TEXT("[TMYellowUI] Patched TextBlock graph color: %s Graph=%s Node=%s"),
								*Blueprint->GetPathName(),
								*Graph->GetName(),
								*Node->GetName());
						}
					}
				}

				if (FunctionName == TEXT("SetStyle")
					&& OwnerClass
					&& OwnerClass->IsChildOf(UButton::StaticClass()))
				{
					for (UEdGraphPin* Pin : Node->Pins)
					{
						if (!Pin || Pin->Direction != EGPD_Input)
						{
							continue;
						}

						const FString PinName = Pin->PinName.ToString();
						if (!PinName.Contains(TEXT("Foreground"), ESearchCase::IgnoreCase))
						{
							continue;
						}

						const UObject* SubCategoryObject = Pin->PinType.PinSubCategoryObject.Get();
						if (!GetNameSafe(SubCategoryObject).Contains(TEXT("SlateColor"), ESearchCase::IgnoreCase))
						{
							continue;
						}

						if (TMSetGraphSlateColorPinYellow(Pin, Yellow))
						{
							++OutButtonForegroundGraphCount;
							bChanged = true;
							UE_LOG(
								LogTemp,
								Display,
								TEXT("[TMYellowUI] Patched Button foreground graph color: %s Graph=%s Node=%s Pin=%s"),
								*Blueprint->GetPathName(),
								*Graph->GetName(),
								*Node->GetName(),
								*PinName);
						}
					}
				}
			}
		}

		return bChanged;
	}

	bool TMContainsToken(const FString& Text, const TCHAR* Token)
	{
		return Text.Contains(Token, ESearchCase::IgnoreCase, ESearchDir::FromStart);
	}

	UWidgetTree* TMFindWidgetTree(UBlueprint* Blueprint)
	{
		if (!Blueprint)
		{
			return nullptr;
		}

		FProperty* WidgetTreeProperty = Blueprint->GetClass()->FindPropertyByName(FName(TEXT("WidgetTree")));
		FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(WidgetTreeProperty);
		if (!ObjectProperty)
		{
			return nullptr;
		}

		return Cast<UWidgetTree>(ObjectProperty->GetObjectPropertyValue_InContainer(Blueprint));
	}

	FString TMFormatMarginForMenuDump(const FMargin& Margin)
	{
		return FString::Printf(
			TEXT("L=%.2f T=%.2f R=%.2f B=%.2f"),
			Margin.Left,
			Margin.Top,
			Margin.Right,
			Margin.Bottom);
	}

	FString TMFormatAnchorsForMenuDump(const FAnchors& Anchors)
	{
		return FString::Printf(
			TEXT("Min=%s Max=%s"),
			*Anchors.Minimum.ToString(),
			*Anchors.Maximum.ToString());
	}

	FString TMDescribeSlateBrushForMenuDump(const FSlateBrush& Brush)
	{
		return FString::Printf(
			TEXT("DrawAs=%d Tint=%s Size=%s Margin=%s Resource=%s"),
			static_cast<int32>(Brush.DrawAs),
			*Brush.TintColor.GetSpecifiedColor().ToString(),
			*Brush.ImageSize.ToString(),
			*TMFormatMarginForMenuDump(Brush.Margin),
			*GetPathNameSafe(Brush.GetResourceObject()));
	}

	FString TMDescribeWidgetSlotForMenuDump(const UWidget* Widget)
	{
		if (!Widget || !Widget->Slot)
		{
			return TEXT("Slot=None");
		}

		FString Result = FString::Printf(TEXT("Slot=%s"), *Widget->Slot->GetClass()->GetName());
		if (const UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot))
		{
			Result += FString::Printf(
				TEXT(" Pos=%s Size=%s Anchors=%s Align=%s Auto=%d Z=%d"),
				*CanvasSlot->GetPosition().ToString(),
				*CanvasSlot->GetSize().ToString(),
				*TMFormatAnchorsForMenuDump(CanvasSlot->GetAnchors()),
				*CanvasSlot->GetAlignment().ToString(),
				CanvasSlot->GetAutoSize() ? 1 : 0,
				CanvasSlot->GetZOrder());
		}

		return Result;
	}

	bool TMDumpMenuButtonWidgets()
	{
		const TCHAR* WidgetBlueprintPaths[] =
		{
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_MainMenu.W_MainMenu"),
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_InGameMenu.W_InGameMenu"),
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Settings.W_Settings"),
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Loadout.W_Loadout"),
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Attachments.W_Attachments"),
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Weapon_Layer.W_Weapon_Layer"),
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Attachment_Layer.W_Attachment_Layer"),
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_LevelSelect.W_LevelSelect"),
		};

		bool bSuccess = true;
		for (const TCHAR* BlueprintPath : WidgetBlueprintPaths)
		{
			UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, BlueprintPath);
			UWidgetTree* WidgetTree = TMFindWidgetTree(Blueprint);
			if (!Blueprint || !WidgetTree)
			{
				UE_LOG(
					LogTemp,
					Error,
					TEXT("[TMMenuButtonDump] Failed to load widget tree. BP=%s Loaded=%d Tree=%d"),
					BlueprintPath,
					Blueprint ? 1 : 0,
					WidgetTree ? 1 : 0);
				bSuccess = false;
				continue;
			}

			int32 ButtonCount = 0;
			int32 RelevantWidgetCount = 0;
			UE_LOG(LogTemp, Display, TEXT("[TMMenuButtonDump] BP=%s Root=%s"), *Blueprint->GetPathName(), *GetNameSafe(WidgetTree->RootWidget));
			WidgetTree->ForEachWidget([&](UWidget* Widget)
			{
				if (!Widget)
				{
					return;
				}

				const FString WidgetName = Widget->GetName();
				const FString WidgetClassName = Widget->GetClass()->GetName();
				const bool bRelevantByName =
					WidgetName.Contains(TEXT("Button"), ESearchCase::IgnoreCase)
					|| WidgetName.Contains(TEXT("Menu"), ESearchCase::IgnoreCase)
					|| WidgetName.Contains(TEXT("Loadout"), ESearchCase::IgnoreCase)
					|| WidgetName.Contains(TEXT("Settings"), ESearchCase::IgnoreCase)
					|| WidgetName.Contains(TEXT("Apply"), ESearchCase::IgnoreCase)
					|| WidgetName.Contains(TEXT("Back"), ESearchCase::IgnoreCase)
					|| WidgetName.Contains(TEXT("Return"), ESearchCase::IgnoreCase)
					|| WidgetName.Contains(TEXT("Quit"), ESearchCase::IgnoreCase)
					|| WidgetName.Contains(TEXT("Play"), ESearchCase::IgnoreCase)
					|| WidgetName.Contains(TEXT("Line"), ESearchCase::IgnoreCase)
					|| WidgetName.Contains(TEXT("Strip"), ESearchCase::IgnoreCase)
					|| WidgetName.Contains(TEXT("Arrow"), ESearchCase::IgnoreCase)
					|| WidgetName.Contains(TEXT("Greater"), ESearchCase::IgnoreCase)
					|| WidgetName.Contains(TEXT("Focus"), ESearchCase::IgnoreCase);
				const bool bRelevantByClass =
					Widget->IsA<UButton>()
					|| Widget->IsA<UTextBlock>()
					|| Widget->IsA<UImage>()
					|| Widget->IsA<UBorder>()
					|| Widget->IsA<USizeBox>();
				if (!bRelevantByName && !bRelevantByClass)
				{
					return;
				}

				++RelevantWidgetCount;
				UPanelWidget* Parent = Widget->GetParent();
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMMenuButtonDump]   Widget=%s Class=%s Parent=%s Visibility=%d RenderOpacity=%.3f %s"),
					*WidgetName,
					*WidgetClassName,
					*GetNameSafe(Parent),
					static_cast<int32>(Widget->GetVisibility()),
					Widget->GetRenderOpacity(),
					*TMDescribeWidgetSlotForMenuDump(Widget));

				if (UButton* Button = Cast<UButton>(Widget))
				{
					++ButtonCount;
					const FButtonStyle& Style = Button->GetStyle();
					UE_LOG(LogTemp, Display, TEXT("[TMMenuButtonDump]     Button Normal{%s}"), *TMDescribeSlateBrushForMenuDump(Style.Normal));
					UE_LOG(LogTemp, Display, TEXT("[TMMenuButtonDump]     Button Hovered{%s}"), *TMDescribeSlateBrushForMenuDump(Style.Hovered));
					UE_LOG(LogTemp, Display, TEXT("[TMMenuButtonDump]     Button Pressed{%s}"), *TMDescribeSlateBrushForMenuDump(Style.Pressed));
				}
				else if (UImage* Image = Cast<UImage>(Widget))
				{
					UE_LOG(LogTemp, Display, TEXT("[TMMenuButtonDump]     Image Brush{%s} Color=%s"), *TMDescribeSlateBrushForMenuDump(Image->GetBrush()), *Image->GetColorAndOpacity().ToString());
				}
				else if (UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
				{
					UE_LOG(LogTemp, Display, TEXT("[TMMenuButtonDump]     Text='%s' Color=%s"), *TextBlock->GetText().ToString(), *TextBlock->GetColorAndOpacity().GetSpecifiedColor().ToString());
				}
				else if (UBorder* Border = Cast<UBorder>(Widget))
				{
					UE_LOG(LogTemp, Display, TEXT("[TMMenuButtonDump]     Border Brush{%s} Color=%s"), *TMDescribeSlateBrushForMenuDump(Border->Background), *Border->GetBrushColor().ToString());
				}
			});

			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMMenuButtonDump] Summary BP=%s Buttons=%d RelevantWidgets=%d"),
				*Blueprint->GetPathName(),
				ButtonCount,
				RelevantWidgetCount);
		}

		return bSuccess;
	}

	FSlateSound TMMakeSlateSound(UObject* ResourceObject)
	{
		FSlateSound Sound;
		Sound.SetResourceObject(ResourceObject);
		return Sound;
	}

	FString TMDescribeSlateSound(const FSlateSound& Sound)
	{
		return GetPathNameSafe(Sound.GetResourceObject());
	}

	FString TMExportSlateSoundDefault(UObject* ResourceObject)
	{
		FSlateSound Sound = TMMakeSlateSound(ResourceObject);
		FString DefaultValue;
		FSlateSound::StaticStruct()->ExportText(
			DefaultValue,
			&Sound,
			nullptr,
			nullptr,
			PPF_SerializedAsImportText,
			nullptr);
		return DefaultValue;
	}

	FString TMGetPinHierarchyName(const UEdGraphPin* Pin)
	{
		TArray<FString> Names;
		for (const UEdGraphPin* CurrentPin = Pin; CurrentPin; CurrentPin = CurrentPin->ParentPin)
		{
			FString Name = CurrentPin->PinName.ToString();
			const FString FriendlyName = CurrentPin->PinFriendlyName.ToString();
			if (!FriendlyName.IsEmpty() && !FriendlyName.Equals(Name, ESearchCase::IgnoreCase))
			{
				Name += TEXT("|");
				Name += FriendlyName;
			}

			Names.Insert(Name, 0);
		}

		return FString::Join(Names, TEXT("."));
	}

	bool TMIsGraphPinDefaultObject(UEdGraphPin* Pin)
	{
		return Pin
			&& (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object
				|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject);
	}

	bool TMIsGraphPinSlateSoundStruct(const UEdGraphPin* Pin)
	{
		return Pin
			&& Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct
			&& Pin->PinType.PinSubCategoryObject.Get() == FSlateSound::StaticStruct()
			&& Pin->SubPins.Num() == 0;
	}

	bool TMSetGraphObjectPinDefault(UEdGraphPin* Pin, UObject* ResourceObject)
	{
		if (!Pin)
		{
			return false;
		}

		const bool bHadLinks = Pin->LinkedTo.Num() > 0;
		const bool bChangedDefault =
			Pin->DefaultObject != ResourceObject
			|| !Pin->DefaultValue.IsEmpty()
			|| !Pin->AutogeneratedDefaultValue.IsEmpty()
			|| !Pin->DefaultTextValue.IsEmpty();
		if (!bHadLinks && !bChangedDefault)
		{
			return false;
		}

		Pin->Modify();
		if (bHadLinks)
		{
			Pin->BreakAllPinLinks(false);
		}

		if (const UEdGraphSchema* Schema = Pin->GetSchema())
		{
			Schema->TrySetDefaultObject(*Pin, ResourceObject);
		}
		else
		{
			Pin->DefaultObject = ResourceObject;
		}

		Pin->DefaultValue.Reset();
		Pin->AutogeneratedDefaultValue.Reset();
		Pin->DefaultTextValue = FText::GetEmpty();
		return true;
	}

	bool TMSetGraphSlateSoundPinDefault(UEdGraphPin* Pin, UObject* ResourceObject)
	{
		if (!Pin)
		{
			return false;
		}

		if (TMIsGraphPinDefaultObject(Pin))
		{
			return TMSetGraphObjectPinDefault(Pin, ResourceObject);
		}

		if (TMIsGraphPinSlateSoundStruct(Pin))
		{
			return TMSetLinkedPinDefault(Pin, TMExportSlateSoundDefault(ResourceObject));
		}

		return false;
	}

	bool TMPinLooksLikeSlateSoundResource(const UEdGraphPin* Pin, const FString& PinPath)
	{
		if (!Pin)
		{
			return false;
		}

		if (PinPath.Contains(TEXT("ResourceObject"), ESearchCase::IgnoreCase)
			|| PinPath.Contains(TEXT("Resource Object"), ESearchCase::IgnoreCase))
		{
			return true;
		}

		return Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object
			|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject
			|| TMIsGraphPinSlateSoundStruct(Pin);
	}

	bool TMNodeCarriesButtonStyle(UEdGraphNode* Node)
	{
		if (!Node)
		{
			return false;
		}

		if (UK2Node_MakeStruct* MakeStructNode = Cast<UK2Node_MakeStruct>(Node))
		{
			return MakeStructNode->StructType == FButtonStyle::StaticStruct();
		}

		if (UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node))
		{
			UFunction* TargetFunction = CallFunctionNode->GetTargetFunction();
			UClass* OwnerClass = TargetFunction ? TargetFunction->GetOwnerClass() : nullptr;
			return CallFunctionNode->FunctionReference.GetMemberName() == TEXT("SetStyle")
				&& OwnerClass
				&& OwnerClass->IsChildOf(UButton::StaticClass());
		}

		return false;
	}

	bool TMPatchUIButtonSoundGraphPins(
		UBlueprint* Blueprint,
		UObject* ButtonSound,
		int32& OutPressedGraphPins,
		int32& OutHoveredGraphPins,
		int32& OutClickedGraphPins)
	{
		if (!Blueprint || !ButtonSound)
		{
			return false;
		}

		bool bChanged = false;

		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);

		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!TMNodeCarriesButtonStyle(Node))
				{
					continue;
				}

				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (!Pin || Pin->Direction != EGPD_Input)
					{
						continue;
					}

					const FString PinPath = TMGetPinHierarchyName(Pin);
					if (PinPath.Contains(TEXT("PressedSlateSound"), ESearchCase::IgnoreCase)
						&& TMPinLooksLikeSlateSoundResource(Pin, PinPath)
						&& TMSetGraphSlateSoundPinDefault(Pin, ButtonSound))
					{
						++OutPressedGraphPins;
						bChanged = true;
						UE_LOG(
							LogTemp,
							Display,
							TEXT("[TMUIButtonSounds] Patched graph pressed sound: %s Graph=%s Node=%s Pin=%s"),
							*Blueprint->GetPathName(),
							*Graph->GetName(),
							*Node->GetName(),
							*PinPath);
					}

					if (PinPath.Contains(TEXT("HoveredSlateSound"), ESearchCase::IgnoreCase)
						&& TMPinLooksLikeSlateSoundResource(Pin, PinPath)
						&& TMSetGraphSlateSoundPinDefault(Pin, ButtonSound))
					{
						++OutHoveredGraphPins;
						bChanged = true;
						UE_LOG(
							LogTemp,
							Display,
							TEXT("[TMUIButtonSounds] Patched graph hovered sound: %s Graph=%s Node=%s Pin=%s"),
							*Blueprint->GetPathName(),
							*Graph->GetName(),
							*Node->GetName(),
							*PinPath);
					}

					if (PinPath.Contains(TEXT("ClickedSlateSound"), ESearchCase::IgnoreCase)
						&& TMPinLooksLikeSlateSoundResource(Pin, PinPath)
						&& TMSetGraphSlateSoundPinDefault(Pin, nullptr))
					{
						++OutClickedGraphPins;
						bChanged = true;
						UE_LOG(
							LogTemp,
							Display,
							TEXT("[TMUIButtonSounds] Cleared graph clicked sound: %s Graph=%s Node=%s Pin=%s"),
							*Blueprint->GetPathName(),
							*Graph->GetName(),
							*Node->GetName(),
							*PinPath);
					}
				}
			}
		}

		return bChanged;
	}

	bool TMPatchUIButtonStyleSound(
		UButton* Button,
		UObject* ButtonSound,
		int32& OutPressedButtons,
		int32& OutHoveredButtons,
		int32& OutClearedClickedButtons,
		int32& OutExistingClickButtons)
	{
		if (!Button || !ButtonSound)
		{
			return false;
		}

		FButtonStyle Style = Button->GetStyle();
		const FString PreviousPressedSound = TMDescribeSlateSound(Style.PressedSlateSound);
		const FString PreviousHoveredSound = TMDescribeSlateSound(Style.HoveredSlateSound);
		const FString PreviousClickedSound = TMDescribeSlateSound(Style.ClickedSlateSound);

		bool bChanged = false;
		if (Style.PressedSlateSound.GetResourceObject() != ButtonSound)
		{
			Style.SetPressedSound(TMMakeSlateSound(ButtonSound));
			++OutPressedButtons;
			bChanged = true;
		}

		if (Style.HoveredSlateSound.GetResourceObject() != ButtonSound)
		{
			Style.SetHoveredSound(TMMakeSlateSound(ButtonSound));
			++OutHoveredButtons;
			bChanged = true;
		}

		if (Style.ClickedSlateSound.GetResourceObject())
		{
			Style.SetClickedSound(TMMakeSlateSound(nullptr));
			++OutClearedClickedButtons;
			++OutExistingClickButtons;
			bChanged = true;
		}

		if (!bChanged)
		{
			return false;
		}

		Button->Modify();
		Button->SetStyle(Style);
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMUIButtonSounds] Button=%s Pressed '%s' -> '%s', Hovered '%s' -> '%s', Clicked '%s' -> ''"),
			*Button->GetPathName(),
			*PreviousPressedSound,
			*GetPathNameSafe(ButtonSound),
			*PreviousHoveredSound,
			*GetPathNameSafe(ButtonSound),
			*PreviousClickedSound);
		return true;
	}

	bool TMPatchUIButtonSoundsForWidgetBlueprint(const FAssetData& AssetData, UObject* ButtonSound)
	{
		UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
		if (!Blueprint)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[TMUIButtonSounds] Skipping non-blueprint widget asset: %s"),
				*AssetData.GetObjectPathString());
			return true;
		}

		UWidgetTree* WidgetTree = TMFindWidgetTree(Blueprint);
		if (!WidgetTree)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TMUIButtonSounds] WidgetTree not found in: %s"), *Blueprint->GetPathName());
			return true;
		}

		int32 ButtonCount = 0;
		int32 PressedButtons = 0;
		int32 HoveredButtons = 0;
		int32 ClearedClickedButtons = 0;
		int32 ExistingClickButtons = 0;
		int32 PressedGraphPins = 0;
		int32 HoveredGraphPins = 0;
		int32 ClickedGraphPins = 0;
		bool bChanged = false;

		WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			UButton* Button = Cast<UButton>(Widget);
			if (!Button)
			{
				return;
			}

			++ButtonCount;
			bChanged |= TMPatchUIButtonStyleSound(
				Button,
				ButtonSound,
				PressedButtons,
				HoveredButtons,
				ClearedClickedButtons,
				ExistingClickButtons);
		});

		if (TMPatchUIButtonSoundGraphPins(Blueprint, ButtonSound, PressedGraphPins, HoveredGraphPins, ClickedGraphPins))
		{
			bChanged = true;
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMUIButtonSounds] %s: Buttons=%d PressedSet=%d HoveredSet=%d ClickedCleared=%d ExistingClickSlots=%d GraphPressedSet=%d GraphHoveredSet=%d GraphClickedCleared=%d"),
			*Blueprint->GetPathName(),
			ButtonCount,
			PressedButtons,
			HoveredButtons,
			ClearedClickedButtons,
			ExistingClickButtons,
			PressedGraphPins,
			HoveredGraphPins,
			ClickedGraphPins);

		if (!bChanged)
		{
			return true;
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status == BS_Error)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMUIButtonSounds] Blueprint compile failed: %s"), *Blueprint->GetPathName());
			return false;
		}

		return TMSavePackageForAsset(Blueprint, TEXT("TMUIButtonSounds"));
	}

	bool TMConsolidateAdvancedLocomotionClick(UObject* ButtonSound)
	{
		if (!ButtonSound)
		{
			return false;
		}

		const TCHAR* OldClickPath = TEXT("/Game/AdvancedLocomotionV4/Audio/UI/Click.Click");
		UObject* OldClickSound = LoadObject<UObject>(nullptr, OldClickPath);
		if (!OldClickSound)
		{
			UE_LOG(LogTemp, Display, TEXT("[TMUIButtonSounds] Old ALS click asset not found, nothing to consolidate."));
			return true;
		}

		if (OldClickSound == ButtonSound)
		{
			UBlueprint* ControllerBlueprint = LoadObject<UBlueprint>(
				nullptr,
				TEXT("/Game/AdvancedLocomotionV4/Blueprints/CharacterLogic/ALS_Player_Controller.ALS_Player_Controller"));
			if (!ControllerBlueprint)
			{
				UE_LOG(LogTemp, Error, TEXT("[TMUIButtonSounds] Failed to load ALS_Player_Controller for old click fixup."));
				return false;
			}

			FBlueprintEditorUtils::MarkBlueprintAsModified(ControllerBlueprint);
			FKismetEditorUtilities::CompileBlueprint(ControllerBlueprint);
			if (ControllerBlueprint->Status == BS_Error)
			{
				UE_LOG(LogTemp, Error, TEXT("[TMUIButtonSounds] ALS_Player_Controller compile failed during old click fixup."));
				return false;
			}

			return TMSavePackageForAsset(ControllerBlueprint, TEXT("TMUIButtonSounds"));
		}

		if (UObjectRedirector* Redirector = Cast<UObjectRedirector>(OldClickSound))
		{
			if (Redirector->DestinationObject != ButtonSound)
			{
				UE_LOG(
					LogTemp,
					Error,
					TEXT("[TMUIButtonSounds] Old ALS click redirector points to %s, expected %s."),
					*GetPathNameSafe(Redirector->DestinationObject),
					*GetPathNameSafe(ButtonSound));
				return false;
			}

			UBlueprint* ControllerBlueprint = LoadObject<UBlueprint>(
				nullptr,
				TEXT("/Game/AdvancedLocomotionV4/Blueprints/CharacterLogic/ALS_Player_Controller.ALS_Player_Controller"));
			if (!ControllerBlueprint)
			{
				UE_LOG(LogTemp, Error, TEXT("[TMUIButtonSounds] Failed to load ALS_Player_Controller for redirector fixup."));
				return false;
			}

			FBlueprintEditorUtils::MarkBlueprintAsModified(ControllerBlueprint);
			FKismetEditorUtilities::CompileBlueprint(ControllerBlueprint);
			if (ControllerBlueprint->Status == BS_Error)
			{
				UE_LOG(LogTemp, Error, TEXT("[TMUIButtonSounds] ALS_Player_Controller compile failed during redirector fixup."));
				return false;
			}

			return TMSavePackageForAsset(ControllerBlueprint, TEXT("TMUIButtonSounds"));
		}

		if (OldClickSound->GetClass() != ButtonSound->GetClass())
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMUIButtonSounds] Cannot consolidate %s (%s) to %s (%s): class mismatch."),
				*GetPathNameSafe(OldClickSound),
				*GetNameSafe(OldClickSound->GetClass()),
				*GetPathNameSafe(ButtonSound),
				*GetNameSafe(ButtonSound->GetClass()));
			return false;
		}

		LoadObject<UObject>(
			nullptr,
			TEXT("/Game/AdvancedLocomotionV4/Blueprints/CharacterLogic/ALS_Player_Controller.ALS_Player_Controller"));

		TArray<UObject*> OldClickSounds;
		OldClickSounds.Add(OldClickSound);
		ObjectTools::FConsolidationResults Results =
			ObjectTools::ConsolidateObjects(ButtonSound, OldClickSounds, false);

		if (Results.InvalidConsolidationObjs.Num() > 0 || Results.FailedConsolidationObjs.Num() > 0)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMUIButtonSounds] Old click consolidation failed. Invalid=%d Failed=%d"),
				Results.InvalidConsolidationObjs.Num(),
				Results.FailedConsolidationObjs.Num());
			return false;
		}

		if (Results.DirtiedPackages.Num() > 0)
		{
			bool bSavedAllPackages = true;
			for (UPackage* Package : Results.DirtiedPackages)
			{
				if (!Package)
				{
					continue;
				}

				const FString PackageFilename = FPackageName::LongPackageNameToFilename(
					Package->GetName(),
					FPackageName::GetAssetPackageExtension());

				if (!TMClearReadOnlyFile(PackageFilename, TEXT("TMUIButtonSounds")))
				{
					bSavedAllPackages = false;
					continue;
				}

				FSavePackageArgs SaveArgs;
				SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
				SaveArgs.SaveFlags = SAVE_NoError;
				if (!UPackage::SavePackage(Package, nullptr, *PackageFilename, SaveArgs))
				{
					UE_LOG(
						LogTemp,
						Error,
						TEXT("[TMUIButtonSounds] Failed to save consolidated package: %s"),
						*PackageFilename);
					bSavedAllPackages = false;
				}
			}

			if (!bSavedAllPackages)
			{
				return false;
			}
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMUIButtonSounds] Consolidated old ALS click to %s. DirtiedPackages=%d"),
			*GetPathNameSafe(ButtonSound),
			Results.DirtiedPackages.Num());
		return true;
	}

	bool TMPatchUIButtonSounds()
	{
		USoundBase* ButtonSound = LoadObject<USoundBase>(nullptr, TMUIButtonPushSoundPath);
		if (!ButtonSound)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMUIButtonSounds] Failed to load button sound: %s"), TMUIButtonPushSoundPath);
			return false;
		}

		FAssetRegistryModule& AssetRegistryModule =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		AssetRegistry.SearchAllAssets(true);

		FARFilter Filter;
		Filter.PackagePaths.Add(FName(TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets")));
		Filter.ClassPaths.Add(FTopLevelAssetPath(FName(TEXT("/Script/UMGEditor")), FName(TEXT("WidgetBlueprint"))));
		Filter.bRecursivePaths = true;
		Filter.bRecursiveClasses = true;

		TArray<FAssetData> WidgetBlueprintAssets;
		AssetRegistry.GetAssets(Filter, WidgetBlueprintAssets);
		WidgetBlueprintAssets.Sort([](const FAssetData& Left, const FAssetData& Right)
		{
			return Left.GetObjectPathString() < Right.GetObjectPathString();
		});

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMUIButtonSounds] Patching %d WidgetBlueprint assets with %s"),
			WidgetBlueprintAssets.Num(),
			*ButtonSound->GetPathName());

		bool bSuccess = true;
		for (const FAssetData& AssetData : WidgetBlueprintAssets)
		{
			bSuccess &= TMPatchUIButtonSoundsForWidgetBlueprint(AssetData, ButtonSound);
		}

		bSuccess &= TMConsolidateAdvancedLocomotionClick(ButtonSound);
		return bSuccess;
	}

	FString TMDescribeWidgetImage(UImage* Image)
	{
		FString Descriptor = Image ? Image->GetName() : FString();
		if (Image)
		{
			if (UObject* ResourceObject = Image->GetBrush().GetResourceObject())
			{
				Descriptor += TEXT(" ");
				Descriptor += ResourceObject->GetPathName();
			}
		}

		return Descriptor;
	}

	bool TMShouldKeepHudImageWhite(const FString& Descriptor)
	{
		return TMContainsToken(Descriptor, TEXT("Crosshair"))
			|| TMContainsToken(Descriptor, TEXT("Reticle"))
			|| TMContainsToken(Descriptor, TEXT("Aiming"))
			|| TMContainsToken(Descriptor, TEXT("Aim"))
			|| TMContainsToken(Descriptor, TEXT("Sight"));
	}

	bool TMShouldTintHudImageYellow(const FString& Descriptor)
	{
		if (TMShouldKeepHudImageWhite(Descriptor)
			|| TMContainsToken(Descriptor, TEXT("Background"))
			|| TMContainsToken(Descriptor, TEXT("Blood"))
			|| TMContainsToken(Descriptor, TEXT("Focus")))
		{
			return false;
		}

		return TMContainsToken(Descriptor, TEXT("Ammo"))
			|| TMContainsToken(Descriptor, TEXT("Health"))
			|| TMContainsToken(Descriptor, TEXT("Weapon"))
			|| TMContainsToken(Descriptor, TEXT("Firemode"))
			|| TMContainsToken(Descriptor, TEXT("Grenade"))
			|| TMContainsToken(Descriptor, TEXT("Syringe"))
			|| TMContainsToken(Descriptor, TEXT("Compass"))
			|| TMContainsToken(Descriptor, TEXT("Hitmarker"))
			|| TMContainsToken(Descriptor, TEXT("Hit_Wheel"))
			|| TMContainsToken(Descriptor, TEXT("Circle"))
			|| TMContainsToken(Descriptor, TEXT("Knife"))
			|| TMContainsToken(Descriptor, TEXT("Frag"))
			|| TMContainsToken(Descriptor, TEXT("Skull"))
			|| TMContainsToken(Descriptor, TEXT("Score"))
			|| TMContainsToken(Descriptor, TEXT("Objective"))
			|| TMContainsToken(Descriptor, TEXT("FillImage"))
			|| TMContainsToken(Descriptor, TEXT("Progress"));
	}

	bool TMShouldTintHudBrushWidgetYellow(const FString& WidgetName)
	{
		if (TMShouldKeepHudImageWhite(WidgetName)
			|| TMContainsToken(WidgetName, TEXT("Background")))
		{
			return false;
		}

		return TMContainsToken(WidgetName, TEXT("Ammo"))
			|| TMContainsToken(WidgetName, TEXT("Health"))
			|| TMContainsToken(WidgetName, TEXT("Weapon"))
			|| TMContainsToken(WidgetName, TEXT("Firemode"))
			|| TMContainsToken(WidgetName, TEXT("Grenade"))
			|| TMContainsToken(WidgetName, TEXT("Score"))
			|| TMContainsToken(WidgetName, TEXT("Objective"))
			|| TMContainsToken(WidgetName, TEXT("Progress"));
	}

	bool TMApplyYellowToWidgetBlueprint(const TCHAR* BlueprintPath, const bool bTintHudBrushes)
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, BlueprintPath);
		if (!Blueprint)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMYellowUI] Failed to load widget blueprint: %s"), BlueprintPath);
			return false;
		}

		UWidgetTree* WidgetTree = TMFindWidgetTree(Blueprint);
		if (!WidgetTree)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMYellowUI] Failed to find WidgetTree in: %s"), *Blueprint->GetPathName());
			return false;
		}

		const FLinearColor Yellow = TMGetDirtyFolderYellow();
		const FSlateColor SlateYellow(Yellow);
		int32 TextCount = 0;
		int32 RichTextCount = 0;
		int32 ButtonCount = 0;
		int32 ImageCount = 0;
		int32 ProgressBarCount = 0;
		int32 BorderCount = 0;
		int32 KeptWhiteCount = 0;
		int32 TextGraphColorCount = 0;
		int32 ButtonForegroundGraphCount = 0;
		bool bChanged = false;

		WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (!Widget)
			{
				return;
			}

			if (UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
			{
				TextBlock->SetColorAndOpacity(SlateYellow);
				++TextCount;
				bChanged = true;
			}

			if (URichTextBlock* RichTextBlock = Cast<URichTextBlock>(Widget))
			{
				RichTextBlock->SetDefaultColorAndOpacity(SlateYellow);
				++RichTextCount;
				bChanged = true;
			}

			if (UButton* Button = Cast<UButton>(Widget))
			{
				if (TMSetButtonForegroundStyleYellow(Button, SlateYellow))
				{
					++ButtonCount;
					bChanged = true;
				}
			}

			if (!bTintHudBrushes)
			{
				return;
			}

			if (UImage* Image = Cast<UImage>(Widget))
			{
				const FString Descriptor = TMDescribeWidgetImage(Image);
				if (TMShouldKeepHudImageWhite(Descriptor))
				{
					++KeptWhiteCount;
					UE_LOG(LogTemp, Display, TEXT("[TMYellowUI] Keeping HUD image white: %s"), *Descriptor);
				}
				else if (TMShouldTintHudImageYellow(Descriptor))
				{
					Image->SetColorAndOpacity(Yellow);
					++ImageCount;
					bChanged = true;
				}
			}

			if (UProgressBar* ProgressBar = Cast<UProgressBar>(Widget))
			{
				ProgressBar->SetFillColorAndOpacity(Yellow);
				++ProgressBarCount;
				bChanged = true;
			}

			if (UBorder* Border = Cast<UBorder>(Widget))
			{
				const FString WidgetName = Border->GetName();
				if (TMShouldTintHudBrushWidgetYellow(WidgetName))
				{
					Border->SetBrushColor(Yellow);
					++BorderCount;
					bChanged = true;
				}
			}
		});

		if (TMPatchYellowWidgetGraphColors(Blueprint, TextGraphColorCount, ButtonForegroundGraphCount))
		{
			bChanged = true;
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMYellowUI] %s: Text=%d RichText=%d Buttons=%d GraphTextColors=%d GraphButtonForegrounds=%d Images=%d ProgressBars=%d Borders=%d KeptWhiteImages=%d"),
			*Blueprint->GetPathName(),
			TextCount,
			RichTextCount,
			ButtonCount,
			TextGraphColorCount,
			ButtonForegroundGraphCount,
			ImageCount,
			ProgressBarCount,
			BorderCount,
			KeptWhiteCount);

		if (!bChanged)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TMYellowUI] No widgets were changed in %s"), *Blueprint->GetPathName());
			return true;
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status == BS_Error)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMYellowUI] Blueprint compile failed: %s"), *Blueprint->GetPathName());
			return false;
		}

		return TMSavePackageForAsset(Blueprint, TEXT("TMYellowUI"));
	}

	FSlateBrush TMMakeNoFillMenuButtonBrush()
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
		Brush.TintColor = FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.0f));
		return Brush;
	}

	FSlateBrush TMMakeMenuButtonIndicatorBrush(UObject* IndicatorResource, const FLinearColor& Tint)
	{
		FSlateBrush Brush;
		if (IndicatorResource)
		{
			Brush.DrawAs = ESlateBrushDrawType::Image;
			Brush.SetResourceObject(IndicatorResource);
		}
		else
		{
			Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
		}

		Brush.TintColor = FSlateColor(Tint);
		return Brush;
	}

	bool TMSetMinimalMenuButtonStyle(
		UButton* Button,
		UObject* IndicatorResource,
		const bool bUseHoverIndicator,
		int32& OutChangedButtons)
	{
		if (!Button)
		{
			return false;
		}

		(void)IndicatorResource;
		(void)bUseHoverIndicator;

		const FLinearColor DirtyYellow = TMGetDirtyFolderYellow();
		const FLinearColor NormalForeground(DirtyYellow.R, DirtyYellow.G, DirtyYellow.B, 0.92f);
		const FLinearColor HoveredForeground(1.0f, 0.0f, 0.0f, 1.0f);
		const FLinearColor DisabledForeground(DirtyYellow.R, DirtyYellow.G, DirtyYellow.B, 0.35f);

		FButtonStyle Style = Button->GetStyle();
		Style
			.SetNormal(TMMakeNoFillMenuButtonBrush())
			.SetHovered(TMMakeNoFillMenuButtonBrush())
			.SetPressed(TMMakeNoFillMenuButtonBrush())
			.SetDisabled(TMMakeNoFillMenuButtonBrush())
			.SetNormalForeground(FSlateColor(NormalForeground))
			.SetHoveredForeground(FSlateColor(HoveredForeground))
			.SetPressedForeground(FSlateColor(HoveredForeground))
			.SetDisabledForeground(FSlateColor(DisabledForeground));

		Button->Modify();
		Button->SetStyle(Style);
		++OutChangedButtons;
		return true;
	}

	void TMVisitContentWidgets(UWidget* Widget, TFunctionRef<void(UWidget*)> Visitor)
	{
		if (!Widget)
		{
			return;
		}

		Visitor(Widget);

		if (UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget))
		{
			const int32 ChildCount = PanelWidget->GetChildrenCount();
			for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
			{
				TMVisitContentWidgets(PanelWidget->GetChildAt(ChildIndex), Visitor);
			}
			return;
		}

		if (UContentWidget* ContentWidget = Cast<UContentWidget>(Widget))
		{
			TMVisitContentWidgets(ContentWidget->GetContent(), Visitor);
		}
	}

	bool TMSetMenuButtonTextUsesForeground(UButton* Button, int32& OutChangedTextBlocks)
	{
		if (!Button)
		{
			return false;
		}

		bool bChanged = false;
		TMVisitContentWidgets(Button, [&](UWidget* Widget)
		{
			if (Widget == Button)
			{
				return;
			}

			if (UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
			{
				TextBlock->Modify();
				TextBlock->SetColorAndOpacity(FSlateColor::UseForeground());
				++OutChangedTextBlocks;
				bChanged = true;
				return;
			}

			if (URichTextBlock* RichTextBlock = Cast<URichTextBlock>(Widget))
			{
				RichTextBlock->Modify();
				RichTextBlock->SetDefaultColorAndOpacity(FSlateColor::UseForeground());
				++OutChangedTextBlocks;
				bChanged = true;
			}
		});

		return bChanged;
	}

	bool TMIsMainMenuLargeLabelText(const FString& Text)
	{
		const FString TrimmedText = Text.TrimStartAndEnd();
		return TrimmedText.Equals(TEXT("Go"), ESearchCase::IgnoreCase)
			|| TrimmedText.Equals(TEXT("Instrument"), ESearchCase::IgnoreCase)
			|| TrimmedText.Equals(TEXT("Settings"), ESearchCase::IgnoreCase)
			|| TrimmedText.Equals(TEXT("Quit"), ESearchCase::IgnoreCase);
	}

	FLinearColor TMGetMainMenuLargeLabelColor(const FString& Text)
	{
		const FString TrimmedText = Text.TrimStartAndEnd();
		if (TrimmedText.Equals(TEXT("Go"), ESearchCase::IgnoreCase))
		{
			return FLinearColor::FromSRGBColor(FColor(250, 222, 82, 255));
		}

		if (TrimmedText.Equals(TEXT("Instrument"), ESearchCase::IgnoreCase))
		{
			return FLinearColor::FromSRGBColor(FColor(255, 188, 23, 255));
		}

		if (TrimmedText.Equals(TEXT("Settings"), ESearchCase::IgnoreCase))
		{
			return FLinearColor::FromSRGBColor(FColor(218, 171, 38, 255));
		}

		return FLinearColor::FromSRGBColor(FColor(255, 236, 118, 255));
	}

	struct FTMMainMenuLargeLabelEntry
	{
		UButton* Button = nullptr;
		TArray<UTextBlock*> TextBlocks;
		FString Text;
		UCanvasPanelSlot* CanvasSlot = nullptr;
		UVerticalBoxSlot* VerticalSlot = nullptr;
		FVector2D OriginalCanvasPosition = FVector2D::ZeroVector;
	};

	bool TMFindMainMenuLargeLabelInButton(
		UButton* Button,
		FString& OutText,
		TArray<UTextBlock*>& OutTextBlocks)
	{
		if (!Button)
		{
			return false;
		}

		bool bFound = false;
		TMVisitContentWidgets(Button, [&](UWidget* Widget)
		{
			UTextBlock* TextBlock = Cast<UTextBlock>(Widget);
			if (!TextBlock)
			{
				return;
			}

			const FString Text = TextBlock->GetText().ToString();
			if (!TMIsMainMenuLargeLabelText(Text))
			{
				return;
			}

			OutText = Text.TrimStartAndEnd();
			OutTextBlocks.Add(TextBlock);
			bFound = true;
		});

		return bFound;
	}

	bool TMPatchMainMenuLargeLabelTextBlock(
		UTextBlock* TextBlock,
		const int32 TargetFontSize,
		int32& OutChangedTextBlocks)
	{
		if (!TextBlock)
		{
			return false;
		}

		bool bChanged = false;
		FSlateFontInfo FontInfo = TextBlock->GetFont();
		if (FontInfo.Size != TargetFontSize)
		{
			FontInfo.Size = TargetFontSize;
			TextBlock->Modify();
			TextBlock->SetFont(FontInfo);
			bChanged = true;
		}

		TextBlock->Modify();
		TextBlock->SetColorAndOpacity(FSlateColor::UseForeground());
		bChanged = true;

		if (bChanged)
		{
			++OutChangedTextBlocks;
		}

		return bChanged;
	}

	bool TMPatchMainMenuLargeLabelButtonColor(
		UButton* Button,
		const FLinearColor& NormalColor,
		int32& OutChangedButtons)
	{
		if (!Button)
		{
			return false;
		}

		const FLinearColor HoveredForeground(1.0f, 0.0f, 0.0f, 1.0f);
		const FLinearColor DisabledForeground(NormalColor.R, NormalColor.G, NormalColor.B, 0.35f);
		FButtonStyle Style = Button->GetStyle();
		Style
			.SetNormalForeground(FSlateColor(NormalColor))
			.SetHoveredForeground(FSlateColor(HoveredForeground))
			.SetPressedForeground(FSlateColor(HoveredForeground))
			.SetDisabledForeground(FSlateColor(DisabledForeground));

		Button->Modify();
		Button->SetStyle(Style);
		++OutChangedButtons;
		return true;
	}

	bool TMPatchMainMenuLargeLabelCanvasSpacing(
		TArray<FTMMainMenuLargeLabelEntry>& Entries,
		int32& OutMovedSlots,
		int32& OutResizedSlots)
	{
		TArray<FTMMainMenuLargeLabelEntry*> CanvasEntries;
		for (FTMMainMenuLargeLabelEntry& Entry : Entries)
		{
			if (Entry.CanvasSlot)
			{
				CanvasEntries.Add(&Entry);
			}
		}

		if (CanvasEntries.Num() < 2)
		{
			return false;
		}

		CanvasEntries.Sort([](const FTMMainMenuLargeLabelEntry& Left, const FTMMainMenuLargeLabelEntry& Right)
		{
			return Left.OriginalCanvasPosition.Y < Right.OriginalCanvasPosition.Y;
		});

		const float FirstY = CanvasEntries[0]->OriginalCanvasPosition.Y;
		const float LastY = CanvasEntries.Last()->OriginalCanvasPosition.Y;
		const float AverageSpacing = (LastY - FirstY) / static_cast<float>(CanvasEntries.Num() - 1);
		const bool bAlreadyExpanded = AverageSpacing >= 80.0f;
		const float SpacingScale = bAlreadyExpanded ? 1.0f : 3.0f;

		bool bChanged = false;
		for (FTMMainMenuLargeLabelEntry* Entry : CanvasEntries)
		{
			UCanvasPanelSlot* CanvasSlot = Entry ? Entry->CanvasSlot : nullptr;
			if (!CanvasSlot)
			{
				continue;
			}

			const FVector2D CurrentPosition = CanvasSlot->GetPosition();
			const FVector2D NewPosition(
				CurrentPosition.X,
				FirstY + (Entry->OriginalCanvasPosition.Y - FirstY) * SpacingScale);
			if (!CurrentPosition.Equals(NewPosition, 0.1f))
			{
				CanvasSlot->Modify();
				CanvasSlot->SetPosition(NewPosition);
				++OutMovedSlots;
				bChanged = true;
			}

			const FVector2D CurrentSize = CanvasSlot->GetSize();
			FVector2D NewSize = CurrentSize;
			if (CurrentSize.X > 0.0f && CurrentSize.X < 360.0f)
			{
				NewSize.X = FMath::Max(CurrentSize.X * 3.0f, 360.0f);
			}
			if (CurrentSize.Y > 0.0f && CurrentSize.Y < 72.0f)
			{
				NewSize.Y = FMath::Max(CurrentSize.Y * 3.0f, 72.0f);
			}

			if (!CurrentSize.Equals(NewSize, 0.1f))
			{
				CanvasSlot->Modify();
				CanvasSlot->SetSize(NewSize);
				++OutResizedSlots;
				bChanged = true;
			}
		}

		return bChanged;
	}

	bool TMPatchMainMenuLargeLabelVerticalSpacing(
		TArray<FTMMainMenuLargeLabelEntry>& Entries,
		int32& OutChangedVerticalSlots)
	{
		TArray<FTMMainMenuLargeLabelEntry*> VerticalEntries;
		for (FTMMainMenuLargeLabelEntry& Entry : Entries)
		{
			if (Entry.VerticalSlot)
			{
				VerticalEntries.Add(&Entry);
			}
		}

		if (VerticalEntries.IsEmpty())
		{
			return false;
		}

		bool bChanged = false;
		for (int32 EntryIndex = 0; EntryIndex < VerticalEntries.Num(); ++EntryIndex)
		{
			UVerticalBoxSlot* VerticalSlot = VerticalEntries[EntryIndex]->VerticalSlot;
			if (!VerticalSlot)
			{
				continue;
			}

			const FMargin CurrentPadding = VerticalSlot->GetPadding();
			FMargin NewPadding = CurrentPadding;
			if (EntryIndex < VerticalEntries.Num() - 1)
			{
				NewPadding.Bottom = FMath::Max(CurrentPadding.Bottom * 3.0f, 48.0f);
			}
			NewPadding.Top = CurrentPadding.Top >= 16.0f ? CurrentPadding.Top : CurrentPadding.Top * 3.0f;

			if (!FMath::IsNearlyEqual(CurrentPadding.Top, NewPadding.Top, 0.1f)
				|| !FMath::IsNearlyEqual(CurrentPadding.Bottom, NewPadding.Bottom, 0.1f))
			{
				VerticalSlot->Modify();
				VerticalSlot->SetPadding(NewPadding);
				++OutChangedVerticalSlots;
				bChanged = true;
			}
		}

		return bChanged;
	}

	bool TMPatchMainMenuBigLabels()
	{
		const TCHAR* BlueprintPath = TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_MainMenu.W_MainMenu");
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, BlueprintPath);
		UWidgetTree* WidgetTree = TMFindWidgetTree(Blueprint);
		if (!Blueprint || !WidgetTree)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMMainMenuBigLabels] Failed to load widget tree. BP=%s Loaded=%d Tree=%d"),
				BlueprintPath,
				Blueprint ? 1 : 0,
				WidgetTree ? 1 : 0);
			return false;
		}

		TArray<FTMMainMenuLargeLabelEntry> Entries;
		WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			UButton* Button = Cast<UButton>(Widget);
			if (!Button)
			{
				return;
			}

			FTMMainMenuLargeLabelEntry Entry;
			Entry.Button = Button;
			if (!TMFindMainMenuLargeLabelInButton(Button, Entry.Text, Entry.TextBlocks))
			{
				return;
			}

			Entry.CanvasSlot = Cast<UCanvasPanelSlot>(Button->Slot);
			Entry.VerticalSlot = Cast<UVerticalBoxSlot>(Button->Slot);
			if (Entry.CanvasSlot)
			{
				Entry.OriginalCanvasPosition = Entry.CanvasSlot->GetPosition();
			}

			Entries.Add(Entry);
		});

		if (Entries.Num() == 0)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMainMenuBigLabels] No main menu label buttons were found."));
			return false;
		}

		int32 ChangedButtons = 0;
		int32 ChangedTextBlocks = 0;
		int32 MovedCanvasSlots = 0;
		int32 ResizedCanvasSlots = 0;
		int32 ChangedVerticalSlots = 0;
		bool bChanged = false;

		const int32 TargetFontSize = 54;
		for (FTMMainMenuLargeLabelEntry& Entry : Entries)
		{
			bChanged |= TMPatchMainMenuLargeLabelButtonColor(
				Entry.Button,
				TMGetMainMenuLargeLabelColor(Entry.Text),
				ChangedButtons);

			for (UTextBlock* TextBlock : Entry.TextBlocks)
			{
				bChanged |= TMPatchMainMenuLargeLabelTextBlock(TextBlock, TargetFontSize, ChangedTextBlocks);
			}

			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMMainMenuBigLabels] Label='%s' Button=%s TextBlocks=%d Canvas=%d Vertical=%d"),
				*Entry.Text,
				*GetPathNameSafe(Entry.Button),
				Entry.TextBlocks.Num(),
				Entry.CanvasSlot ? 1 : 0,
				Entry.VerticalSlot ? 1 : 0);
		}

		bChanged |= TMPatchMainMenuLargeLabelCanvasSpacing(Entries, MovedCanvasSlots, ResizedCanvasSlots);
		bChanged |= TMPatchMainMenuLargeLabelVerticalSpacing(Entries, ChangedVerticalSlots);

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMMainMenuBigLabels] Summary: Labels=%d Buttons=%d TextBlocks=%d MovedCanvasSlots=%d ResizedCanvasSlots=%d ChangedVerticalSlots=%d"),
			Entries.Num(),
			ChangedButtons,
			ChangedTextBlocks,
			MovedCanvasSlots,
			ResizedCanvasSlots,
			ChangedVerticalSlots);

		if (!bChanged)
		{
			return true;
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status == BS_Error)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMainMenuBigLabels] Blueprint compile failed: %s"), *Blueprint->GetPathName());
			return false;
		}

		return TMSavePackageForAsset(Blueprint, TEXT("TMMainMenuBigLabels"));
	}

	bool TMShouldUseMinimalMenuButtonHoverIndicator(const TCHAR* BlueprintPath)
	{
		return false;
	}

	bool TMShouldPatchMinimalMenuButton(const UButton* Button, const TCHAR* BlueprintPath)
	{
		if (!Button)
		{
			return false;
		}

		const FString Path(BlueprintPath);
		if (!Path.Contains(TEXT("W_Loadout"), ESearchCase::IgnoreCase)
			&& !Path.Contains(TEXT("W_Attachments"), ESearchCase::IgnoreCase))
		{
			return true;
		}

		const FString Name = Button->GetName();
		return Name.Equals(TEXT("B_Selection"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("B_Modify"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("B_Return"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("B_Return_1"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("B_Primary"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("B_Secondary"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("B_Special"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("B_Melee"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("B_Explosive"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("B_Optics"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("B_SideRail"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("B_Underbarrel"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("B_Muzzle"), ESearchCase::IgnoreCase)
			|| Name.EndsWith(TEXT("_R"), ESearchCase::IgnoreCase);
	}

	bool TMIsMenuButtonFillResource(const UObject* ResourceObject)
	{
		const FString Path = GetPathNameSafe(ResourceObject);
		if (Path.IsEmpty())
		{
			return false;
		}

		return Path.Contains(TEXT("I_Gradient_Button"), ESearchCase::IgnoreCase)
			|| Path.Contains(TEXT("I_InGame_Button_Gradient"), ESearchCase::IgnoreCase)
			|| Path.Contains(TEXT("I_InGame_Button_solid"), ESearchCase::IgnoreCase)
			|| Path.Contains(TEXT("I_Rounded_Rectangle"), ESearchCase::IgnoreCase)
			|| Path.Contains(TEXT("button_512x128"), ESearchCase::IgnoreCase)
			|| Path.Contains(TEXT("button_normal"), ESearchCase::IgnoreCase)
			|| Path.Contains(TEXT("button_hover"), ESearchCase::IgnoreCase)
			|| Path.Contains(TEXT("button_pressed"), ESearchCase::IgnoreCase)
			|| Path.Contains(TEXT("I_Apply_Empty"), ESearchCase::IgnoreCase);
	}

	bool TMShouldClearMenuButtonImageFill(const UImage* Image)
	{
		if (!Image)
		{
			return false;
		}

		const UObject* ResourceObject = Image->GetBrush().GetResourceObject();
		if (!TMIsMenuButtonFillResource(ResourceObject))
		{
			return false;
		}

		const FString Name = Image->GetName();
		const FString Path = GetPathNameSafe(ResourceObject);
		const bool bLooksLikeFill =
			Name.Contains(TEXT("Button"), ESearchCase::IgnoreCase)
			|| Name.Contains(TEXT("Background"), ESearchCase::IgnoreCase)
			|| Name.Contains(TEXT("BG"), ESearchCase::IgnoreCase)
			|| Name.Contains(TEXT("Focus"), ESearchCase::IgnoreCase)
			|| Name.Contains(TEXT("Selection"), ESearchCase::IgnoreCase)
			|| Path.Contains(TEXT("Button"), ESearchCase::IgnoreCase)
			|| Path.Contains(TEXT("Rectangle"), ESearchCase::IgnoreCase);
		const bool bLooksLikeIcon =
			Name.Contains(TEXT("Icon"), ESearchCase::IgnoreCase)
			|| Name.Contains(TEXT("Arrow"), ESearchCase::IgnoreCase)
			|| Name.Contains(TEXT("Greater"), ESearchCase::IgnoreCase)
			|| Path.Contains(TEXT("Icon"), ESearchCase::IgnoreCase)
			|| Path.Contains(TEXT("Arrow"), ESearchCase::IgnoreCase)
			|| Path.Contains(TEXT("Greater"), ESearchCase::IgnoreCase);

		return bLooksLikeFill && !bLooksLikeIcon;
	}

	bool TMClearMenuButtonImageFill(UImage* Image, int32& OutClearedImages)
	{
		if (!TMShouldClearMenuButtonImageFill(Image))
		{
			return false;
		}

		FLinearColor Color = Image->GetColorAndOpacity();
		if (Color.A == 0.0f)
		{
			return false;
		}

		Color.A = 0.0f;
		Image->Modify();
		Image->SetColorAndOpacity(Color);
		++OutClearedImages;
		return true;
	}

	bool TMShouldClearMenuButtonBorderFill(const UBorder* Border)
	{
		if (!Border)
		{
			return false;
		}

		const UObject* ResourceObject = Border->Background.GetResourceObject();
		const FString Name = Border->GetName();
		return TMIsMenuButtonFillResource(ResourceObject)
			&& (Name.Contains(TEXT("Button"), ESearchCase::IgnoreCase)
				|| Name.Contains(TEXT("Background"), ESearchCase::IgnoreCase)
				|| Name.Contains(TEXT("BG"), ESearchCase::IgnoreCase)
				|| Name.Contains(TEXT("Focus"), ESearchCase::IgnoreCase)
				|| Name.Contains(TEXT("Selection"), ESearchCase::IgnoreCase));
	}

	bool TMClearMenuButtonBorderFill(UBorder* Border, int32& OutClearedBorders)
	{
		if (!TMShouldClearMenuButtonBorderFill(Border))
		{
			return false;
		}

		FLinearColor Color = Border->GetBrushColor();
		if (Color.A == 0.0f)
		{
			return false;
		}

		Color.A = 0.0f;
		Border->Modify();
		Border->SetBrushColor(Color);
		++OutClearedBorders;
		return true;
	}

	bool TMPatchMinimalMenuButtonsForWidgetBlueprint(const TCHAR* BlueprintPath, UObject* IndicatorResource)
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, BlueprintPath);
		UWidgetTree* WidgetTree = TMFindWidgetTree(Blueprint);
		if (!Blueprint || !WidgetTree)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMMinimalMenuButtons] Failed to load widget tree. BP=%s Loaded=%d Tree=%d"),
				BlueprintPath,
				Blueprint ? 1 : 0,
				WidgetTree ? 1 : 0);
			return false;
		}

		int32 ChangedButtons = 0;
		int32 ChangedTextBlocks = 0;
		int32 ClearedImages = 0;
		int32 ClearedBorders = 0;
		const bool bUseHoverIndicator = TMShouldUseMinimalMenuButtonHoverIndicator(BlueprintPath);
		bool bChanged = false;
		WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (UButton* Button = Cast<UButton>(Widget))
			{
				if (TMShouldPatchMinimalMenuButton(Button, BlueprintPath))
				{
					bChanged |= TMSetMinimalMenuButtonStyle(Button, IndicatorResource, bUseHoverIndicator, ChangedButtons);
					bChanged |= TMSetMenuButtonTextUsesForeground(Button, ChangedTextBlocks);
				}
				return;
			}

			if (UImage* Image = Cast<UImage>(Widget))
			{
				bChanged |= TMClearMenuButtonImageFill(Image, ClearedImages);
				return;
			}

			if (UBorder* Border = Cast<UBorder>(Widget))
			{
				bChanged |= TMClearMenuButtonBorderFill(Border, ClearedBorders);
			}
		});

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMMinimalMenuButtons] %s: Buttons=%d TextBlocks=%d ClearedImages=%d ClearedBorders=%d"),
			*Blueprint->GetPathName(),
			ChangedButtons,
			ChangedTextBlocks,
			ClearedImages,
			ClearedBorders);

		if (!bChanged)
		{
			return true;
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status == BS_Error)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMinimalMenuButtons] Blueprint compile failed: %s"), *Blueprint->GetPathName());
			return false;
		}

		return TMSavePackageForAsset(Blueprint, TEXT("TMMinimalMenuButtons"));
	}

	bool TMPatchMinimalMenuButtons()
	{
		UObject* IndicatorResource = LoadObject<UObject>(
			nullptr,
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/Images/I_Strip.I_Strip"));
		if (!IndicatorResource)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TMMinimalMenuButtons] I_Strip not found; hovered/pressed buttons will stay fill-less."));
		}

		const TCHAR* WidgetBlueprintPaths[] =
		{
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_MainMenu.W_MainMenu"),
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_InGameMenu.W_InGameMenu"),
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Settings.W_Settings"),
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Loadout.W_Loadout"),
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Attachments.W_Attachments"),
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Weapon_Layer.W_Weapon_Layer"),
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Attachment_Layer.W_Attachment_Layer"),
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_KeyBindings.W_KeyBindings"),
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Sessions.W_Sessions"),
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_LevelSelect.W_LevelSelect"),
		};

		bool bSuccess = true;
		for (const TCHAR* BlueprintPath : WidgetBlueprintPaths)
		{
			bSuccess &= TMPatchMinimalMenuButtonsForWidgetBlueprint(BlueprintPath, IndicatorResource);
		}

		return bSuccess;
	}

	void TMReimportYellowSplashTextures()
	{
		const TCHAR* SplashTexturePaths[] =
		{
			TEXT("/Game/Splash/Splash.Splash"),
			TEXT("/Game/Splash/EdSplash.EdSplash")
		};

		for (const TCHAR* TexturePath : SplashTexturePaths)
		{
			UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, TexturePath);
			if (!Texture)
			{
				UE_LOG(LogTemp, Warning, TEXT("[TMYellowUI] Splash texture asset not found for reimport: %s"), TexturePath);
				continue;
			}

			if (!FReimportManager::Instance()->Reimport(Texture, false, false, FString(), nullptr, INDEX_NONE, false, true, false))
			{
				UE_LOG(LogTemp, Warning, TEXT("[TMYellowUI] Reimport failed for splash texture asset: %s"), TexturePath);
				continue;
			}

			TMSavePackageForAsset(Texture, TEXT("TMYellowUI"));
		}
	}

	bool TMTintYellowUI()
	{
		TMReimportYellowSplashTextures();

		bool bSuccess = true;
		bSuccess &= TMApplyYellowToWidgetBlueprint(
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_MainMenu.W_MainMenu"),
			false);
		bSuccess &= TMApplyYellowToWidgetBlueprint(
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_InGameMenu.W_InGameMenu"),
			false);
		bSuccess &= TMApplyYellowToWidgetBlueprint(
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Loadout.W_Loadout"),
			false);
		bSuccess &= TMApplyYellowToWidgetBlueprint(
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Attachments.W_Attachments"),
			false);
		bSuccess &= TMApplyYellowToWidgetBlueprint(
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Attachment_Layer.W_Attachment_Layer"),
			false);
		bSuccess &= TMApplyYellowToWidgetBlueprint(
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_HUD.W_HUD"),
			true);
		bSuccess &= TMApplyYellowToWidgetBlueprint(
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Weapon_Layer.W_Weapon_Layer"),
			true);

		return bSuccess;
	}

	bool TMIsIntroSkipTextBlock(const UTextBlock* TextBlock)
	{
		if (!TextBlock)
		{
			return false;
		}

		const FString Text = TextBlock->GetText().ToString().TrimStartAndEnd();
		const FString Name = TextBlock->GetName();
		return Text.Equals(TEXT("Press [Space] to Skip"), ESearchCase::IgnoreCase)
			|| (Text.Contains(TEXT("Space"), ESearchCase::IgnoreCase) && Text.Contains(TEXT("Skip"), ESearchCase::IgnoreCase))
			|| Name.Contains(TEXT("Skip"), ESearchCase::IgnoreCase);
	}

	bool TMSetIntroSkipTextStyle(UTextBlock* TextBlock)
	{
		if (!TMIsIntroSkipTextBlock(TextBlock))
		{
			return false;
		}

		const FLinearColor SkipYellow = FLinearColor::FromSRGBColor(FColor(255, 212, 32, 255));
		FSlateFontInfo Font = TextBlock->GetFont();
		Font.TypefaceFontName = TEXT("Light");
		Font.OutlineSettings.OutlineSize = 0;

		TextBlock->Modify();
		TextBlock->SetColorAndOpacity(FSlateColor(SkipYellow));
		TextBlock->SetFont(Font);
		TextBlock->SetShadowOffset(FVector2D::ZeroVector);
		TextBlock->SetShadowColorAndOpacity(FLinearColor::Transparent);

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMIntroSkipText] Styled %s Text='%s' FontSize=%.1f Typeface=%s"),
			*TextBlock->GetName(),
			*TextBlock->GetText().ToString(),
			static_cast<double>(Font.Size),
			*Font.TypefaceFontName.ToString());
		return true;
	}

	bool TMPatchIntroSkipTextStyle()
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(
			nullptr,
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Intro.W_Intro"));
		if (!Blueprint)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMIntroSkipText] Failed to load W_Intro."));
			return false;
		}

		UWidgetTree* WidgetTree = TMFindWidgetTree(Blueprint);
		if (!WidgetTree)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMIntroSkipText] Failed to find WidgetTree in W_Intro."));
			return false;
		}

		int32 ChangedTextBlocks = 0;
		WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
			{
				if (TMSetIntroSkipTextStyle(TextBlock))
				{
					++ChangedTextBlocks;
				}
			}
		});

		if (ChangedTextBlocks <= 0)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMIntroSkipText] No Space/Skip TextBlock found in W_Intro."));
			return false;
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status == BS_Error)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMIntroSkipText] Blueprint compile failed: %s"), *Blueprint->GetPathName());
			return false;
		}

		UE_LOG(LogTemp, Display, TEXT("[TMIntroSkipText] Styled %d W_Intro skip text block(s)."), ChangedTextBlocks);
		return TMSavePackageForAsset(Blueprint, TEXT("TMIntroSkipText"));
	}

	bool TMIsColorRelatedPin(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return false;
		}

		const FString PinName = Pin->PinName.ToString();
		const FString FriendlyName = Pin->PinFriendlyName.ToString();
		const FString PinCategory = Pin->PinType.PinCategory.ToString();
		const FString PinSubCategory = Pin->PinType.PinSubCategory.ToString();
		const UObject* SubCategoryObject = Pin->PinType.PinSubCategoryObject.Get();
		const FString SubCategoryObjectName = GetNameSafe(SubCategoryObject);

		return PinName.Contains(TEXT("Color"), ESearchCase::IgnoreCase)
			|| FriendlyName.Contains(TEXT("Color"), ESearchCase::IgnoreCase)
			|| PinCategory.Contains(TEXT("Color"), ESearchCase::IgnoreCase)
			|| PinSubCategory.Contains(TEXT("Color"), ESearchCase::IgnoreCase)
			|| SubCategoryObjectName.Contains(TEXT("Color"), ESearchCase::IgnoreCase)
			|| PinName.Contains(TEXT("Foreground"), ESearchCase::IgnoreCase)
			|| FriendlyName.Contains(TEXT("Foreground"), ESearchCase::IgnoreCase);
	}

	bool TMIsYellowRelevantFunctionName(const FString& FunctionName)
	{
		return FunctionName.Contains(TEXT("Color"), ESearchCase::IgnoreCase)
			|| FunctionName.Contains(TEXT("Foreground"), ESearchCase::IgnoreCase)
			|| FunctionName.Contains(TEXT("Brush"), ESearchCase::IgnoreCase);
	}

	void TMDumpYellowWidgetColorGraph(const TCHAR* BlueprintPath)
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, BlueprintPath);
		if (!Blueprint)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMYellowUIDump] Failed to load widget blueprint: %s"), BlueprintPath);
			return;
		}

		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);

		for (const UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}

			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node)
				{
					continue;
				}

				bool bShouldDumpNode = false;
				FString FunctionName;
				FString FunctionClassName;
				if (const UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node))
				{
					FunctionName = CallFunctionNode->FunctionReference.GetMemberName().ToString();
					if (const UFunction* TargetFunction = CallFunctionNode->GetTargetFunction())
					{
						FunctionClassName = GetNameSafe(TargetFunction->GetOwnerClass());
					}
					bShouldDumpNode = TMIsYellowRelevantFunctionName(FunctionName);
				}

				for (const UEdGraphPin* Pin : Node->Pins)
				{
					bShouldDumpNode |= TMIsColorRelatedPin(Pin);
				}

				if (!bShouldDumpNode)
				{
					continue;
				}

				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMYellowUIDump] BP=%s Graph=%s Node=%s Class=%s Function=%s Owner=%s Pins=%d"),
					*Blueprint->GetPathName(),
					*Graph->GetName(),
					*Node->GetName(),
					*Node->GetClass()->GetName(),
					*FunctionName,
					*FunctionClassName,
					Node->Pins.Num());

				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (!Pin)
					{
						continue;
					}

					const UObject* SubCategoryObject = Pin->PinType.PinSubCategoryObject.Get();
					UE_LOG(
						LogTemp,
						Display,
						TEXT("[TMYellowUIDump]   Pin=%s Friendly=%s Dir=%s Cat=%s SubCat=%s Obj=%s Default={%s} Auto={%s} Links=%s Parent=%s SubPins=%d"),
						*Pin->PinName.ToString(),
						*Pin->PinFriendlyName.ToString(),
						Pin->Direction == EGPD_Input ? TEXT("In") : TEXT("Out"),
						*Pin->PinType.PinCategory.ToString(),
						*Pin->PinType.PinSubCategory.ToString(),
						*GetNameSafe(SubCategoryObject),
						*Pin->DefaultValue,
						*Pin->AutogeneratedDefaultValue,
						*TMDescribePinLinks(Pin),
						Pin->ParentPin ? *Pin->ParentPin->PinName.ToString() : TEXT("None"),
						Pin->SubPins.Num());
				}
			}
		}
	}

	FString TMDescribeLoadoutOffsetNode(const UEdGraphNode* Node)
	{
		if (!Node)
		{
			return FString();
		}

		FString Text = FString::Printf(
			TEXT("%s %s %s"),
			*Node->GetName(),
			*Node->GetClass()->GetName(),
			*Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

		if (const UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node))
		{
			Text += FString::Printf(
				TEXT(" Function=%s"),
				*CallFunctionNode->FunctionReference.GetMemberName().ToString());
			if (const UFunction* TargetFunction = CallFunctionNode->GetTargetFunction())
			{
				Text += FString::Printf(TEXT(" Owner=%s"), *GetNameSafe(TargetFunction->GetOwnerClass()));
			}
		}
		else if (const UK2Node_VariableGet* VariableGetNode = Cast<UK2Node_VariableGet>(Node))
		{
			Text += FString::Printf(
				TEXT(" VariableGet=%s"),
				*VariableGetNode->GetVarNameString());
		}
		else if (const UK2Node_VariableSet* VariableSetNode = Cast<UK2Node_VariableSet>(Node))
		{
			Text += FString::Printf(
				TEXT(" VariableSet=%s"),
				*VariableSetNode->GetVarNameString());
		}
		else if (const UK2Node_BreakStruct* BreakStructNode = Cast<UK2Node_BreakStruct>(Node))
		{
			Text += FString::Printf(
				TEXT(" BreakStruct=%s"),
				*GetNameSafe(BreakStructNode->StructType));
		}
		else if (const UK2Node_MakeStruct* MakeStructNode = Cast<UK2Node_MakeStruct>(Node))
		{
			Text += FString::Printf(
				TEXT(" MakeStruct=%s"),
				*GetNameSafe(MakeStructNode->StructType));
		}
		else if (const UK2Node_SpawnActorFromClass* SpawnNode = Cast<UK2Node_SpawnActorFromClass>(Node))
		{
			Text += FString::Printf(
				TEXT(" SpawnClassPin=%s"),
				*TMDescribePin(TMFindPinByNameConst(SpawnNode, TEXT("Class"), EGPD_Input)));
		}

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin)
			{
				continue;
			}

			Text += FString::Printf(
				TEXT(" Pin[%s Friendly=%s Cat=%s SubCat=%s Obj=%s Default=%s Auto=%s Links=%s]"),
				*Pin->PinName.ToString(),
				*Pin->PinFriendlyName.ToString(),
				*Pin->PinType.PinCategory.ToString(),
				*Pin->PinType.PinSubCategory.ToString(),
				*GetNameSafe(Pin->PinType.PinSubCategoryObject.Get()),
				*Pin->DefaultValue,
				*Pin->AutogeneratedDefaultValue,
				*TMDescribePinLinks(Pin));
		}

		return Text;
	}

	bool TMIsLoadoutOffsetRelevantText(const FString& Text)
	{
		return Text.Contains(TEXT("ViewOffset"), ESearchCase::IgnoreCase)
			|| Text.Contains(TEXT("View Offset"), ESearchCase::IgnoreCase)
			|| Text.Contains(TEXT("LocalOffset"), ESearchCase::IgnoreCase)
			|| Text.Contains(TEXT("Local Offset"), ESearchCase::IgnoreCase)
			|| Text.Contains(TEXT("LocalOffsets"), ESearchCase::IgnoreCase)
			|| Text.Contains(TEXT("Local Offsets"), ESearchCase::IgnoreCase)
			|| Text.Contains(TEXT("SetRelative"), ESearchCase::IgnoreCase)
			|| Text.Contains(TEXT("Relative Location"), ESearchCase::IgnoreCase)
			|| Text.Contains(TEXT("Relative Transform"), ESearchCase::IgnoreCase)
			|| Text.Contains(TEXT("SpawnActor"), ESearchCase::IgnoreCase)
			|| Text.Contains(TEXT("GetDataTableRow"), ESearchCase::IgnoreCase)
			|| Text.Contains(TEXT("ST_Weapon"), ESearchCase::IgnoreCase)
			|| Text.Contains(TEXT("Parameters"), ESearchCase::IgnoreCase)
			|| Text.Contains(TEXT("DT_Weapons"), ESearchCase::IgnoreCase)
			|| Text.Contains(TEXT("ActiveWeapon"), ESearchCase::IgnoreCase);
	}

	bool TMDumpLoadoutOffsetGraph()
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(
			nullptr,
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Loadout.W_Loadout"));
		if (!Blueprint)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLoadoutOffsetGraph] Failed to load W_Loadout."));
			return false;
		}

		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMLoadoutOffsetGraph] BP=%s Graphs=%d GeneratedClass=%s"),
			*Blueprint->GetPathName(),
			Graphs.Num(),
			*GetNameSafe(Blueprint->GeneratedClass));

		int32 MatchedNodeCount = 0;
		for (const UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}

			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMLoadoutOffsetGraph] Graph=%s Nodes=%d"),
				*Graph->GetName(),
				Graph->Nodes.Num());

			TSet<const UEdGraphNode*> NodesToDump;
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				const FString NodeText = TMDescribeLoadoutOffsetNode(Node);
				if (!TMIsLoadoutOffsetRelevantText(NodeText))
				{
					continue;
				}

				NodesToDump.Add(Node);
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (!Pin)
					{
						continue;
					}

					for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						if (const UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr)
						{
							NodesToDump.Add(LinkedNode);
						}
					}
				}
			}

			MatchedNodeCount += NodesToDump.Num();
			for (const UEdGraphNode* Node : NodesToDump)
			{
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMLoadoutOffsetGraph]   Node=%s"),
					*TMDescribeLoadoutOffsetNode(Node));
			}
		}

		if (Blueprint->GeneratedClass)
		{
			for (TFieldIterator<FProperty> It(Blueprint->GeneratedClass); It; ++It)
			{
				const FProperty* Property = *It;
				if (!Property)
				{
					continue;
				}

				const FString Name = Property->GetName();
				if (!TMIsLoadoutOffsetRelevantText(Name))
				{
					continue;
				}

				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMLoadoutOffsetGraph]   GeneratedProperty=%s Class=%s"),
					*Name,
					*Property->GetClass()->GetName());
			}
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMLoadoutOffsetGraph] Summary: MatchedNodes=%d"),
			MatchedNodeCount);
		return MatchedNodeCount > 0;
	}

	bool TMIsExecPin(const UEdGraphPin* Pin)
	{
		return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
	}

	bool TMIsLoadoutWeaponSelectionClickEvent(const UEdGraphNode* Node)
	{
		const UK2Node_ComponentBoundEvent* EventNode = Cast<UK2Node_ComponentBoundEvent>(Node);
		if (!EventNode)
		{
			return false;
		}

		const FString ComponentName = EventNode->ComponentPropertyName.ToString();
		const FString DelegateName = EventNode->DelegatePropertyName.ToString();
		const FString NodeName = EventNode->GetName();
		return ComponentName.Equals(TEXT("B_Selection"), ESearchCase::IgnoreCase)
			&& (DelegateName.Contains(TEXT("Clicked"), ESearchCase::IgnoreCase)
				|| NodeName.Contains(TEXT("OnButtonClickedEvent"), ESearchCase::IgnoreCase));
	}

	TArray<UEdGraphPin*> TMGetExecOutputPins(UEdGraphNode* Node)
	{
		TArray<UEdGraphPin*> Pins;
		if (!Node)
		{
			return Pins;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output && TMIsExecPin(Pin))
			{
				Pins.Add(Pin);
			}
		}

		return Pins;
	}

	void TMCollectExecReachableNodes(UEdGraphNode* StartNode, TSet<UEdGraphNode*>& OutNodes)
	{
		if (!StartNode || OutNodes.Contains(StartNode))
		{
			return;
		}

		OutNodes.Add(StartNode);
		for (UEdGraphPin* OutputPin : TMGetExecOutputPins(StartNode))
		{
			for (UEdGraphPin* LinkedPin : OutputPin->LinkedTo)
			{
				if (!LinkedPin || !TMIsExecPin(LinkedPin))
				{
					continue;
				}

				TMCollectExecReachableNodes(LinkedPin->GetOwningNode(), OutNodes);
			}
		}
	}

	bool TMExecReachableContainsFunction(
		const UEdGraphNode* Node,
		const FName FunctionName,
		TSet<const UEdGraphNode*>& VisitedNodes)
	{
		if (!Node || VisitedNodes.Contains(Node))
		{
			return false;
		}

		VisitedNodes.Add(Node);
		if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
		{
			if (CallNode->FunctionReference.GetMemberName() == FunctionName)
			{
				return true;
			}
		}

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output || !TMIsExecPin(Pin))
			{
				continue;
			}

			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				const UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
				if (TMExecReachableContainsFunction(LinkedNode, FunctionName, VisitedNodes))
				{
					return true;
				}
			}
		}

		return false;
	}

	bool TMExecOutputPinReachableContainsFunction(const UEdGraphPin* OutputPin, const FName FunctionName)
	{
		if (!OutputPin)
		{
			return false;
		}

		TSet<const UEdGraphNode*> VisitedNodes;
		for (const UEdGraphPin* LinkedPin : OutputPin->LinkedTo)
		{
			const UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
			if (TMExecReachableContainsFunction(LinkedNode, FunctionName, VisitedNodes))
			{
				return true;
			}
		}

		return false;
	}

	UEdGraphPin* TMFindLinkedOutputDataPin(UEdGraphPin* InputPin)
	{
		if (!InputPin || InputPin->Direction != EGPD_Input)
		{
			return nullptr;
		}

		for (UEdGraphPin* LinkedPin : InputPin->LinkedTo)
		{
			if (LinkedPin && LinkedPin->Direction == EGPD_Output && !TMIsExecPin(LinkedPin))
			{
				return LinkedPin;
			}
		}

		return nullptr;
	}

	bool TMIsVariableGetOutputPin(const UEdGraphPin* OutputPin, const TCHAR* VariableName)
	{
		const UK2Node_VariableGet* VariableGetNode = OutputPin
			? Cast<UK2Node_VariableGet>(OutputPin->GetOwningNode())
			: nullptr;
		return VariableGetNode
			&& VariableGetNode->GetVarNameString().Equals(VariableName, ESearchCase::IgnoreCase);
	}

	bool TMIsInputPinLinkedFromVariableGet(const UEdGraphPin* InputPin, const TCHAR* VariableName)
	{
		if (!InputPin || InputPin->Direction != EGPD_Input)
		{
			return false;
		}

		for (const UEdGraphPin* LinkedPin : InputPin->LinkedTo)
		{
			if (TMIsVariableGetOutputPin(LinkedPin, VariableName))
			{
				return true;
			}
		}

		return false;
	}

	UEdGraphPin* TMFindInputPinLinkedFromVariableGet(UEdGraphNode* Node, const TCHAR* VariableName, UEdGraphPin** OutSourcePin = nullptr)
	{
		if (OutSourcePin)
		{
			*OutSourcePin = nullptr;
		}
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input || TMIsExecPin(Pin))
			{
				continue;
			}

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (TMIsVariableGetOutputPin(LinkedPin, VariableName))
				{
					if (OutSourcePin)
					{
						*OutSourcePin = LinkedPin;
					}
					return Pin;
				}
			}
		}

		return nullptr;
	}

	UEdGraphPin* TMFindInputPinLinkedFromVariableGetOrName(UEdGraphNode* Node, const TCHAR* VariableName, const FName FallbackPinName, UEdGraphPin** OutSourcePin = nullptr)
	{
		UEdGraphPin* Pin = TMFindInputPinLinkedFromVariableGet(Node, VariableName, OutSourcePin);
		if (Pin)
		{
			return Pin;
		}

		Pin = TMFindPinByName(Node, FallbackPinName, EGPD_Input);
		if (OutSourcePin)
		{
			*OutSourcePin = TMFindLinkedOutputDataPin(Pin);
		}
		return Pin;
	}

	FString TMDescribeCallNodeInputLinks(const UK2Node_CallFunction* CallNode)
	{
		if (!CallNode)
		{
			return TEXT("None");
		}

		TArray<FString> Parts;
		for (const UEdGraphPin* Pin : CallNode->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input || TMIsExecPin(Pin) || Pin->LinkedTo.Num() == 0)
			{
				continue;
			}

			Parts.Add(FString::Printf(TEXT("%s<=%s"), *Pin->PinName.ToString(), *TMDescribePinLinks(Pin)));
		}

		return Parts.Num() > 0 ? FString::Join(Parts, TEXT("; ")) : FString(TEXT("None"));
	}

	UEdGraphPin* TMFindVariableGetOutputPinByName(UEdGraphNode* Node, const FName VariableName)
	{
		UK2Node_VariableGet* VariableGetNode = Cast<UK2Node_VariableGet>(Node);
		if (!VariableGetNode || !VariableGetNode->GetVarNameString().Equals(VariableName.ToString(), ESearchCase::IgnoreCase))
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : VariableGetNode->Pins)
		{
			if (Pin
				&& Pin->Direction == EGPD_Output
				&& !TMIsExecPin(Pin)
				&& Pin->PinName == VariableName)
			{
				return Pin;
			}
		}

		return TMFindFirstDataPin(VariableGetNode, EGPD_Output);
	}

	bool TMIsLoadoutOffsetSetWorldTransformNode(UK2Node_CallFunction* CallNode)
	{
		if (!CallNode
			|| CallNode->FunctionReference.GetMemberName() != TEXT("K2_SetWorldTransform"))
		{
			return false;
		}

		return TMFindInputPinLinkedFromVariableGet(CallNode, TEXT("DT_ViewOffset")) != nullptr;
	}

	bool TMInsertLoadoutOffsetLogAfterSetWorldTransform(UEdGraph* Graph, UK2Node_CallFunction* SetWorldTransformNode)
	{
		if (!Graph || !SetWorldTransformNode)
		{
			return false;
		}

		UEdGraphPin* SetThenPin = TMFindPinByName(SetWorldTransformNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
		if (TMExecOutputPinReachableContainsFunction(
			SetThenPin,
			GET_FUNCTION_NAME_CHECKED(UTMGameplayStatics, LogLoadoutPreviewOffsetApplied)))
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMLoadoutOffsetLog] SetWorldTransform already logs offset: Graph=%s Node=%s"),
				*Graph->GetName(),
				*SetWorldTransformNode->GetName());
			return false;
		}

		const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
		UEdGraphPin* TargetSourcePin = nullptr;
		UEdGraphPin* TargetPin = TMFindInputPinLinkedFromVariableGetOrName(
			SetWorldTransformNode,
			TEXT("Item"),
			TEXT("self"),
			&TargetSourcePin);
		UEdGraphPin* ViewOffsetSourcePin = nullptr;
		UEdGraphPin* LocationPin = TMFindInputPinLinkedFromVariableGetOrName(
			SetWorldTransformNode,
			TEXT("DT_ViewOffset"),
			TEXT("NewTransform_Location"),
			&ViewOffsetSourcePin);
		UEdGraphPin* WeaponActorSourcePin = nullptr;

		if (TargetSourcePin)
		{
			UEdGraphNode* TargetSourceNode = TargetSourcePin->GetOwningNode();
			if (UK2Node_VariableGet* ItemGetNode = Cast<UK2Node_VariableGet>(TargetSourceNode))
			{
				UEdGraphPin* ItemSelfPin = TMFindPinByName(ItemGetNode, TEXT("self"), EGPD_Input);
				WeaponActorSourcePin = TMFindLinkedOutputDataPin(ItemSelfPin);
				if (!WeaponActorSourcePin)
				{
					TMFindInputPinLinkedFromVariableGet(ItemGetNode, TEXT("ActiveWeapon"), &WeaponActorSourcePin);
				}
			}
		}

		if (!Schema || !SetThenPin || !TargetSourcePin || !ViewOffsetSourcePin || !WeaponActorSourcePin)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMLoadoutOffsetLog] Missing pins for Graph=%s Node=%s Schema=%d Then=%d Target=%s ViewOffset=%s Weapon=%s"),
				*GetNameSafe(Graph),
				*GetNameSafe(SetWorldTransformNode),
				Schema ? 1 : 0,
				SetThenPin ? 1 : 0,
				*FString::Printf(TEXT("Pin=%s Source=%s"), *TMDescribePin(TargetPin), *TMDescribePin(TargetSourcePin)),
				*FString::Printf(TEXT("Pin=%s Source=%s"), *TMDescribePin(LocationPin), *TMDescribePin(ViewOffsetSourcePin)),
				*TMDescribePin(WeaponActorSourcePin));
			return false;
		}

		TArray<UEdGraphPin*> PreviousThenTargets = SetThenPin->LinkedTo;

		FGraphNodeCreator<UK2Node_CallFunction> LogCreator(*Graph);
		UK2Node_CallFunction* LogNode = LogCreator.CreateNode();
		LogNode->FunctionReference.SetExternalMember(
			GET_FUNCTION_NAME_CHECKED(UTMGameplayStatics, LogLoadoutPreviewOffsetApplied),
			UTMGameplayStatics::StaticClass());
		LogNode->NodePosX = SetWorldTransformNode->NodePosX + 360;
		LogNode->NodePosY = SetWorldTransformNode->NodePosY - 120;
		LogNode->NodeComment = TEXT("TM: log loadout DT_ViewOffset when it is applied to the preview weapon component");
		LogCreator.Finalize();

		UEdGraphPin* LogExecPin = TMFindPinByName(LogNode, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		UEdGraphPin* LogThenPin = TMFindPinByName(LogNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
		UEdGraphPin* WeaponActorPin = TMFindPinByName(LogNode, TEXT("WeaponActor"), EGPD_Input);
		UEdGraphPin* TargetComponentPin = TMFindPinByName(LogNode, TEXT("TargetComponent"), EGPD_Input);
		UEdGraphPin* AppliedViewOffsetPin = TMFindPinByName(LogNode, TEXT("AppliedViewOffset"), EGPD_Input);

		if (!LogExecPin || !LogThenPin || !WeaponActorPin || !TargetComponentPin || !AppliedViewOffsetPin)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMLoadoutOffsetLog] Failed to create log call pins Exec=%d Then=%d Weapon=%d Component=%d Offset=%d"),
				LogExecPin ? 1 : 0,
				LogThenPin ? 1 : 0,
				WeaponActorPin ? 1 : 0,
				TargetComponentPin ? 1 : 0,
				AppliedViewOffsetPin ? 1 : 0);
			Graph->RemoveNode(LogNode);
			return false;
		}

		SetThenPin->Modify();
		SetThenPin->BreakAllPinLinks(false);

		bool bSuccess = true;
		bSuccess &= Schema->TryCreateConnection(SetThenPin, LogExecPin);
		for (UEdGraphPin* PreviousThenTarget : PreviousThenTargets)
		{
			if (PreviousThenTarget)
			{
				bSuccess &= Schema->TryCreateConnection(LogThenPin, PreviousThenTarget);
			}
		}
		bSuccess &= Schema->TryCreateConnection(WeaponActorSourcePin, WeaponActorPin);
		bSuccess &= Schema->TryCreateConnection(TargetSourcePin, TargetComponentPin);
		bSuccess &= Schema->TryCreateConnection(ViewOffsetSourcePin, AppliedViewOffsetPin);

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMLoadoutOffsetLog] Inserted offset apply log after %s in graph %s. OldThenTargets=%d Success=%d Weapon=%s Component=%s Offset=%s"),
			*SetWorldTransformNode->GetName(),
			*Graph->GetName(),
			PreviousThenTargets.Num(),
			bSuccess ? 1 : 0,
			*TMDescribePin(WeaponActorSourcePin),
			*TMDescribePin(TargetSourcePin),
			*TMDescribePin(ViewOffsetSourcePin));
		return bSuccess;
	}

	bool TMPatchLoadoutOffsetApplyLogging()
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(
			nullptr,
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Loadout.W_Loadout"));
		if (!Blueprint)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLoadoutOffsetLog] Failed to load W_Loadout."));
			return false;
		}

		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);

		bool bChanged = false;
		int32 CandidateCount = 0;
		int32 SetWorldTransformCount = 0;
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}

			TArray<UEdGraphNode*> GraphNodes = Graph->Nodes;
			for (UEdGraphNode* Node : GraphNodes)
			{
				UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
				if (CallNode && CallNode->FunctionReference.GetMemberName() == TEXT("K2_SetWorldTransform"))
				{
					++SetWorldTransformCount;
					UE_LOG(
						LogTemp,
						Display,
						TEXT("[TMLoadoutOffsetLog] Saw SetWorldTransform Graph=%s Node=%s InputLinks=%s"),
						*Graph->GetName(),
						*CallNode->GetName(),
						*TMDescribeCallNodeInputLinks(CallNode));
				}
				if (!TMIsLoadoutOffsetSetWorldTransformNode(CallNode))
				{
					continue;
				}

				++CandidateCount;
				if (TMInsertLoadoutOffsetLogAfterSetWorldTransform(Graph, CallNode))
				{
					bChanged = true;
				}
			}
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMLoadoutOffsetLog] Summary: SetWorldTransformNodes=%d CandidateSetWorldTransformNodes=%d Changed=%d"),
			SetWorldTransformCount,
			CandidateCount,
			bChanged ? 1 : 0);

		if (CandidateCount == 0)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLoadoutOffsetLog] Did not find W_Loadout DT_ViewOffset SetWorldTransform node."));
			return false;
		}

		if (!bChanged)
		{
			return true;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status == BS_Error)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLoadoutOffsetLog] W_Loadout failed to compile after patch."));
			return false;
		}

		return TMSavePackageForAsset(Blueprint, TEXT("TMLoadoutOffsetLog"));
	}

	bool TMSpawnNodeAlreadyCallsWeaponSpawnFeedback(const UK2Node_SpawnActorFromClass* SpawnNode)
	{
		const UEdGraphPin* ThenPin = TMFindPinByNameConst(SpawnNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
		return TMExecOutputPinReachableContainsFunction(
			ThenPin,
			GET_FUNCTION_NAME_CHECKED(UTMGameplayStatics, PlayWeaponSpawnFeedbackForActor));
	}

	bool TMSpawnNodeAlreadyCleansLoadoutPreviewBeforeSpawn(const UK2Node_SpawnActorFromClass* SpawnNode)
	{
		const UEdGraphPin* ExecutePin = TMFindPinByNameConst(SpawnNode, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		if (!ExecutePin)
		{
			return false;
		}

		for (const UEdGraphPin* LinkedPin : ExecutePin->LinkedTo)
		{
			const UK2Node_CallFunction* CallNode = LinkedPin ? Cast<UK2Node_CallFunction>(LinkedPin->GetOwningNode()) : nullptr;
			if (CallNode
				&& CallNode->FunctionReference.GetMemberName() == GET_FUNCTION_NAME_CHECKED(UTMGameplayStatics, CleanupLoadoutPreview))
			{
				return true;
			}
		}

		return false;
	}

	bool TMInsertLoadoutPreviewCleanupBeforeSpawn(UEdGraph* Graph, UK2Node_SpawnActorFromClass* SpawnNode)
	{
		if (!Graph || !SpawnNode)
		{
			return false;
		}

		if (TMSpawnNodeAlreadyCleansLoadoutPreviewBeforeSpawn(SpawnNode))
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMLoadoutPreviewCleanup] Spawn node already has pre-spawn cleanup: %s"),
				*SpawnNode->GetName());
			return false;
		}

		const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
		UEdGraphPin* SpawnExecPin = TMFindPinByName(SpawnNode, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		if (!Schema || !SpawnExecPin)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[TMLoadoutPreviewCleanup] Spawn node missing exec pin: %s Exec=%d"),
				*GetNameSafe(SpawnNode),
				SpawnExecPin ? 1 : 0);
			return false;
		}

		TArray<UEdGraphPin*> PreviousExecSources = SpawnExecPin->LinkedTo;
		if (PreviousExecSources.IsEmpty())
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[TMLoadoutPreviewCleanup] Spawn node has no incoming exec links: %s"),
				*GetNameSafe(SpawnNode));
			return false;
		}

		FGraphNodeCreator<UK2Node_CallFunction> CleanupCreator(*Graph);
		UK2Node_CallFunction* CleanupNode = CleanupCreator.CreateNode();
		CleanupNode->FunctionReference.SetExternalMember(
			GET_FUNCTION_NAME_CHECKED(UTMGameplayStatics, CleanupLoadoutPreview),
			UTMGameplayStatics::StaticClass());
		CleanupNode->NodePosX = SpawnNode->NodePosX - 360;
		CleanupNode->NodePosY = SpawnNode->NodePosY - 80;
		CleanupNode->NodeComment = TEXT("TM: cleanup previous loadout weapon preview before spawning another");
		CleanupCreator.Finalize();

		FGraphNodeCreator<UK2Node_Self> SelfCreator(*Graph);
		UK2Node_Self* SelfNode = SelfCreator.CreateNode();
		SelfNode->NodePosX = CleanupNode->NodePosX - 180;
		SelfNode->NodePosY = CleanupNode->NodePosY + 160;
		SelfCreator.Finalize();

		UEdGraphPin* CleanupExecPin = TMFindPinByName(CleanupNode, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		UEdGraphPin* CleanupThenPin = TMFindPinByName(CleanupNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
		UEdGraphPin* OwnerWidgetPin = TMFindPinByName(CleanupNode, TEXT("OwnerWidget"), EGPD_Input);
		if (!OwnerWidgetPin)
		{
			OwnerWidgetPin = TMFindFirstDataPin(CleanupNode, EGPD_Input);
		}

		UEdGraphPin* SelfOutputPin = TMFindPinByName(SelfNode, UEdGraphSchema_K2::PN_Self, EGPD_Output);
		if (!SelfOutputPin)
		{
			SelfOutputPin = TMFindFirstDataPin(SelfNode, EGPD_Output);
		}

		if (!CleanupExecPin || !CleanupThenPin || !OwnerWidgetPin || !SelfOutputPin)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMLoadoutPreviewCleanup] Failed to create cleanup pins in graph %s Exec=%d Then=%d Owner=%d Self=%d"),
				*Graph->GetName(),
				CleanupExecPin ? 1 : 0,
				CleanupThenPin ? 1 : 0,
				OwnerWidgetPin ? 1 : 0,
				SelfOutputPin ? 1 : 0);
			Graph->RemoveNode(CleanupNode);
			Graph->RemoveNode(SelfNode);
			return false;
		}

		SpawnExecPin->Modify();
		SpawnExecPin->BreakAllPinLinks(false);

		bool bSuccess = true;
		for (UEdGraphPin* PreviousExecSource : PreviousExecSources)
		{
			if (PreviousExecSource)
			{
				bSuccess &= Schema->TryCreateConnection(PreviousExecSource, CleanupExecPin);
			}
		}
		bSuccess &= Schema->TryCreateConnection(CleanupThenPin, SpawnExecPin);
		bSuccess &= Schema->TryCreateConnection(SelfOutputPin, OwnerWidgetPin);

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMLoadoutPreviewCleanup] Inserted CleanupLoadoutPreview before %s in graph %s. OldExecSources=%d Success=%d"),
			*SpawnNode->GetName(),
			*Graph->GetName(),
			PreviousExecSources.Num(),
			bSuccess ? 1 : 0);
		return bSuccess;
	}

	bool TMInsertLoadoutWeaponSpawnFeedbackAfterSpawn(UEdGraph* Graph, UK2Node_SpawnActorFromClass* SpawnNode)
	{
		if (!Graph || !SpawnNode)
		{
			return false;
		}

		if (TMSpawnNodeAlreadyCallsWeaponSpawnFeedback(SpawnNode))
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMLoadoutWeaponFeedback] Spawn node already patched: %s"),
				*SpawnNode->GetName());
			return false;
		}

		const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
		UEdGraphPin* SpawnThenPin = TMFindPinByName(SpawnNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
		UEdGraphPin* SpawnReturnPin = TMFindPinByName(SpawnNode, UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
		if (!Schema || !SpawnThenPin || !SpawnReturnPin)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[TMLoadoutWeaponFeedback] Spawn node missing pins: %s Then=%d Return=%d"),
				*GetNameSafe(SpawnNode),
				SpawnThenPin ? 1 : 0,
				SpawnReturnPin ? 1 : 0);
			return false;
		}

		TArray<UEdGraphPin*> PreviousThenTargets = SpawnThenPin->LinkedTo;

		FGraphNodeCreator<UK2Node_CallFunction> FeedbackCreator(*Graph);
		UK2Node_CallFunction* FeedbackNode = FeedbackCreator.CreateNode();
		FeedbackNode->FunctionReference.SetExternalMember(
			GET_FUNCTION_NAME_CHECKED(UTMGameplayStatics, PlayWeaponSpawnFeedbackForActor),
			UTMGameplayStatics::StaticClass());
		FeedbackNode->NodePosX = SpawnNode->NodePosX + 360;
		FeedbackNode->NodePosY = SpawnNode->NodePosY + 80;
		FeedbackNode->NodeComment = TEXT("TM: weapon spawn feedback on weapon UI click");
		FeedbackCreator.Finalize();

		UEdGraphPin* FeedbackExecPin = TMFindPinByName(FeedbackNode, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		UEdGraphPin* FeedbackThenPin = TMFindPinByName(FeedbackNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
		UEdGraphPin* ActorPin = TMFindPinByName(FeedbackNode, TEXT("WeaponActor"), EGPD_Input);

		if (!FeedbackExecPin || !FeedbackThenPin || !ActorPin)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMLoadoutWeaponFeedback] Failed to create feedback call pins Exec=%d Then=%d Actor=%d"),
				FeedbackExecPin ? 1 : 0,
				FeedbackThenPin ? 1 : 0,
				ActorPin ? 1 : 0);
			Graph->RemoveNode(FeedbackNode);
			return false;
		}

		SpawnThenPin->Modify();
		SpawnThenPin->BreakAllPinLinks(false);

		bool bSuccess = true;
		bSuccess &= Schema->TryCreateConnection(SpawnThenPin, FeedbackExecPin);
		bSuccess &= Schema->TryCreateConnection(SpawnReturnPin, ActorPin);

		for (UEdGraphPin* PreviousThenTarget : PreviousThenTargets)
		{
			if (PreviousThenTarget)
			{
				bSuccess &= Schema->TryCreateConnection(FeedbackThenPin, PreviousThenTarget);
			}
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMLoadoutWeaponFeedback] Inserted PlayWeaponSpawnFeedbackForActor after %s in graph %s. OldThenTargets=%d Success=%d"),
			*SpawnNode->GetName(),
			*Graph->GetName(),
			PreviousThenTargets.Num(),
			bSuccess ? 1 : 0);
		return bSuccess;
	}

	bool TMPatchLoadoutWeaponSelectionFeedback()
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(
			nullptr,
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Loadout.W_Loadout"));
		if (!Blueprint)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLoadoutWeaponFeedback] Failed to load W_Loadout."));
			return false;
		}

		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);

		bool bChanged = false;
		int32 SelectionEventCount = 0;
		int32 ReachableSpawnCount = 0;
		int32 TotalSpawnCount = 0;
		TSet<UK2Node_SpawnActorFromClass*> CandidateSpawnNodes;

		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}

			TArray<UEdGraphNode*> Nodes = Graph->Nodes;
			for (UEdGraphNode* Node : Nodes)
			{
				if (UK2Node_SpawnActorFromClass* SpawnNode = Cast<UK2Node_SpawnActorFromClass>(Node))
				{
					++TotalSpawnCount;
					CandidateSpawnNodes.Add(SpawnNode);
					UE_LOG(
						LogTemp,
						Display,
						TEXT("[TMLoadoutWeaponFeedback] Found SpawnActorFromClass candidate: Graph=%s Node=%s"),
						*Graph->GetName(),
						*SpawnNode->GetName());
				}

				if (!TMIsLoadoutWeaponSelectionClickEvent(Node))
				{
					continue;
				}

				++SelectionEventCount;
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMLoadoutWeaponFeedback] Found B_Selection click event: Graph=%s Node=%s"),
					*Graph->GetName(),
					*Node->GetName());

				TSet<UEdGraphNode*> ReachableNodes;
				TMCollectExecReachableNodes(Node, ReachableNodes);
				for (UEdGraphNode* ReachableNode : ReachableNodes)
				{
					UK2Node_SpawnActorFromClass* SpawnNode = Cast<UK2Node_SpawnActorFromClass>(ReachableNode);
					if (!SpawnNode)
					{
						continue;
					}

					++ReachableSpawnCount;
					CandidateSpawnNodes.Add(SpawnNode);
				}
			}
		}

		for (UK2Node_SpawnActorFromClass* SpawnNode : CandidateSpawnNodes)
		{
			UEdGraph* SpawnGraph = SpawnNode ? SpawnNode->GetGraph() : nullptr;
			if (TMInsertLoadoutPreviewCleanupBeforeSpawn(SpawnGraph, SpawnNode))
			{
				bChanged = true;
			}
			if (TMInsertLoadoutWeaponSpawnFeedbackAfterSpawn(SpawnGraph, SpawnNode))
			{
				bChanged = true;
			}
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMLoadoutWeaponFeedback] Summary: SelectionEvents=%d ReachableSpawnNodes=%d TotalSpawnNodes=%d CandidateSpawnNodes=%d Changed=%d"),
			SelectionEventCount,
			ReachableSpawnCount,
			TotalSpawnCount,
			CandidateSpawnNodes.Num(),
			bChanged ? 1 : 0);

		if (CandidateSpawnNodes.IsEmpty())
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLoadoutWeaponFeedback] Did not find any W_Loadout SpawnActorFromClass nodes."));
			return false;
		}

		if (!bChanged)
		{
			return true;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status == BS_Error)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLoadoutWeaponFeedback] W_Loadout failed to compile after patch."));
			return false;
		}

		return TMSavePackageForAsset(Blueprint, TEXT("TMLoadoutWeaponFeedback"));
	}

	bool TMIsLoadoutWeaponLayerIconPatchGraph(const UEdGraph* Graph)
	{
		if (!Graph)
		{
			return false;
		}

		const FString GraphName = Graph->GetName();
		return GraphName.Equals(TEXT("GetDefault"), ESearchCase::IgnoreCase)
			|| GraphName.Equals(TEXT("Highlight"), ESearchCase::IgnoreCase)
			|| GraphName.Equals(TEXT("ReferenceLoadout"), ESearchCase::IgnoreCase);
	}

	bool TMGraphAlreadyCallsLoadoutWeaponLayerIcon(const UEdGraph* Graph)
	{
		if (!Graph)
		{
			return false;
		}

		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
			if (CallNode
				&& CallNode->FunctionReference.GetMemberName()
					== GET_FUNCTION_NAME_CHECKED(UTMGameplayStatics, ApplyLoadoutWeaponLayerIcon))
			{
				return true;
			}
		}

		return false;
	}

	bool TMIsApplyLoadoutWeaponLayerIconCall(const UEdGraphNode* Node)
	{
		const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
		return CallNode
			&& CallNode->FunctionReference.GetMemberName() == GET_FUNCTION_NAME_CHECKED(UTMGameplayStatics, ApplyLoadoutWeaponLayerIcon);
	}

	TArray<UEdGraphPin*> TMGetTerminalExecOutputPins(UEdGraph* Graph)
	{
		TArray<UEdGraphPin*> TerminalPins;
		if (!Graph)
		{
			return TerminalPins;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin
					&& Pin->Direction == EGPD_Output
					&& TMIsExecPin(Pin)
					&& Pin->LinkedTo.IsEmpty())
				{
					TerminalPins.Add(Pin);
				}
			}
		}

		return TerminalPins;
	}

	bool TMInsertLoadoutWeaponLayerIconCallAfterExecPin(
		UEdGraph* Graph,
		UEdGraphPin* SourceThenPin,
		const int32 NodeIndex)
	{
		if (!Graph || !SourceThenPin)
		{
			return false;
		}

		const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
		UEdGraphNode* SourceNode = SourceThenPin->GetOwningNode();
		if (!Schema || !SourceNode)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMLoadoutWeaponIcon] Missing schema/source node for graph %s."),
				*GetNameSafe(Graph));
			return false;
		}

		FGraphNodeCreator<UK2Node_CallFunction> IconCreator(*Graph);
		UK2Node_CallFunction* IconNode = IconCreator.CreateNode();
		IconNode->FunctionReference.SetExternalMember(
			GET_FUNCTION_NAME_CHECKED(UTMGameplayStatics, ApplyLoadoutWeaponLayerIcon),
			UTMGameplayStatics::StaticClass());
		IconNode->NodePosX = SourceNode->NodePosX + 300;
		IconNode->NodePosY = SourceNode->NodePosY + (NodeIndex * 120);
		IconNode->NodeComment = TEXT("TM: apply generated weapon icon to loadout row");
		IconCreator.Finalize();

		FGraphNodeCreator<UK2Node_Self> SelfCreator(*Graph);
		UK2Node_Self* SelfNode = SelfCreator.CreateNode();
		SelfNode->NodePosX = IconNode->NodePosX - 220;
		SelfNode->NodePosY = IconNode->NodePosY + 130;
		SelfCreator.Finalize();

		UEdGraphPin* IconExecPin = TMFindPinByName(IconNode, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		UEdGraphPin* WidgetPin = TMFindPinByName(IconNode, TEXT("WeaponLayerWidget"), EGPD_Input);
		if (!WidgetPin)
		{
			WidgetPin = TMFindFirstDataPin(IconNode, EGPD_Input);
		}

		UEdGraphPin* SelfOutputPin = TMFindPinByName(SelfNode, UEdGraphSchema_K2::PN_Self, EGPD_Output);
		if (!SelfOutputPin)
		{
			SelfOutputPin = TMFindFirstDataPin(SelfNode, EGPD_Output);
		}

		if (!IconExecPin || !WidgetPin || !SelfOutputPin)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMLoadoutWeaponIcon] Failed to create helper pins in graph %s Exec=%d Widget=%d Self=%d"),
				*Graph->GetName(),
				IconExecPin ? 1 : 0,
				WidgetPin ? 1 : 0,
				SelfOutputPin ? 1 : 0);
			Graph->RemoveNode(IconNode);
			Graph->RemoveNode(SelfNode);
			return false;
		}

		SourceThenPin->Modify();
		bool bSuccess = true;
		bSuccess &= Schema->TryCreateConnection(SourceThenPin, IconExecPin);
		bSuccess &= Schema->TryCreateConnection(SelfOutputPin, WidgetPin);

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMLoadoutWeaponIcon] Inserted icon helper in graph %s after %s.%s Success=%d"),
			*Graph->GetName(),
			*GetNameSafe(SourceNode),
			*SourceThenPin->PinName.ToString(),
			bSuccess ? 1 : 0);
		return bSuccess;
	}

	bool TMInsertLoadoutWeaponLayerIconCalls(UEdGraph* Graph)
	{
		if (!Graph || !TMIsLoadoutWeaponLayerIconPatchGraph(Graph))
		{
			return false;
		}

		if (TMGraphAlreadyCallsLoadoutWeaponLayerIcon(Graph))
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMLoadoutWeaponIcon] Graph already patched: %s"),
				*Graph->GetName());
			return false;
		}

		TArray<UEdGraphPin*> TerminalPins = TMGetTerminalExecOutputPins(Graph);
		if (TerminalPins.IsEmpty())
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[TMLoadoutWeaponIcon] No terminal exec pins found in graph %s."),
				*Graph->GetName());
			return false;
		}

		bool bChanged = false;
		for (int32 Index = 0; Index < TerminalPins.Num(); ++Index)
		{
			if (TMInsertLoadoutWeaponLayerIconCallAfterExecPin(Graph, TerminalPins[Index], Index))
			{
				bChanged = true;
			}
		}

		return bChanged;
	}

	bool TMPatchLoadoutWeaponLayerIcons()
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(
			nullptr,
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Weapon_Layer.W_Weapon_Layer"));
		if (!Blueprint)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLoadoutWeaponIcon] Failed to load W_Weapon_Layer."));
			return false;
		}

		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);

		bool bChanged = false;
		int32 CandidateGraphCount = 0;
		for (UEdGraph* Graph : Graphs)
		{
			if (!TMIsLoadoutWeaponLayerIconPatchGraph(Graph))
			{
				continue;
			}

			++CandidateGraphCount;
			if (TMInsertLoadoutWeaponLayerIconCalls(Graph))
			{
				bChanged = true;
			}
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMLoadoutWeaponIcon] Summary: CandidateGraphs=%d Changed=%d"),
			CandidateGraphCount,
			bChanged ? 1 : 0);

		if (CandidateGraphCount == 0)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLoadoutWeaponIcon] Did not find GetDefault/Highlight/ReferenceLoadout graphs."));
			return false;
		}

		if (!bChanged)
		{
			return true;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status == BS_Error)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLoadoutWeaponIcon] W_Weapon_Layer failed to compile after patch."));
			return false;
		}

		return TMSavePackageForAsset(Blueprint, TEXT("TMLoadoutWeaponIcon"));
	}

	bool TMIsLoadoutWeaponLayerCreateWidgetNode(UEdGraphNode* Node)
	{
		if (!Node || !Node->GetClass()->GetName().Equals(TEXT("K2Node_CreateWidget"), ESearchCase::IgnoreCase))
		{
			return false;
		}

		UEdGraphPin* ReturnPin = TMFindPinByName(Node, UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
		const UObject* ReturnType = ReturnPin ? ReturnPin->PinType.PinSubCategoryObject.Get() : nullptr;
		return ReturnType && GetNameSafe(ReturnType).Contains(TEXT("W_Weapon_Layer"), ESearchCase::IgnoreCase);
	}

	bool TMLoadoutCreateWidgetAlreadyAppliesWeaponLayerIcon(UEdGraphNode* CreateWidgetNode)
	{
		if (!CreateWidgetNode)
		{
			return false;
		}

		if (UEdGraphPin* ReturnPin = TMFindPinByName(CreateWidgetNode, UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output))
		{
			for (UEdGraphPin* LinkedPin : ReturnPin->LinkedTo)
			{
				if (LinkedPin && TMIsApplyLoadoutWeaponLayerIconCall(LinkedPin->GetOwningNode()))
				{
					return true;
				}
			}
		}

		if (UEdGraphPin* ThenPin = TMFindPinByName(CreateWidgetNode, UEdGraphSchema_K2::PN_Then, EGPD_Output))
		{
			for (UEdGraphPin* LinkedPin : ThenPin->LinkedTo)
			{
				if (LinkedPin && TMIsApplyLoadoutWeaponLayerIconCall(LinkedPin->GetOwningNode()))
				{
					return true;
				}
			}
		}

		return false;
	}

	bool TMInsertLoadoutWeaponLayerIconCallAfterCreateWidget(UEdGraph* Graph, UEdGraphNode* CreateWidgetNode)
	{
		if (!Graph || !CreateWidgetNode)
		{
			return false;
		}

		if (TMLoadoutCreateWidgetAlreadyAppliesWeaponLayerIcon(CreateWidgetNode))
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMLoadoutWeaponIcon] W_Loadout create widget already patched: Graph=%s Node=%s"),
				*GetNameSafe(Graph),
				*GetNameSafe(CreateWidgetNode));
			return false;
		}

		const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
		UEdGraphPin* CreateThenPin = TMFindPinByName(CreateWidgetNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
		UEdGraphPin* CreateReturnPin = TMFindPinByName(CreateWidgetNode, UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
		if (!Schema || !CreateThenPin || !CreateReturnPin)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[TMLoadoutWeaponIcon] W_Loadout create widget missing pins: Graph=%s Node=%s Then=%d Return=%d"),
				*GetNameSafe(Graph),
				*GetNameSafe(CreateWidgetNode),
				CreateThenPin ? 1 : 0,
				CreateReturnPin ? 1 : 0);
			return false;
		}

		TArray<UEdGraphPin*> PreviousThenTargets = CreateThenPin->LinkedTo;
		if (PreviousThenTargets.IsEmpty())
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[TMLoadoutWeaponIcon] W_Loadout create widget has no outgoing exec link: Graph=%s Node=%s"),
				*GetNameSafe(Graph),
				*GetNameSafe(CreateWidgetNode));
			return false;
		}

		FGraphNodeCreator<UK2Node_CallFunction> IconCreator(*Graph);
		UK2Node_CallFunction* IconNode = IconCreator.CreateNode();
		IconNode->FunctionReference.SetExternalMember(
			GET_FUNCTION_NAME_CHECKED(UTMGameplayStatics, ApplyLoadoutWeaponLayerIcon),
			UTMGameplayStatics::StaticClass());
		IconNode->NodePosX = CreateWidgetNode->NodePosX + 320;
		IconNode->NodePosY = CreateWidgetNode->NodePosY + 80;
		IconNode->NodeComment = TEXT("TM: apply generated weapon icon to created loadout row");
		IconCreator.Finalize();

		UEdGraphPin* IconExecPin = TMFindPinByName(IconNode, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		UEdGraphPin* IconThenPin = TMFindPinByName(IconNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
		UEdGraphPin* WidgetPin = TMFindPinByName(IconNode, TEXT("WeaponLayerWidget"), EGPD_Input);
		if (!WidgetPin)
		{
			WidgetPin = TMFindFirstDataPin(IconNode, EGPD_Input);
		}

		if (!IconExecPin || !IconThenPin || !WidgetPin)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMLoadoutWeaponIcon] Failed to create W_Loadout helper pins: Exec=%d Then=%d Widget=%d"),
				IconExecPin ? 1 : 0,
				IconThenPin ? 1 : 0,
				WidgetPin ? 1 : 0);
			Graph->RemoveNode(IconNode);
			return false;
		}

		CreateThenPin->Modify();
		CreateThenPin->BreakAllPinLinks(false);

		bool bSuccess = true;
		bSuccess &= Schema->TryCreateConnection(CreateThenPin, IconExecPin);
		bSuccess &= Schema->TryCreateConnection(CreateReturnPin, WidgetPin);
		for (UEdGraphPin* PreviousThenTarget : PreviousThenTargets)
		{
			if (PreviousThenTarget)
			{
				bSuccess &= Schema->TryCreateConnection(IconThenPin, PreviousThenTarget);
			}
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMLoadoutWeaponIcon] Inserted W_Loadout icon helper after %s in graph %s. OldThenTargets=%d Success=%d"),
			*GetNameSafe(CreateWidgetNode),
			*GetNameSafe(Graph),
			PreviousThenTargets.Num(),
			bSuccess ? 1 : 0);
		return bSuccess;
	}

	bool TMPatchLoadoutCreatedWeaponLayerIcons()
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(
			nullptr,
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Loadout.W_Loadout"));
		if (!Blueprint)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLoadoutWeaponIcon] Failed to load W_Loadout."));
			return false;
		}

		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);

		int32 CreateWidgetCount = 0;
		bool bChanged = false;
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}

			TArray<UEdGraphNode*> Nodes = Graph->Nodes;
			for (UEdGraphNode* Node : Nodes)
			{
				if (!TMIsLoadoutWeaponLayerCreateWidgetNode(Node))
				{
					continue;
				}

				++CreateWidgetCount;
				if (TMInsertLoadoutWeaponLayerIconCallAfterCreateWidget(Graph, Node))
				{
					bChanged = true;
				}
			}
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMLoadoutWeaponIcon] W_Loadout summary: CreateWeaponLayerWidgets=%d Changed=%d"),
			CreateWidgetCount,
			bChanged ? 1 : 0);

		if (CreateWidgetCount == 0)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLoadoutWeaponIcon] Did not find W_Loadout Create W Weapon Layer Widget nodes."));
			return false;
		}

		if (!bChanged)
		{
			return true;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status == BS_Error)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMLoadoutWeaponIcon] W_Loadout failed to compile after patch."));
			return false;
		}

		return TMSavePackageForAsset(Blueprint, TEXT("TMLoadoutWeaponIcon"));
	}

	bool TMIsMainMenuLoadoutCleanupTargetComponent(const FString& ComponentName)
	{
		static const TCHAR* TargetComponents[] =
		{
			TEXT("B_Settings"),
			TEXT("B_Settings_Hide"),
			TEXT("B_Singleplayer"),
			TEXT("B_Singleplayer_R"),
			TEXT("B_Multiplayer"),
			TEXT("B_Multiplayer_R"),
			TEXT("B_MultiplayerHeader"),
			TEXT("B_Return_1"),
			TEXT("Button_0"),
			TEXT("B_NG"),
			TEXT("B_L_Select"),
			TEXT("B_ContinueGame"),
			TEXT("B_Join"),
			TEXT("B_Host"),
			TEXT("Level_01"),
			TEXT("Level_02"),
			TEXT("Level_03"),
			TEXT("B_Loadout_Hide"),
			TEXT("B_Quit")
		};

		for (const TCHAR* TargetComponent : TargetComponents)
		{
			if (ComponentName.Equals(TargetComponent, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}

	bool TMIsMainMenuLoadoutCleanupFunctionGraph(const UEdGraph* Graph)
	{
		if (!Graph)
		{
			return false;
		}

		const FString GraphName = Graph->GetName();
		if (!GraphName.Contains(TEXT("OnButtonClickedEvent"), ESearchCase::IgnoreCase))
		{
			return false;
		}

		static const TCHAR* TargetComponents[] =
		{
			TEXT("B_Settings"),
			TEXT("B_Settings_Hide"),
			TEXT("B_Singleplayer"),
			TEXT("B_Singleplayer_R"),
			TEXT("B_Multiplayer"),
			TEXT("B_Multiplayer_R"),
			TEXT("B_MultiplayerHeader"),
			TEXT("B_Return_1"),
			TEXT("Button_0"),
			TEXT("B_NG"),
			TEXT("B_L_Select"),
			TEXT("B_ContinueGame"),
			TEXT("B_Join"),
			TEXT("B_Host"),
			TEXT("Level_01"),
			TEXT("Level_02"),
			TEXT("Level_03"),
			TEXT("B_Loadout_Hide"),
			TEXT("B_Quit")
		};

		for (const TCHAR* TargetComponent : TargetComponents)
		{
			if (GraphName.Contains(FString::Printf(TEXT("__%s_"), TargetComponent), ESearchCase::IgnoreCase)
				|| GraphName.Contains(FString::Printf(TEXT("_%s_"), TargetComponent), ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}

	bool TMIsMainMenuLoadoutCleanupClickEvent(const UEdGraphNode* Node)
	{
		const UK2Node_ComponentBoundEvent* EventNode = Cast<UK2Node_ComponentBoundEvent>(Node);
		if (!EventNode)
		{
			return false;
		}

		const FString ComponentName = EventNode->ComponentPropertyName.ToString();
		const FString DelegateName = EventNode->DelegatePropertyName.ToString();
		const FString NodeName = EventNode->GetName();
		return TMIsMainMenuLoadoutCleanupTargetComponent(ComponentName)
			&& (DelegateName.Contains(TEXT("Clicked"), ESearchCase::IgnoreCase)
				|| NodeName.Contains(TEXT("OnButtonClickedEvent"), ESearchCase::IgnoreCase));
	}

	bool TMIsLoadoutCleanupCallNode(const UEdGraphNode* Node)
	{
		const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
		return CallNode
			&& CallNode->FunctionReference.GetMemberName()
				== GET_FUNCTION_NAME_CHECKED(UTMGameplayStatics, CleanupLoadoutPreview);
	}

	bool TMStartNodeAlreadyCallsLoadoutCleanup(UEdGraphNode* StartNode)
	{
		if (!StartNode)
		{
			return false;
		}

		TArray<UEdGraphPin*> StartExecOutputs = TMGetExecOutputPins(StartNode);
		UEdGraphPin* StartThenPin = StartExecOutputs.Num() > 0 ? StartExecOutputs[0] : nullptr;
		if (!StartThenPin)
		{
			return false;
		}

		for (const UEdGraphPin* LinkedPin : StartThenPin->LinkedTo)
		{
			if (LinkedPin && TMIsLoadoutCleanupCallNode(LinkedPin->GetOwningNode()))
			{
				return true;
			}
		}

		return false;
	}

	void TMGetMainMenuLoadoutCleanupStartNodes(UEdGraph* Graph, TArray<UEdGraphNode*>& OutStartNodes)
	{
		if (!Graph)
		{
			return;
		}

		if (TMIsMainMenuLoadoutCleanupFunctionGraph(Graph))
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Cast<UK2Node_FunctionEntry>(Node))
				{
					OutStartNodes.Add(Node);
					return;
				}
			}
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (TMIsMainMenuLoadoutCleanupClickEvent(Node))
			{
				OutStartNodes.Add(Node);
			}
		}
	}

	bool TMInsertMainMenuLoadoutCleanupCall(UEdGraph* Graph, UEdGraphNode* StartNode)
	{
		if (!Graph || !StartNode)
		{
			return false;
		}

		if (TMStartNodeAlreadyCallsLoadoutCleanup(StartNode))
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMMainMenuLoadoutCleanup] Start node already patched: graph %s node %s"),
				*Graph->GetName(),
				*StartNode->GetName());
			return false;
		}

		const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
		TArray<UEdGraphPin*> StartExecOutputs = TMGetExecOutputPins(StartNode);
		UEdGraphPin* StartThenPin = StartExecOutputs.Num() > 0 ? StartExecOutputs[0] : nullptr;
		if (!Schema || !StartThenPin)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[TMMainMenuLoadoutCleanup] Missing start exec pin in graph %s node %s"),
				*GetNameSafe(Graph),
				*GetNameSafe(StartNode));
			return false;
		}

		TArray<UEdGraphPin*> PreviousThenTargets = StartThenPin->LinkedTo;

		FGraphNodeCreator<UK2Node_CallFunction> CleanupCreator(*Graph);
		UK2Node_CallFunction* CleanupNode = CleanupCreator.CreateNode();
		CleanupNode->FunctionReference.SetExternalMember(
			GET_FUNCTION_NAME_CHECKED(UTMGameplayStatics, CleanupLoadoutPreview),
			UTMGameplayStatics::StaticClass());
		CleanupNode->NodePosX = StartNode->NodePosX + 300;
		CleanupNode->NodePosY = StartNode->NodePosY;
		CleanupNode->NodeComment = TEXT("TM: cleanup loadout weapon preview before leaving loadout");
		CleanupCreator.Finalize();

		FGraphNodeCreator<UK2Node_Self> SelfCreator(*Graph);
		UK2Node_Self* SelfNode = SelfCreator.CreateNode();
		SelfNode->NodePosX = StartNode->NodePosX + 300;
		SelfNode->NodePosY = StartNode->NodePosY + 170;
		SelfCreator.Finalize();

		UEdGraphPin* CleanupExecPin = TMFindPinByName(CleanupNode, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		UEdGraphPin* CleanupThenPin = TMFindPinByName(CleanupNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
		UEdGraphPin* OwnerWidgetPin = TMFindPinByName(CleanupNode, TEXT("OwnerWidget"), EGPD_Input);
		if (!OwnerWidgetPin)
		{
			OwnerWidgetPin = TMFindFirstDataPin(CleanupNode, EGPD_Input);
		}

		UEdGraphPin* SelfOutputPin = TMFindPinByName(SelfNode, UEdGraphSchema_K2::PN_Self, EGPD_Output);
		if (!SelfOutputPin)
		{
			SelfOutputPin = TMFindFirstDataPin(SelfNode, EGPD_Output);
		}

		if (!CleanupExecPin || !CleanupThenPin || !OwnerWidgetPin || !SelfOutputPin)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMMainMenuLoadoutCleanup] Failed to create cleanup pins in graph %s Exec=%d Then=%d Owner=%d Self=%d"),
				*Graph->GetName(),
				CleanupExecPin ? 1 : 0,
				CleanupThenPin ? 1 : 0,
				OwnerWidgetPin ? 1 : 0,
				SelfOutputPin ? 1 : 0);
			Graph->RemoveNode(CleanupNode);
			Graph->RemoveNode(SelfNode);
			return false;
		}

		StartThenPin->Modify();
		StartThenPin->BreakAllPinLinks(false);

		bool bSuccess = true;
		bSuccess &= Schema->TryCreateConnection(StartThenPin, CleanupExecPin);
		bSuccess &= Schema->TryCreateConnection(SelfOutputPin, OwnerWidgetPin);

		for (UEdGraphPin* PreviousThenTarget : PreviousThenTargets)
		{
			if (PreviousThenTarget)
			{
				bSuccess &= Schema->TryCreateConnection(CleanupThenPin, PreviousThenTarget);
			}
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMMainMenuLoadoutCleanup] Inserted CleanupLoadoutPreview in graph %s after %s. OldThenTargets=%d Success=%d"),
			*Graph->GetName(),
			*StartNode->GetName(),
			PreviousThenTargets.Num(),
			bSuccess ? 1 : 0);
		return bSuccess;
	}

	bool TMPatchMainMenuLoadoutPreviewCleanup()
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(
			nullptr,
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_MainMenu.W_MainMenu"));
		if (!Blueprint)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMainMenuLoadoutCleanup] Failed to load W_MainMenu."));
			return false;
		}

		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);

		bool bChanged = false;
		int32 CandidateGraphCount = 0;
		int32 CandidateStartCount = 0;
		for (UEdGraph* Graph : Graphs)
		{
			TArray<UEdGraphNode*> StartNodes;
			TMGetMainMenuLoadoutCleanupStartNodes(Graph, StartNodes);
			if (StartNodes.IsEmpty())
			{
				continue;
			}

			++CandidateGraphCount;
			CandidateStartCount += StartNodes.Num();
			for (UEdGraphNode* StartNode : StartNodes)
			{
				if (TMInsertMainMenuLoadoutCleanupCall(Graph, StartNode))
				{
					bChanged = true;
				}
			}
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMMainMenuLoadoutCleanup] Summary: CandidateGraphs=%d CandidateStarts=%d Changed=%d"),
			CandidateGraphCount,
			CandidateStartCount,
			bChanged ? 1 : 0);

		if (CandidateStartCount == 0)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMainMenuLoadoutCleanup] Did not find main menu leave-loadout handlers."));
			return false;
		}

		if (!bChanged)
		{
			return true;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status == BS_Error)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMainMenuLoadoutCleanup] W_MainMenu failed to compile after patch."));
			return false;
		}

		return TMSavePackageForAsset(Blueprint, TEXT("TMMainMenuLoadoutCleanup"));
	}

	bool TMSetObjectPropertyIfDifferent(UObject* Object, const FName PropertyName, UObject* Value, const TCHAR* LogPrefix, bool& bChanged)
	{
		FObjectPropertyBase* Property = Object
			? FindFProperty<FObjectPropertyBase>(Object->GetClass(), PropertyName)
			: nullptr;
		if (!Property)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[%s] Missing object property %s on %s."),
				LogPrefix,
				*PropertyName.ToString(),
				Object ? *Object->GetPathName() : TEXT("None"));
			return false;
		}

		UObject* PreviousValue = Property->GetObjectPropertyValue_InContainer(Object);
		if (PreviousValue == Value)
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[%s] %s already %s on %s."),
				LogPrefix,
				*PropertyName.ToString(),
				Value ? *Value->GetPathName() : TEXT("None"),
				*Object->GetPathName());
			return true;
		}

		Object->Modify();
		Property->SetObjectPropertyValue_InContainer(Object, Value);
		FPropertyChangedEvent PropertyChangedEvent(Property);
		Object->PostEditChangeProperty(PropertyChangedEvent);
		Object->MarkPackageDirty();
		bChanged = true;

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[%s] Set %s: %s -> %s on %s."),
			LogPrefix,
			*PropertyName.ToString(),
			PreviousValue ? *PreviousValue->GetPathName() : TEXT("None"),
			Value ? *Value->GetPathName() : TEXT("None"),
			*Object->GetPathName());
		return true;
	}

	bool TMSetVectorPropertyIfDifferent(UObject* Object, const FName PropertyName, const FVector& Value, const TCHAR* LogPrefix, bool& bChanged)
	{
		FStructProperty* Property = Object
			? FindFProperty<FStructProperty>(Object->GetClass(), PropertyName)
			: nullptr;
		if (!Property || Property->Struct != TBaseStructure<FVector>::Get())
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[%s] Missing vector property %s on %s."),
				LogPrefix,
				*PropertyName.ToString(),
				Object ? *Object->GetPathName() : TEXT("None"));
			return false;
		}

		FVector* PreviousValue = Property->ContainerPtrToValuePtr<FVector>(Object);
		if (PreviousValue && PreviousValue->Equals(Value, KINDA_SMALL_NUMBER))
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[%s] %s already %s on %s."),
				LogPrefix,
				*PropertyName.ToString(),
				*Value.ToString(),
				*Object->GetPathName());
			return true;
		}

		const FVector OldValue = PreviousValue ? *PreviousValue : FVector::ZeroVector;
		Object->Modify();
		Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Object), &Value);
		FPropertyChangedEvent PropertyChangedEvent(Property);
		Object->PostEditChangeProperty(PropertyChangedEvent);
		Object->MarkPackageDirty();
		bChanged = true;

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[%s] Set %s: %s -> %s on %s."),
			LogPrefix,
			*PropertyName.ToString(),
			*OldValue.ToString(),
			*Value.ToString(),
			*Object->GetPathName());
		return true;
	}

	bool TMSetAudioComponentSoundIfDifferent(
		UAudioComponent* AudioComponent,
		USoundBase* Sound,
		const TCHAR* LogPrefix,
		bool& bChanged)
	{
		if (!AudioComponent)
		{
			UE_LOG(LogTemp, Error, TEXT("[%s] Missing soundtrack audio component."), LogPrefix);
			return false;
		}

		USoundBase* PreviousSound = AudioComponent->GetSound();
		if (PreviousSound == Sound)
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[%s] AudioComponent.Sound already %s on %s."),
				LogPrefix,
				Sound ? *Sound->GetPathName() : TEXT("None"),
				*AudioComponent->GetPathName());
			return true;
		}

		AudioComponent->Modify();
		AudioComponent->SetSound(Sound);
		if (FProperty* SoundProperty = FindFProperty<FProperty>(AudioComponent->GetClass(), TEXT("Sound")))
		{
			FPropertyChangedEvent PropertyChangedEvent(SoundProperty);
			AudioComponent->PostEditChangeProperty(PropertyChangedEvent);
		}
		AudioComponent->MarkPackageDirty();
		bChanged = true;

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[%s] Set AudioComponent.Sound: %s -> %s on %s."),
			LogPrefix,
			PreviousSound ? *PreviousSound->GetPathName() : TEXT("None"),
			Sound ? *Sound->GetPathName() : TEXT("None"),
			*AudioComponent->GetPathName());
		return true;
	}

	FString TMGetCallFunctionName(const UEdGraphNode* Node)
	{
		const UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node);
		return CallFunctionNode ? CallFunctionNode->FunctionReference.GetMemberName().ToString() : FString();
	}

	FString TMGetVariableReferenceName(const UEdGraphNode* Node)
	{
		if (const UK2Node_VariableGet* VariableGetNode = Cast<UK2Node_VariableGet>(Node))
		{
			return VariableGetNode->VariableReference.GetMemberName().ToString();
		}
		if (const UK2Node_VariableSet* VariableSetNode = Cast<UK2Node_VariableSet>(Node))
		{
			return VariableSetNode->VariableReference.GetMemberName().ToString();
		}
		return FString();
	}

	bool TMIsMenuSoundDumpNode(const UEdGraphNode* Node)
	{
		if (!Node)
		{
			return false;
		}

		const FString NodeText = Node->GetName()
			+ TEXT(" ") + Node->GetClass()->GetName()
			+ TEXT(" ") + Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString()
			+ TEXT(" ") + Node->NodeComment
			+ TEXT(" ") + TMGetCallFunctionName(Node)
			+ TEXT(" ") + TMGetVariableReferenceName(Node);

		static const TCHAR* Needles[] =
		{
			TEXT("Sound"),
			TEXT("Audio"),
			TEXT("Soundtrack"),
			TEXT("SpawnSound2D"),
			TEXT("CreateSound2D"),
			TEXT("PlaySound2D"),
			TEXT("AnalyzeAudioComponent"),
			TEXT("EnvelopeFollower"),
			TEXT("OnBeat"),
			TEXT("On Beat"),
			TEXT("OnDownbeat"),
			TEXT("On Downbeat"),
			TEXT("Assign"),
			TEXT("Bind Event"),
			TEXT("Add Event"),
			TEXT("Custom Event"),
			TEXT("PlayAnimation"),
			TEXT("Play Animation"),
			TEXT("Animation"),
			TEXT("Delay"),
			TEXT("Timer"),
			TEXT("K2_SetTimerDelegate"),
			TEXT("MoveToMainMenu"),
			TEXT("Construct"),
			TEXT("LowPass"),
			TEXT("VolumeMultiplier"),
			TEXT("OutsideVolume"),
			TEXT("IfThenElse"),
			TEXT("Is Valid")
		};

		for (const TCHAR* Needle : Needles)
		{
			if (NodeText.Contains(Needle, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			const UObject* DefaultObject = Pin ? Pin->DefaultObject : nullptr;
			const FString PinText = Pin
				? Pin->PinName.ToString()
					+ TEXT(" ") + Pin->DefaultValue
					+ TEXT(" ") + Pin->AutogeneratedDefaultValue
					+ TEXT(" ") + GetNameSafe(DefaultObject)
				: FString();
			for (const TCHAR* Needle : Needles)
			{
				if (PinText.Contains(Needle, ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
		}

		return false;
	}

	void TMDumpMenuSoundNode(const UBlueprint* Blueprint, const UEdGraph* Graph, const UEdGraphNode* Node)
	{
		if (!Blueprint || !Graph || !Node)
		{
			return;
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMMenuSoundGraphDump] BP=%s Graph=%s Node=%s Class=%s Title={%s} Function=%s Variable=%s Pins=%d"),
			*Blueprint->GetPathName(),
			*Graph->GetName(),
			*Node->GetName(),
			*Node->GetClass()->GetName(),
			*Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString(),
			*TMGetCallFunctionName(Node),
			*TMGetVariableReferenceName(Node),
			Node->Pins.Num());

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin)
			{
				continue;
			}

			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMMenuSoundGraphDump]   Pin=%s Dir=%s Cat=%s Obj=%s Default={%s} Auto={%s} Links=%s"),
				*Pin->PinName.ToString(),
				Pin->Direction == EGPD_Input ? TEXT("In") : TEXT("Out"),
				*Pin->PinType.PinCategory.ToString(),
				*GetNameSafe(Pin->DefaultObject),
				*Pin->DefaultValue,
				*Pin->AutogeneratedDefaultValue,
				*TMDescribePinLinks(Pin));
		}
	}

	bool TMDumpMenuSoundGraph()
	{
		const TCHAR* WidgetBlueprintPaths[] =
		{
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Intro.W_Intro"),
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_MainMenu.W_MainMenu")
		};

		bool bSuccess = true;
		for (const TCHAR* BlueprintPath : WidgetBlueprintPaths)
		{
			UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, BlueprintPath);
			if (!Blueprint)
			{
				UE_LOG(LogTemp, Error, TEXT("[TMMenuSoundGraphDump] Failed to load widget blueprint: %s"), BlueprintPath);
				bSuccess = false;
				continue;
			}

			TArray<UEdGraph*> Graphs;
			Blueprint->GetAllGraphs(Graphs);
			UE_LOG(LogTemp, Display, TEXT("[TMMenuSoundGraphDump] BP=%s Graphs=%d"), *Blueprint->GetPathName(), Graphs.Num());
			for (const UEdGraph* Graph : Graphs)
			{
				if (!Graph)
				{
					continue;
				}

				for (const UEdGraphNode* Node : Graph->Nodes)
				{
					if (TMIsMenuSoundDumpNode(Node))
					{
						TMDumpMenuSoundNode(Blueprint, Graph, Node);
					}
				}
			}
		}

		return bSuccess;
	}

	bool TMDumpIntroMainFlow()
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(
			nullptr,
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Intro.W_Intro"));
		if (!Blueprint)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMIntroMainFlowDump] Failed to load W_Intro."));
			return false;
		}

		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (const UEdGraph* Graph : Graphs)
		{
			if (!Graph
				|| (!Graph->GetName().Equals(TEXT("EventGraph"), ESearchCase::IgnoreCase)
					&& !Graph->GetName().Equals(TEXT("MoveToMainMenu"), ESearchCase::IgnoreCase)))
			{
				continue;
			}

			UE_LOG(LogTemp, Display, TEXT("[TMIntroMainFlowDump] Graph=%s Nodes=%d"), *Graph->GetName(), Graph->Nodes.Num());
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node)
				{
					continue;
				}

				bool bHasExecPin = false;
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					bHasExecPin |= TMIsExecPin(Pin);
				}

				const FString NodeText = Node->GetName()
					+ TEXT(" ") + Node->GetClass()->GetName()
					+ TEXT(" ") + Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString()
					+ TEXT(" ") + TMGetCallFunctionName(Node)
					+ TEXT(" ") + TMGetVariableReferenceName(Node);
				const bool bRelevantDataNode =
					NodeText.Contains(TEXT("MainMenu"), ESearchCase::IgnoreCase)
					|| NodeText.Contains(TEXT("Soundtrack"), ESearchCase::IgnoreCase)
					|| NodeText.Contains(TEXT("SpawnSound"), ESearchCase::IgnoreCase)
					|| NodeText.Contains(TEXT("Create"), ESearchCase::IgnoreCase);
				if (!bHasExecPin && !bRelevantDataNode)
				{
					continue;
				}

				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMIntroMainFlowDump] Node=%s Class=%s Title={%s} Function=%s Variable=%s"),
					*Node->GetName(),
					*Node->GetClass()->GetName(),
					*Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString(),
					*TMGetCallFunctionName(Node),
					*TMGetVariableReferenceName(Node));

				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (!Pin)
					{
						continue;
					}

					const bool bShouldDumpPin = TMIsExecPin(Pin)
						|| Pin->LinkedTo.Num() > 0
						|| Pin->DefaultObject
						|| Pin->PinName.ToString().Contains(TEXT("Sound"), ESearchCase::IgnoreCase)
						|| Pin->PinName.ToString().Contains(TEXT("Class"), ESearchCase::IgnoreCase);
					if (!bShouldDumpPin)
					{
						continue;
					}

					UE_LOG(
						LogTemp,
						Display,
						TEXT("[TMIntroMainFlowDump]   Pin=%s Dir=%s Cat=%s Obj=%s Default={%s} Auto={%s} Links=%s"),
						*Pin->PinName.ToString(),
						Pin->Direction == EGPD_Input ? TEXT("In") : TEXT("Out"),
						*Pin->PinType.PinCategory.ToString(),
						*GetNameSafe(Pin->DefaultObject),
						*Pin->DefaultValue,
						*Pin->AutogeneratedDefaultValue,
						*TMDescribePinLinks(Pin));
				}
			}
		}

		return true;
	}

	struct FTMSpawnSound2DSettings
	{
		USoundBase* Sound = nullptr;
		UObject* ConcurrencySettings = nullptr;
		FString VolumeMultiplier = TEXT("1.000000");
		FString PitchMultiplier = TEXT("1.000000");
		FString StartTime = TEXT("0.000000");
		FString bPersistAcrossLevelTransition = TEXT("false");
		FString bAutoDestroy = TEXT("true");
	};

	bool TMIsSpawnSound2DNode(const UEdGraphNode* Node)
	{
		const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
		return CallNode
			&& CallNode->FunctionReference.GetMemberName()
				== GET_FUNCTION_NAME_CHECKED(UGameplayStatics, SpawnSound2D);
	}

	bool TMSpawnSound2DUsesSound(const UK2Node_CallFunction* SpawnNode, const UObject* Sound)
	{
		const UEdGraphPin* SoundPin = TMFindPinByName(
			const_cast<UK2Node_CallFunction*>(SpawnNode),
			TEXT("Sound"),
			EGPD_Input);
		return SoundPin && SoundPin->DefaultObject == Sound;
	}

	FString TMGetPinDefaultOrAuto(const UEdGraphPin* Pin, const TCHAR* Fallback)
	{
		if (!Pin)
		{
			return Fallback;
		}

		if (!Pin->DefaultValue.IsEmpty())
		{
			return Pin->DefaultValue;
		}

		if (!Pin->AutogeneratedDefaultValue.IsEmpty())
		{
			return Pin->AutogeneratedDefaultValue;
		}

		return Fallback;
	}

	UObject* TMResolveObjectInputPinDefault(const UBlueprint* Blueprint, const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return nullptr;
		}

		if (Pin->DefaultObject)
		{
			return Pin->DefaultObject;
		}

		for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			const UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
			const UK2Node_VariableGet* VariableGetNode = Cast<UK2Node_VariableGet>(LinkedNode);
			const FName VariableName = VariableGetNode
				? VariableGetNode->VariableReference.GetMemberName()
				: NAME_None;
			if (VariableName.IsNone())
			{
				continue;
			}

			UObject* DefaultObject = Blueprint && Blueprint->GeneratedClass
				? Blueprint->GeneratedClass->GetDefaultObject()
				: nullptr;
			const FObjectPropertyBase* ObjectProperty = DefaultObject
				? FindFProperty<FObjectPropertyBase>(DefaultObject->GetClass(), VariableName)
				: nullptr;
			if (ObjectProperty)
			{
				return ObjectProperty->GetObjectPropertyValue_InContainer(DefaultObject);
			}
		}

		return nullptr;
	}

	bool TMExtractSpawnSound2DSettings(
		const UBlueprint* Blueprint,
		UK2Node_CallFunction* SpawnNode,
		FTMSpawnSound2DSettings& OutSettings)
	{
		if (!SpawnNode || !TMIsSpawnSound2DNode(SpawnNode))
		{
			return false;
		}

		UEdGraphPin* SoundPin = TMFindPinByName(SpawnNode, TEXT("Sound"), EGPD_Input);
		OutSettings.Sound = Cast<USoundBase>(SoundPin ? SoundPin->DefaultObject : nullptr);
		OutSettings.VolumeMultiplier = TMGetPinDefaultOrAuto(
			TMFindPinByName(SpawnNode, TEXT("VolumeMultiplier"), EGPD_Input),
			TEXT("1.000000"));
		OutSettings.PitchMultiplier = TMGetPinDefaultOrAuto(
			TMFindPinByName(SpawnNode, TEXT("PitchMultiplier"), EGPD_Input),
			TEXT("1.000000"));
		OutSettings.StartTime = TMGetPinDefaultOrAuto(
			TMFindPinByName(SpawnNode, TEXT("StartTime"), EGPD_Input),
			TEXT("0.000000"));
		OutSettings.ConcurrencySettings = TMResolveObjectInputPinDefault(
			Blueprint,
			TMFindPinByName(SpawnNode, TEXT("ConcurrencySettings"), EGPD_Input));
		OutSettings.bPersistAcrossLevelTransition = TMGetPinDefaultOrAuto(
			TMFindPinByName(SpawnNode, TEXT("bPersistAcrossLevelTransition"), EGPD_Input),
			TEXT("false"));
		OutSettings.bAutoDestroy = TMGetPinDefaultOrAuto(
			TMFindPinByName(SpawnNode, TEXT("bAutoDestroy"), EGPD_Input),
			TEXT("true"));

		return OutSettings.Sound != nullptr;
	}

	UK2Node_CallFunction* TMFindSpawnSound2DForSound(UEdGraph* Graph, const UObject* Sound)
	{
		if (!Graph || !Sound)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_CallFunction* SpawnNode = Cast<UK2Node_CallFunction>(Node);
			if (SpawnNode && TMIsSpawnSound2DNode(SpawnNode) && TMSpawnSound2DUsesSound(SpawnNode, Sound))
			{
				return SpawnNode;
			}
		}

		return nullptr;
	}

	UK2Node_VariableSet* TMFindSetSoundtrackAfterSpawn(UK2Node_CallFunction* SpawnNode)
	{
		UEdGraphPin* SpawnThenPin = TMFindPinByName(SpawnNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
		if (!SpawnThenPin)
		{
			return nullptr;
		}

		for (UEdGraphPin* LinkedPin : SpawnThenPin->LinkedTo)
		{
			UK2Node_VariableSet* CandidateNode = Cast<UK2Node_VariableSet>(
				LinkedPin ? LinkedPin->GetOwningNode() : nullptr);
			if (CandidateNode
				&& CandidateNode->VariableReference.GetMemberName() == TEXT("Soundtrack"))
			{
				return CandidateNode;
			}
		}

		return nullptr;
	}

	UK2Node_CallFunction* TMFindMainMenuSoundtrackStartSpawn(UEdGraph* Graph, const UObject* Sound)
	{
		if (!Graph || !Sound)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_CallFunction* SpawnNode = Cast<UK2Node_CallFunction>(Node);
			if (!SpawnNode || !TMIsSpawnSound2DNode(SpawnNode))
			{
				continue;
			}

			const bool bMatchesSoundtrack =
				SpawnNode->NodeComment == TMMainMenuSoundtrackPatchComment
				|| TMSpawnSound2DUsesSound(SpawnNode, Sound);
			if (bMatchesSoundtrack && TMFindSetSoundtrackAfterSpawn(SpawnNode))
			{
				return SpawnNode;
			}
		}

		return nullptr;
	}

	UEdGraphNode* TMFindWidgetEventConstructNode(UEdGraph* Graph)
	{
		if (!Graph)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			const UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
			if (!EventNode)
			{
				continue;
			}

			const FString Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
			const FString EventName = EventNode->EventReference.GetMemberName().ToString();
			if (Title.Equals(TEXT("Event Construct"), ESearchCase::IgnoreCase)
				|| EventName.Equals(TEXT("Construct"), ESearchCase::IgnoreCase))
			{
				return Node;
			}
		}

		return nullptr;
	}

	bool TMExecPinHasLinkToNode(const UEdGraphPin* SourcePin, const UEdGraphNode* TargetNode)
	{
		if (!SourcePin || !TargetNode)
		{
			return false;
		}

		for (const UEdGraphPin* LinkedPin : SourcePin->LinkedTo)
		{
			if (LinkedPin && LinkedPin->GetOwningNode() == TargetNode)
			{
				return true;
			}
		}

		return false;
	}

	bool TMMainMenuAlreadyStartsSoundtrack(UEdGraph* EventGraph, const UObject* Sound)
	{
		return TMFindMainMenuSoundtrackStartSpawn(EventGraph, Sound) != nullptr;
	}

	bool TMSetSpawnSound2DPinDefaults(UK2Node_CallFunction* SpawnNode, const FTMSpawnSound2DSettings& Settings)
	{
		if (!SpawnNode)
		{
			return false;
		}

		bool bSuccess = true;
		if (UEdGraphPin* SoundPin = TMFindPinByName(SpawnNode, TEXT("Sound"), EGPD_Input))
		{
			TMSetGraphObjectPinDefault(SoundPin, Settings.Sound);
		}
		else
		{
			bSuccess = false;
		}

		TMSetLinkedPinDefault(
			TMFindPinByName(SpawnNode, TEXT("VolumeMultiplier"), EGPD_Input),
			Settings.VolumeMultiplier);
		TMSetLinkedPinDefault(
			TMFindPinByName(SpawnNode, TEXT("PitchMultiplier"), EGPD_Input),
			Settings.PitchMultiplier);
		TMSetLinkedPinDefault(
			TMFindPinByName(SpawnNode, TEXT("StartTime"), EGPD_Input),
			Settings.StartTime);
		if (Settings.ConcurrencySettings)
		{
			TMSetGraphObjectPinDefault(
				TMFindPinByName(SpawnNode, TEXT("ConcurrencySettings"), EGPD_Input),
				Settings.ConcurrencySettings);
		}
		TMSetLinkedPinDefault(
			TMFindPinByName(SpawnNode, TEXT("bPersistAcrossLevelTransition"), EGPD_Input),
			Settings.bPersistAcrossLevelTransition);
		TMSetLinkedPinDefault(
			TMFindPinByName(SpawnNode, TEXT("bAutoDestroy"), EGPD_Input),
			Settings.bAutoDestroy);

		return bSuccess;
	}

	bool TMPatchIntroSoundtrackExecBypass(
		UBlueprint* Blueprint,
		const UObject* Sound,
		FTMSpawnSound2DSettings& OutSettings,
		bool& bChanged)
	{
		UEdGraph* EventGraph = TMFindGraphByName(Blueprint, TEXT("EventGraph"));
		UK2Node_CallFunction* SpawnNode = TMFindSpawnSound2DForSound(EventGraph, Sound);
		if (!Blueprint || !EventGraph || !SpawnNode)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMenuSoundtrackMove] Failed to find W_Intro MainCulto SpawnSound2D."));
			return false;
		}

		if (!TMExtractSpawnSound2DSettings(Blueprint, SpawnNode, OutSettings))
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMenuSoundtrackMove] Failed to extract W_Intro SpawnSound2D settings."));
			return false;
		}

		UEdGraphPin* SpawnExecPin = TMFindPinByName(SpawnNode, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		UEdGraphPin* SpawnThenPin = TMFindPinByName(SpawnNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
		UK2Node_VariableSet* SetSoundtrackNode = nullptr;
		for (UEdGraphPin* LinkedPin : SpawnThenPin ? SpawnThenPin->LinkedTo : TArray<UEdGraphPin*>())
		{
			UK2Node_VariableSet* CandidateNode = Cast<UK2Node_VariableSet>(
				LinkedPin ? LinkedPin->GetOwningNode() : nullptr);
			if (CandidateNode
				&& CandidateNode->VariableReference.GetMemberName() == TEXT("Soundtrack"))
			{
				SetSoundtrackNode = CandidateNode;
				break;
			}
		}

		UEdGraphPin* SetThenPin = TMFindPinByName(SetSoundtrackNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
		if (!SpawnExecPin || !SetSoundtrackNode || !SetThenPin)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMMenuSoundtrackMove] Incomplete W_Intro soundtrack chain. SpawnExec=%d SetNode=%d SetThen=%d"),
				SpawnExecPin ? 1 : 0,
				SetSoundtrackNode ? 1 : 0,
				SetThenPin ? 1 : 0);
			return false;
		}

		TArray<UEdGraphPin*> PreviousExecPins = SpawnExecPin->LinkedTo;
		TArray<UEdGraphPin*> NextExecPins = SetThenPin->LinkedTo;
		if (PreviousExecPins.IsEmpty())
		{
			UE_LOG(LogTemp, Display, TEXT("[TMMenuSoundtrackMove] W_Intro SpawnSound2D already has no exec input."));
			return true;
		}

		const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(EventGraph->GetSchema());
		if (!Schema || NextExecPins.IsEmpty())
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMMenuSoundtrackMove] Cannot bypass W_Intro soundtrack chain. Schema=%d NextExecPins=%d"),
				Schema ? 1 : 0,
				NextExecPins.Num());
			return false;
		}

		EventGraph->Modify();
		for (UEdGraphPin* PreviousExecPin : PreviousExecPins)
		{
			if (!PreviousExecPin)
			{
				continue;
			}

			PreviousExecPin->Modify();
			PreviousExecPin->BreakLinkTo(SpawnExecPin);
			for (UEdGraphPin* NextExecPin : NextExecPins)
			{
				if (NextExecPin && !PreviousExecPin->LinkedTo.Contains(NextExecPin))
				{
					Schema->TryCreateConnection(PreviousExecPin, NextExecPin);
				}
			}
		}

		bChanged = true;
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMMenuSoundtrackMove] Bypassed W_Intro SpawnSound2D. PreviousExecPins=%d NextExecPins=%d Sound=%s Volume=%s Pitch=%s StartTime=%s Concurrency=%s Persist=%s AutoDestroy=%s"),
			PreviousExecPins.Num(),
			NextExecPins.Num(),
			*GetPathNameSafe(OutSettings.Sound),
			*OutSettings.VolumeMultiplier,
			*OutSettings.PitchMultiplier,
			*OutSettings.StartTime,
			*GetPathNameSafe(OutSettings.ConcurrencySettings),
			*OutSettings.bPersistAcrossLevelTransition,
			*OutSettings.bAutoDestroy);
		return true;
	}

	bool TMPatchMainMenuSoundtrackStart(
		UBlueprint* Blueprint,
		const FTMSpawnSound2DSettings& Settings,
		bool& bChanged)
	{
		UEdGraph* EventGraph = TMFindGraphByName(Blueprint, TEXT("EventGraph"));
		UEdGraphNode* EventConstructNode = TMFindWidgetEventConstructNode(EventGraph);
		UEdGraphPin* EventThenPin = TMFindPinByName(EventConstructNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
		const UEdGraphSchema_K2* Schema = EventGraph ? Cast<UEdGraphSchema_K2>(EventGraph->GetSchema()) : nullptr;
		if (!Blueprint || !EventGraph || !EventConstructNode || !EventThenPin || !Schema || !Settings.Sound)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMMenuSoundtrackMove] Cannot patch W_MainMenu soundtrack start. EventGraph=%d EventConstruct=%d EventThen=%d Schema=%d Sound=%d"),
				EventGraph ? 1 : 0,
				EventConstructNode ? 1 : 0,
				EventThenPin ? 1 : 0,
				Schema ? 1 : 0,
				Settings.Sound ? 1 : 0);
			return false;
		}

		if (TMMainMenuAlreadyStartsSoundtrack(EventGraph, Settings.Sound))
		{
			UE_LOG(LogTemp, Display, TEXT("[TMMenuSoundtrackMove] W_MainMenu Event Construct already starts MainCulto."));
			return true;
		}

		TArray<UEdGraphPin*> PreviousThenTargets = EventThenPin->LinkedTo;
		EventGraph->Modify();

		UFunction* SpawnSound2DFunction = UGameplayStatics::StaticClass()->FindFunctionByName(
			GET_FUNCTION_NAME_CHECKED(UGameplayStatics, SpawnSound2D));
		if (!SpawnSound2DFunction)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMenuSoundtrackMove] SpawnSound2D function was not found."));
			return false;
		}

		FGraphNodeCreator<UK2Node_CallFunction> SpawnCreator(*EventGraph);
		UK2Node_CallFunction* SpawnNode = SpawnCreator.CreateNode();
		SpawnNode->SetFromFunction(SpawnSound2DFunction);
		SpawnNode->NodePosX = EventConstructNode->NodePosX + 320;
		SpawnNode->NodePosY = EventConstructNode->NodePosY - 80;
		SpawnNode->NodeComment = TMMainMenuSoundtrackPatchComment;
		SpawnCreator.Finalize();
		SpawnNode->ReconstructNode();

		FGraphNodeCreator<UK2Node_VariableSet> SetCreator(*EventGraph);
		UK2Node_VariableSet* SetSoundtrackNode = SetCreator.CreateNode();
		SetSoundtrackNode->VariableReference.SetSelfMember(TEXT("Soundtrack"));
		SetSoundtrackNode->NodePosX = SpawnNode->NodePosX + 360;
		SetSoundtrackNode->NodePosY = SpawnNode->NodePosY;
		SetSoundtrackNode->NodeComment = TMMainMenuSoundtrackPatchComment;
		SetCreator.Finalize();
		SetSoundtrackNode->ReconstructNode();

		if (!TMSetSpawnSound2DPinDefaults(SpawnNode, Settings))
		{
			EventGraph->RemoveNode(SpawnNode);
			EventGraph->RemoveNode(SetSoundtrackNode);
			UE_LOG(LogTemp, Error, TEXT("[TMMenuSoundtrackMove] Failed to set W_MainMenu SpawnSound2D pin defaults."));
			return false;
		}

		UEdGraphPin* SpawnExecPin = TMFindPinByName(SpawnNode, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		UEdGraphPin* SpawnThenPin = TMFindPinByName(SpawnNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
		UEdGraphPin* SpawnReturnPin = TMFindPinByName(SpawnNode, TEXT("ReturnValue"), EGPD_Output);
		UEdGraphPin* SetExecPin = TMFindPinByName(SetSoundtrackNode, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		UEdGraphPin* SetThenPin = TMFindPinByName(SetSoundtrackNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
		UEdGraphPin* SetValuePin = TMFindPinByName(SetSoundtrackNode, TEXT("Soundtrack"), EGPD_Input);
		if (!SpawnExecPin || !SpawnThenPin || !SpawnReturnPin || !SetExecPin || !SetThenPin || !SetValuePin)
		{
			EventGraph->RemoveNode(SpawnNode);
			EventGraph->RemoveNode(SetSoundtrackNode);
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMMenuSoundtrackMove] Failed to find W_MainMenu soundtrack pins. SpawnExec=%d SpawnThen=%d Return=%d SetExec=%d SetThen=%d SetValue=%d"),
				SpawnExecPin ? 1 : 0,
				SpawnThenPin ? 1 : 0,
				SpawnReturnPin ? 1 : 0,
				SetExecPin ? 1 : 0,
				SetThenPin ? 1 : 0,
				SetValuePin ? 1 : 0);
			return false;
		}

		EventThenPin->Modify();
		EventThenPin->BreakAllPinLinks(false);

		bool bConnected = true;
		bConnected &= Schema->TryCreateConnection(EventThenPin, SpawnExecPin);
		bConnected &= Schema->TryCreateConnection(SpawnThenPin, SetExecPin);
		bConnected &= Schema->TryCreateConnection(SpawnReturnPin, SetValuePin);
		for (UEdGraphPin* PreviousThenTarget : PreviousThenTargets)
		{
			if (PreviousThenTarget)
			{
				bConnected &= Schema->TryCreateConnection(SetThenPin, PreviousThenTarget);
			}
		}

		if (!bConnected)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMenuSoundtrackMove] Failed to connect the W_MainMenu soundtrack chain."));
			return false;
		}

		bChanged = true;
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMMenuSoundtrackMove] Inserted W_MainMenu SpawnSound2D before existing Event Construct chain. OldThenTargets=%d Sound=%s Volume=%s Pitch=%s StartTime=%s Concurrency=%s Persist=%s AutoDestroy=%s"),
			PreviousThenTargets.Num(),
			*GetPathNameSafe(Settings.Sound),
			*Settings.VolumeMultiplier,
			*Settings.PitchMultiplier,
			*Settings.StartTime,
			*GetPathNameSafe(Settings.ConcurrencySettings),
			*Settings.bPersistAcrossLevelTransition,
			*Settings.bAutoDestroy);
		return true;
	}

	bool TMMainMenuSoundtrackStartAlreadyGuarded(
		UEdGraph* EventGraph,
		UK2Node_CallFunction* SpawnNode)
	{
		UEdGraphNode* EventConstructNode = TMFindWidgetEventConstructNode(EventGraph);
		UEdGraphPin* EventThenPin = TMFindPinByName(EventConstructNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
		UEdGraphPin* SpawnExecPin = TMFindPinByName(SpawnNode, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		if (!EventThenPin || !SpawnExecPin)
		{
			return false;
		}

		for (const UEdGraphPin* LinkedPin : SpawnExecPin->LinkedTo)
		{
			const UK2Node_IfThenElse* BranchNode = Cast<UK2Node_IfThenElse>(
				LinkedPin ? LinkedPin->GetOwningNode() : nullptr);
			if (!BranchNode)
			{
				continue;
			}

			const UEdGraphPin* BranchExecPin = TMFindPinByName(
				const_cast<UK2Node_IfThenElse*>(BranchNode),
				UEdGraphSchema_K2::PN_Execute,
				EGPD_Input);
			if (BranchNode->NodeComment.Contains(TEXT("TM: guard MainCulto"), ESearchCase::IgnoreCase)
				&& BranchExecPin
				&& BranchExecPin->LinkedTo.Contains(EventThenPin))
			{
				return true;
			}
		}

		return false;
	}

	bool TMPatchMainMenuSoundtrackConstructGuard(
		UBlueprint* Blueprint,
		const UObject* Sound,
		bool& bChanged)
	{
		UEdGraph* EventGraph = TMFindGraphByName(Blueprint, TEXT("EventGraph"));
		UEdGraphNode* EventConstructNode = TMFindWidgetEventConstructNode(EventGraph);
		UEdGraphPin* EventThenPin = TMFindPinByName(EventConstructNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
		const UEdGraphSchema_K2* Schema = EventGraph ? Cast<UEdGraphSchema_K2>(EventGraph->GetSchema()) : nullptr;
		UK2Node_CallFunction* SpawnNode = TMFindMainMenuSoundtrackStartSpawn(EventGraph, Sound);
		UK2Node_VariableSet* SetSoundtrackNode = TMFindSetSoundtrackAfterSpawn(SpawnNode);
		if (!Blueprint || !EventGraph || !EventConstructNode || !EventThenPin || !Schema || !SpawnNode || !SetSoundtrackNode)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMMenuSoundtrackMove] Cannot guard W_MainMenu soundtrack start. EventGraph=%d EventConstruct=%d EventThen=%d Schema=%d Spawn=%d SetSoundtrack=%d"),
				EventGraph ? 1 : 0,
				EventConstructNode ? 1 : 0,
				EventThenPin ? 1 : 0,
				Schema ? 1 : 0,
				SpawnNode ? 1 : 0,
				SetSoundtrackNode ? 1 : 0);
			return false;
		}

		if (TMMainMenuSoundtrackStartAlreadyGuarded(EventGraph, SpawnNode))
		{
			UE_LOG(LogTemp, Display, TEXT("[TMMenuSoundtrackMove] W_MainMenu soundtrack start already has Construct guard."));
			return true;
		}

		UEdGraphPin* SpawnExecPin = TMFindPinByName(SpawnNode, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		UEdGraphPin* SetThenPin = TMFindPinByName(SetSoundtrackNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
		if (!SpawnExecPin || !SetThenPin || SetThenPin->LinkedTo.IsEmpty())
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMMenuSoundtrackMove] Incomplete W_MainMenu soundtrack start for guard. SpawnExec=%d SetThen=%d SetThenTargets=%d"),
				SpawnExecPin ? 1 : 0,
				SetThenPin ? 1 : 0,
				SetThenPin ? SetThenPin->LinkedTo.Num() : 0);
			return false;
		}

		UFunction* IsValidFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
			GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, IsValid));
		if (!IsValidFunction)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMenuSoundtrackMove] UKismetSystemLibrary::IsValid was not found."));
			return false;
		}

		const TArray<UEdGraphPin*> PreviousSetThenTargets = SetThenPin->LinkedTo;
		EventGraph->Modify();

		FGraphNodeCreator<UK2Node_IfThenElse> BranchCreator(*EventGraph);
		UK2Node_IfThenElse* BranchNode = BranchCreator.CreateNode();
		BranchNode->NodePosX = EventConstructNode->NodePosX + 280;
		BranchNode->NodePosY = EventConstructNode->NodePosY - 120;
		BranchNode->NodeComment = TEXT("TM: guard MainCulto against repeated Construct");
		BranchCreator.Finalize();
		BranchNode->ReconstructNode();

		FGraphNodeCreator<UK2Node_VariableGet> GetCreator(*EventGraph);
		UK2Node_VariableGet* GetSoundtrackNode = GetCreator.CreateNode();
		GetSoundtrackNode->VariableReference.SetSelfMember(TEXT("Soundtrack"));
		GetSoundtrackNode->NodePosX = BranchNode->NodePosX - 260;
		GetSoundtrackNode->NodePosY = BranchNode->NodePosY + 160;
		GetSoundtrackNode->NodeComment = TEXT("TM: guard MainCulto against repeated Construct");
		GetCreator.Finalize();
		GetSoundtrackNode->ReconstructNode();

		FGraphNodeCreator<UK2Node_CallFunction> IsValidCreator(*EventGraph);
		UK2Node_CallFunction* IsValidNode = IsValidCreator.CreateNode();
		IsValidNode->SetFromFunction(IsValidFunction);
		IsValidNode->NodePosX = BranchNode->NodePosX;
		IsValidNode->NodePosY = BranchNode->NodePosY + 170;
		IsValidNode->NodeComment = TEXT("TM: guard MainCulto against repeated Construct");
		IsValidCreator.Finalize();
		IsValidNode->ReconstructNode();

		UEdGraphPin* BranchExecPin = TMFindPinByName(BranchNode, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		UEdGraphPin* BranchThenPin = TMFindPinByName(BranchNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
		UEdGraphPin* BranchElsePin = TMFindPinByName(BranchNode, UEdGraphSchema_K2::PN_Else, EGPD_Output);
		UEdGraphPin* BranchConditionPin = TMFindPinByName(BranchNode, TEXT("Condition"), EGPD_Input);
		UEdGraphPin* GetSoundtrackOutputPin = TMFindPinByName(GetSoundtrackNode, TEXT("Soundtrack"), EGPD_Output);
		UEdGraphPin* IsValidObjectPin = TMFindPinByName(IsValidNode, TEXT("Object"), EGPD_Input);
		UEdGraphPin* IsValidReturnPin = TMFindPinByName(IsValidNode, UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
		if (!BranchExecPin || !BranchThenPin || !BranchElsePin || !BranchConditionPin
			|| !GetSoundtrackOutputPin || !IsValidObjectPin || !IsValidReturnPin)
		{
			EventGraph->RemoveNode(BranchNode);
			EventGraph->RemoveNode(GetSoundtrackNode);
			EventGraph->RemoveNode(IsValidNode);
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMMenuSoundtrackMove] Failed to find guard pins. BranchExec=%d Then=%d Else=%d Condition=%d Get=%d IsValidObject=%d IsValidReturn=%d"),
				BranchExecPin ? 1 : 0,
				BranchThenPin ? 1 : 0,
				BranchElsePin ? 1 : 0,
				BranchConditionPin ? 1 : 0,
				GetSoundtrackOutputPin ? 1 : 0,
				IsValidObjectPin ? 1 : 0,
				IsValidReturnPin ? 1 : 0);
			return false;
		}

		SpawnExecPin->Modify();
		SpawnExecPin->BreakAllPinLinks(false);

		bool bConnected = true;
		bConnected &= Schema->TryCreateConnection(EventThenPin, BranchExecPin);
		bConnected &= Schema->TryCreateConnection(BranchElsePin, SpawnExecPin);
		bConnected &= Schema->TryCreateConnection(GetSoundtrackOutputPin, IsValidObjectPin);
		bConnected &= Schema->TryCreateConnection(IsValidReturnPin, BranchConditionPin);
		for (UEdGraphPin* PreviousSetThenTarget : PreviousSetThenTargets)
		{
			if (PreviousSetThenTarget)
			{
				bConnected &= Schema->TryCreateConnection(BranchThenPin, PreviousSetThenTarget);
			}
		}

		if (!bConnected)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMenuSoundtrackMove] Failed to connect W_MainMenu soundtrack Construct guard."));
			return false;
		}

		bChanged = true;
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMMenuSoundtrackMove] Guarded W_MainMenu MainCulto start against repeated Construct. ValidPathTargets=%d"),
			PreviousSetThenTargets.Num());
		return true;
	}

	bool TMCompileAndSaveBlueprintIfChanged(UBlueprint* Blueprint, const bool bChanged, const TCHAR* LogPrefix)
	{
		if (!Blueprint || !bChanged)
		{
			return true;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status == BS_Error)
		{
			UE_LOG(LogTemp, Error, TEXT("[%s] Blueprint compile failed: %s"), LogPrefix, *GetPathNameSafe(Blueprint));
			return false;
		}

		return TMSavePackageForAsset(Blueprint, LogPrefix);
	}

	bool TMInsertMainMenuBeatAnimationDelay()
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(
			nullptr,
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_MainMenu.W_MainMenu"));
		if (!Blueprint)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMainMenuBeatDelay] Failed to load W_MainMenu."));
			return false;
		}

		UEdGraph* EventGraph = TMFindGraphByName(Blueprint, TEXT("EventGraph"));
		const UEdGraphSchema_K2* Schema = EventGraph ? Cast<UEdGraphSchema_K2>(EventGraph->GetSchema()) : nullptr;
		if (!EventGraph || !Schema)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMainMenuBeatDelay] Missing EventGraph or schema."));
			return false;
		}

		UFunction* DelayFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
			GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, Delay));
		if (!DelayFunction)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMainMenuBeatDelay] UKismetSystemLibrary::Delay was not found."));
			return false;
		}

		auto IsPlayAnimationNode = [](const UK2Node_CallFunction* Node)
		{
			return Node
				&& Node->FunctionReference.GetMemberName() == GET_FUNCTION_NAME_CHECKED(UUserWidget, PlayAnimation);
		};

		auto IsDelayNode = [](const UK2Node_CallFunction* Node)
		{
			return Node
				&& Node->FunctionReference.GetMemberName() == GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, Delay);
		};

		auto FindPlayAnimationAfterThen = [&](UK2Node_CallFunction* Node, UK2Node_CallFunction*& OutDelayNode)
		{
			OutDelayNode = nullptr;
			UEdGraphPin* ThenPin = TMFindPinByName(Node, UEdGraphSchema_K2::PN_Then, EGPD_Output);
			if (!ThenPin)
			{
				return static_cast<UK2Node_CallFunction*>(nullptr);
			}

			for (UEdGraphPin* LinkedThenPin : ThenPin->LinkedTo)
			{
				UK2Node_CallFunction* LinkedCallNode = Cast<UK2Node_CallFunction>(
					LinkedThenPin ? LinkedThenPin->GetOwningNode() : nullptr);
				if (IsPlayAnimationNode(LinkedCallNode))
				{
					return LinkedCallNode;
				}

				if (!IsDelayNode(LinkedCallNode))
				{
					continue;
				}

				UEdGraphPin* DelayThenPin = TMFindPinByName(LinkedCallNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
				if (!DelayThenPin)
				{
					continue;
				}

				for (UEdGraphPin* LinkedDelayThenPin : DelayThenPin->LinkedTo)
				{
					UK2Node_CallFunction* PlayNode = Cast<UK2Node_CallFunction>(
						LinkedDelayThenPin ? LinkedDelayThenPin->GetOwningNode() : nullptr);
					if (IsPlayAnimationNode(PlayNode))
					{
						OutDelayNode = LinkedCallNode;
						return PlayNode;
					}
				}
			}

			return static_cast<UK2Node_CallFunction*>(nullptr);
		};

		auto IsAnimIndexDrivenPlayAnimation = [&](UK2Node_CallFunction* Node)
		{
			UEdGraphPin* ExecutePin = TMFindPinByName(Node, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
			return ExecutePin
				&& ExecutePin->LinkedTo.ContainsByPredicate([](const UEdGraphPin* LinkedPin)
				{
					const UK2Node_VariableSet* SetNode = Cast<UK2Node_VariableSet>(
						LinkedPin ? LinkedPin->GetOwningNode() : nullptr);
					return SetNode && SetNode->VariableReference.GetMemberName() == TEXT("AnimIndex");
				});
		};

		auto IsMainAnimPlayAnimation = [&](UK2Node_CallFunction* Node)
		{
			if (!IsPlayAnimationNode(Node))
			{
				return false;
			}

			UEdGraphPin* AnimationPin = TMFindPinByName(Node, TEXT("InAnimation"), EGPD_Input);
			return AnimationPin
				&& AnimationPin->LinkedTo.ContainsByPredicate([](const UEdGraphPin* LinkedPin)
				{
					const UK2Node_VariableGet* GetNode = Cast<UK2Node_VariableGet>(
						LinkedPin ? LinkedPin->GetOwningNode() : nullptr);
					return GetNode && GetNode->VariableReference.GetMemberName() == TEXT("MainAnim");
				});
		};

		auto SetDelayDuration = [&](UK2Node_CallFunction* DelayNode)
		{
			UEdGraphPin* DurationPin = TMFindPinByName(DelayNode, TEXT("Duration"), EGPD_Input);
			if (!DurationPin)
			{
				return false;
			}

			if (DurationPin->DefaultValue != TEXT("0.250000"))
			{
				DurationPin->Modify();
				DurationPin->DefaultValue = TEXT("0.250000");
			}

			return true;
		};

		auto CreateEighthDelayNode = [&](UK2Node_CallFunction* SourceNode, const int32 OffsetX, const int32 OffsetY)
		{
			FGraphNodeCreator<UK2Node_CallFunction> DelayCreator(*EventGraph);
			UK2Node_CallFunction* DelayNode = DelayCreator.CreateNode();
			DelayNode->SetFromFunction(DelayFunction);
			DelayNode->NodePosX = SourceNode ? SourceNode->NodePosX + OffsetX : 0;
			DelayNode->NodePosY = SourceNode ? SourceNode->NodePosY + OffsetY : 0;
			DelayNode->NodeComment = TEXT("TM: eighth-note gap between main menu beat animation hits");
			DelayCreator.Finalize();
			DelayNode->ReconstructNode();

			UEdGraphPin* DelayExecPin = TMFindPinByName(DelayNode, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
			UEdGraphPin* DelayThenPin = TMFindPinByName(DelayNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
			if (!DelayExecPin || !DelayThenPin || !SetDelayDuration(DelayNode))
			{
				UE_LOG(LogTemp, Error, TEXT("[TMMainMenuBeatDelay] Failed to create a valid Delay node."));
				EventGraph->RemoveNode(DelayNode);
				return static_cast<UK2Node_CallFunction*>(nullptr);
			}

			return DelayNode;
		};

		auto EnsureDelayBetween = [&](UK2Node_CallFunction* FromNode, UK2Node_CallFunction* ToNode, const int32 OffsetX, const int32 OffsetY, bool& bOutChanged)
		{
			UEdGraphPin* FromThenPin = TMFindPinByName(FromNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
			UEdGraphPin* ToExecPin = TMFindPinByName(ToNode, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
			if (!FromThenPin || !ToExecPin)
			{
				UE_LOG(LogTemp, Error, TEXT("[TMMainMenuBeatDelay] Missing exec pins between %s and %s."),
					*GetNameSafe(FromNode),
					*GetNameSafe(ToNode));
				return false;
			}

			for (UEdGraphPin* LinkedThenPin : FromThenPin->LinkedTo)
			{
				UK2Node_CallFunction* ExistingDelay = Cast<UK2Node_CallFunction>(
					LinkedThenPin ? LinkedThenPin->GetOwningNode() : nullptr);
				if (!IsDelayNode(ExistingDelay))
				{
					continue;
				}

				UEdGraphPin* ExistingDelayThenPin = TMFindPinByName(ExistingDelay, UEdGraphSchema_K2::PN_Then, EGPD_Output);
				if (ExistingDelayThenPin && ExistingDelayThenPin->LinkedTo.Contains(ToExecPin))
				{
					return SetDelayDuration(ExistingDelay);
				}
			}

			UK2Node_CallFunction* DelayNode = CreateEighthDelayNode(FromNode, OffsetX, OffsetY);
			if (!DelayNode)
			{
				return false;
			}

			UEdGraphPin* DelayExecPin = TMFindPinByName(DelayNode, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
			UEdGraphPin* DelayThenPin = TMFindPinByName(DelayNode, UEdGraphSchema_K2::PN_Then, EGPD_Output);
			FromThenPin->Modify();
			ToExecPin->Modify();
			FromThenPin->BreakLinkTo(ToExecPin);

			bool bConnected = true;
			bConnected &= Schema->TryCreateConnection(FromThenPin, DelayExecPin);
			bConnected &= Schema->TryCreateConnection(DelayThenPin, ToExecPin);
			if (!bConnected)
			{
				UE_LOG(LogTemp, Error, TEXT("[TMMainMenuBeatDelay] Failed to connect Delay between %s and %s."),
					*GetNameSafe(FromNode),
					*GetNameSafe(ToNode));
				return false;
			}

			bOutChanged = true;
			UE_LOG(LogTemp, Display, TEXT("[TMMainMenuBeatDelay] Inserted 0.25s Delay between %s and %s."),
				*GetNameSafe(FromNode),
				*GetNameSafe(ToNode));
			return true;
		};

		UK2Node_CallFunction* FirstPlayNode = nullptr;
		UK2Node_CallFunction* SecondPlayNode = nullptr;
		UK2Node_CallFunction* ExistingFirstDelayNode = nullptr;
		for (UEdGraphNode* Node : EventGraph->Nodes)
		{
			UK2Node_CallFunction* Candidate = Cast<UK2Node_CallFunction>(Node);
			if (!IsPlayAnimationNode(Candidate) || !IsAnimIndexDrivenPlayAnimation(Candidate))
			{
				continue;
			}

			UK2Node_CallFunction* DelayNode = nullptr;
			if (UK2Node_CallFunction* LinkedPlayNode = FindPlayAnimationAfterThen(Candidate, DelayNode))
			{
				FirstPlayNode = Candidate;
				SecondPlayNode = LinkedPlayNode;
				ExistingFirstDelayNode = DelayNode;
				break;
			}
		}

		if (!FirstPlayNode || !SecondPlayNode)
		{
			UE_LOG(LogTemp, Display, TEXT("[TMMainMenuBeatDelay] Did not find the OnBeat PlayAnimation pair to patch."));
			return true;
		}

		UK2Node_CallFunction* ExistingSecondDelayNode = nullptr;
		UK2Node_CallFunction* ThirdPlayNode = FindPlayAnimationAfterThen(SecondPlayNode, ExistingSecondDelayNode);
		if (!ThirdPlayNode)
		{
			for (UEdGraphNode* Node : EventGraph->Nodes)
			{
				UK2Node_CallFunction* Candidate = Cast<UK2Node_CallFunction>(Node);
				if (Candidate != FirstPlayNode
					&& Candidate != SecondPlayNode
					&& IsMainAnimPlayAnimation(Candidate))
				{
					ThirdPlayNode = Candidate;
					break;
				}
			}
		}

		if (!ThirdPlayNode)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMainMenuBeatDelay] Did not find a MainAnim PlayAnimation node for the third hit."));
			return false;
		}

		EventGraph->Modify();
		bool bChanged = false;
		if (ExistingFirstDelayNode)
		{
			if (!SetDelayDuration(ExistingFirstDelayNode))
			{
				return false;
			}
		}
		else if (!EnsureDelayBetween(FirstPlayNode, SecondPlayNode, 340, 40, bChanged))
		{
			return false;
		}

		if (ExistingSecondDelayNode)
		{
			if (!SetDelayDuration(ExistingSecondDelayNode))
			{
				return false;
			}
		}
		else if (!EnsureDelayBetween(SecondPlayNode, ThirdPlayNode, 340, 120, bChanged))
		{
			return false;
		}

		UE_LOG(LogTemp, Display, TEXT("[TMMainMenuBeatDelay] Ensured three-hit eighth-note animation chain: %s -> %s -> %s."),
			*GetNameSafe(FirstPlayNode),
			*GetNameSafe(SecondPlayNode),
			*GetNameSafe(ThirdPlayNode));
		return TMCompileAndSaveBlueprintIfChanged(Blueprint, bChanged, TEXT("TMMainMenuBeatDelay"));
	}

	bool TMPatchMenuSoundtrackStartToMain()
	{
		USoundBase* MainMenuSoundtrack = LoadObject<USoundBase>(nullptr, TMMainMenuSoundtrackPath);
		if (!MainMenuSoundtrack)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMenuSoundtrackMove] Missing main menu soundtrack: %s"), TMMainMenuSoundtrackPath);
			return false;
		}

		UBlueprint* IntroBlueprint = LoadObject<UBlueprint>(
			nullptr,
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Intro.W_Intro"));
		UBlueprint* MainMenuBlueprint = LoadObject<UBlueprint>(
			nullptr,
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_MainMenu.W_MainMenu"));
		if (!IntroBlueprint || !MainMenuBlueprint)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMMenuSoundtrackMove] Failed to load widgets. W_Intro=%d W_MainMenu=%d"),
				IntroBlueprint ? 1 : 0,
				MainMenuBlueprint ? 1 : 0);
			return false;
		}

		FTMSpawnSound2DSettings Settings;
		bool bIntroChanged = false;
		bool bMainMenuChanged = false;
		bool bSuccess = TMPatchIntroSoundtrackExecBypass(
			IntroBlueprint,
			MainMenuSoundtrack,
			Settings,
			bIntroChanged);
		bSuccess &= TMPatchMainMenuSoundtrackStart(
			MainMenuBlueprint,
			Settings,
			bMainMenuChanged);
		bSuccess &= TMPatchMainMenuSoundtrackConstructGuard(
			MainMenuBlueprint,
			MainMenuSoundtrack,
			bMainMenuChanged);

		if (!bSuccess)
		{
			return false;
		}

		bSuccess &= TMCompileAndSaveBlueprintIfChanged(IntroBlueprint, bIntroChanged, TEXT("TMMenuSoundtrackMove"));
		bSuccess &= TMCompileAndSaveBlueprintIfChanged(MainMenuBlueprint, bMainMenuChanged, TEXT("TMMenuSoundtrackMove"));
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMMenuSoundtrackMove] Summary: IntroChanged=%d MainMenuChanged=%d"),
			bIntroChanged ? 1 : 0,
			bMainMenuChanged ? 1 : 0);
		return bSuccess;
	}

	bool TMPatchWidgetSoundtrackDefault(
		const TCHAR* BlueprintPath,
		USoundBase* Soundtrack,
		const TCHAR* LogPrefix)
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, BlueprintPath);
		if (!Blueprint || !Blueprint->GeneratedClass)
		{
			UE_LOG(LogTemp, Error, TEXT("[%s] Failed to load widget blueprint: %s"), LogPrefix, BlueprintPath);
			return false;
		}

		UObject* DefaultObject = Blueprint->GeneratedClass->GetDefaultObject();
		FObjectPropertyBase* SoundtrackProperty = DefaultObject
			? FindFProperty<FObjectPropertyBase>(DefaultObject->GetClass(), TEXT("Soundtrack"))
			: nullptr;
		if (!SoundtrackProperty)
		{
			UE_LOG(LogTemp, Error, TEXT("[%s] Missing object property Soundtrack on %s."), LogPrefix, *GetNameSafe(DefaultObject));
			return false;
		}

		UObject* CurrentValue = SoundtrackProperty->GetObjectPropertyValue_InContainer(DefaultObject);
		bool bChanged = false;
		if (UAudioComponent* SoundtrackComponent = Cast<UAudioComponent>(CurrentValue))
		{
			if (!TMSetAudioComponentSoundIfDifferent(SoundtrackComponent, Soundtrack, LogPrefix, bChanged))
			{
				return false;
			}
		}
		else if (!TMSetObjectPropertyIfDifferent(DefaultObject, TEXT("Soundtrack"), Soundtrack, LogPrefix, bChanged))
		{
			return false;
		}

		if (!bChanged)
		{
			return true;
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status == BS_Error)
		{
			UE_LOG(LogTemp, Error, TEXT("[%s] Blueprint compile failed: %s"), LogPrefix, *Blueprint->GetPathName());
			return false;
		}

		return TMSavePackageForAsset(Blueprint, LogPrefix);
	}

	bool TMPatchGunFeedbackSoundDefault(
		UBlueprint* Blueprint,
		USoundBase* WeaponSpawnSound,
		USoundBase* AttachmentSound,
		int32& OutChangedBlueprintCount)
	{
		if (!Blueprint || !Blueprint->GeneratedClass || !Blueprint->GeneratedClass->IsChildOf(AGun::StaticClass()))
		{
			return true;
		}

		AGun* DefaultGun = Cast<AGun>(Blueprint->GeneratedClass->GetDefaultObject());
		if (!DefaultGun)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMenuSoundtrackFeedback] Missing AGun CDO for %s."), *Blueprint->GetPathName());
			return false;
		}

		bool bChanged = false;
		bool bSuccess = true;
		bSuccess &= TMSetObjectPropertyIfDifferent(
			DefaultGun,
			TEXT("WeaponSpawnFeedbackSound"),
			WeaponSpawnSound,
			TEXT("TMMenuSoundtrackFeedback"),
			bChanged);
		bSuccess &= TMSetObjectPropertyIfDifferent(
			DefaultGun,
			TEXT("AttachmentFeedbackSound"),
			AttachmentSound,
			TEXT("TMMenuSoundtrackFeedback"),
			bChanged);

		if (!bSuccess || !bChanged)
		{
			return bSuccess;
		}

		++OutChangedBlueprintCount;
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status == BS_Error)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMenuSoundtrackFeedback] Blueprint compile failed: %s"), *Blueprint->GetPathName());
			return false;
		}

		return TMSavePackageForAsset(Blueprint, TEXT("TMMenuSoundtrackFeedback"));
	}

	bool TMPatchGunAttachmentFeedbackFXDefault(
		UBlueprint* Blueprint,
		UFXSystemAsset* AttachmentFX,
		const FVector& AttachmentScale,
		int32& OutChangedBlueprintCount)
	{
		if (!Blueprint || !Blueprint->GeneratedClass || !Blueprint->GeneratedClass->IsChildOf(AGun::StaticClass()))
		{
			return true;
		}

		AGun* DefaultGun = Cast<AGun>(Blueprint->GeneratedClass->GetDefaultObject());
		if (!DefaultGun)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMAttachmentFeedbackFX] Missing AGun CDO for %s."), *Blueprint->GetPathName());
			return false;
		}

		bool bChanged = false;
		bool bSuccess = true;
		bSuccess &= TMSetObjectPropertyIfDifferent(
			DefaultGun,
			TEXT("AttachmentFeedbackFX"),
			AttachmentFX,
			TEXT("TMAttachmentFeedbackFX"),
			bChanged);
		bSuccess &= TMSetVectorPropertyIfDifferent(
			DefaultGun,
			TEXT("AttachmentFeedbackScale"),
			AttachmentScale,
			TEXT("TMAttachmentFeedbackFX"),
			bChanged);

		if (!bSuccess || !bChanged)
		{
			return bSuccess;
		}

		++OutChangedBlueprintCount;
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status == BS_Error)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMAttachmentFeedbackFX] Blueprint compile failed: %s"), *Blueprint->GetPathName());
			return false;
		}

		return TMSavePackageForAsset(Blueprint, TEXT("TMAttachmentFeedbackFX"));
	}

	bool TMPatchGunFeedbackSoundDefaultByPath(
		const TCHAR* BlueprintPath,
		USoundBase* WeaponSpawnSound,
		USoundBase* AttachmentSound,
		int32& OutLoadedBlueprintCount,
		int32& OutGunBlueprintCount,
		int32& OutChangedBlueprintCount)
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, BlueprintPath);
		if (!Blueprint)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMMenuSoundtrackFeedback] Failed to load blueprint: %s"), BlueprintPath);
			return false;
		}

		++OutLoadedBlueprintCount;
		if (Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(AGun::StaticClass()))
		{
			++OutGunBlueprintCount;
		}

		return TMPatchGunFeedbackSoundDefault(
			Blueprint,
			WeaponSpawnSound,
			AttachmentSound,
			OutChangedBlueprintCount);
	}

	bool TMPatchGunAttachmentFeedbackFXDefaultByPath(
		const TCHAR* BlueprintPath,
		UFXSystemAsset* AttachmentFX,
		const FVector& AttachmentScale,
		int32& OutLoadedBlueprintCount,
		int32& OutGunBlueprintCount,
		int32& OutChangedBlueprintCount)
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, BlueprintPath);
		if (!Blueprint)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMAttachmentFeedbackFX] Failed to load blueprint: %s"), BlueprintPath);
			return false;
		}

		++OutLoadedBlueprintCount;
		if (Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(AGun::StaticClass()))
		{
			++OutGunBlueprintCount;
		}

		return TMPatchGunAttachmentFeedbackFXDefault(
			Blueprint,
			AttachmentFX,
			AttachmentScale,
			OutChangedBlueprintCount);
	}

	bool TMPatchMenuSoundtrackOnly()
	{
		return TMPatchMenuSoundtrackStartToMain();
	}

	bool TMPatchAttachmentFeedbackFX()
	{
		UFXSystemAsset* AttachmentFX = LoadObject<UFXSystemAsset>(
			nullptr,
			TEXT("/Game/NiagaraExamples/FX_Misc/NS_HitDissolve.NS_HitDissolve"));
		if (!AttachmentFX)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMAttachmentFeedbackFX] Failed to load NS_HitDissolve."));
			return false;
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		AssetRegistry.SearchAllAssets(true);

		FARFilter Filter;
		Filter.PackagePaths.Add(FName(TEXT("/Game/MP_System_V3/Game/Weapons")));
		Filter.ClassPaths.Add(FTopLevelAssetPath(FName(TEXT("/Script/Engine")), FName(TEXT("Blueprint"))));
		Filter.bRecursivePaths = true;
		Filter.bRecursiveClasses = true;

		TArray<FAssetData> BlueprintAssets;
		AssetRegistry.GetAssets(Filter, BlueprintAssets);
		BlueprintAssets.Sort([](const FAssetData& Left, const FAssetData& Right)
		{
			return Left.GetSoftObjectPath().ToString() < Right.GetSoftObjectPath().ToString();
		});

		const FVector AttachmentScale(0.2f);
		int32 LoadedBlueprintCount = 0;
		int32 GunBlueprintCount = 0;
		int32 ChangedGunBlueprintCount = 0;
		bool bSuccess = true;

		bSuccess &= TMPatchGunAttachmentFeedbackFXDefaultByPath(
			TEXT("/Game/MP_System_V3/Game/Blueprints/Core/BP_Weapon_Master.BP_Weapon_Master"),
			AttachmentFX,
			AttachmentScale,
			LoadedBlueprintCount,
			GunBlueprintCount,
			ChangedGunBlueprintCount);
		bSuccess &= TMPatchGunAttachmentFeedbackFXDefaultByPath(
			TEXT("/Game/MP_System_V3/Game/Blueprints/Core/BP_InteractionMaster.BP_InteractionMaster"),
			AttachmentFX,
			AttachmentScale,
			LoadedBlueprintCount,
			GunBlueprintCount,
			ChangedGunBlueprintCount);
		bSuccess &= TMPatchGunAttachmentFeedbackFXDefaultByPath(
			TEXT("/Game/MP_System_V3/Game/Blueprints/Core/BP_Magazine_Master.BP_Magazine_Master"),
			AttachmentFX,
			AttachmentScale,
			LoadedBlueprintCount,
			GunBlueprintCount,
			ChangedGunBlueprintCount);

		for (const FAssetData& BlueprintAsset : BlueprintAssets)
		{
			UBlueprint* Blueprint = Cast<UBlueprint>(BlueprintAsset.GetAsset());
			if (!Blueprint)
			{
				continue;
			}

			++LoadedBlueprintCount;
			if (Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(AGun::StaticClass()))
			{
				++GunBlueprintCount;
			}

			bSuccess &= TMPatchGunAttachmentFeedbackFXDefault(
				Blueprint,
				AttachmentFX,
				AttachmentScale,
				ChangedGunBlueprintCount);
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMAttachmentFeedbackFX] Summary: LoadedBlueprints=%d GunBlueprints=%d ChangedGunBlueprints=%d FX=%s Scale=%s Success=%d"),
			LoadedBlueprintCount,
			GunBlueprintCount,
			ChangedGunBlueprintCount,
			*AttachmentFX->GetPathName(),
			*AttachmentScale.ToString(),
			bSuccess ? 1 : 0);
		return bSuccess;
	}

	bool TMPatchMenuSoundtrackAndFeedbackSounds()
	{
		USoundBase* WeaponSpawnSound = nullptr;
		USoundBase* AttachmentSound = nullptr;

		bool bSuccess = TMPatchMenuSoundtrackOnly();

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		AssetRegistry.SearchAllAssets(true);

		FARFilter Filter;
		Filter.PackagePaths.Add(FName(TEXT("/Game/MP_System_V3/Game/Weapons")));
		Filter.ClassPaths.Add(FTopLevelAssetPath(FName(TEXT("/Script/Engine")), FName(TEXT("Blueprint"))));
		Filter.bRecursivePaths = true;
		Filter.bRecursiveClasses = true;

		TArray<FAssetData> BlueprintAssets;
		AssetRegistry.GetAssets(Filter, BlueprintAssets);
		BlueprintAssets.Sort([](const FAssetData& Left, const FAssetData& Right)
		{
			return Left.GetSoftObjectPath().ToString() < Right.GetSoftObjectPath().ToString();
		});

		int32 LoadedBlueprintCount = 0;
		int32 GunBlueprintCount = 0;
		int32 ChangedGunBlueprintCount = 0;
		bSuccess &= TMPatchGunFeedbackSoundDefaultByPath(
			TEXT("/Game/MP_System_V3/Game/Blueprints/Core/BP_Weapon_Master.BP_Weapon_Master"),
			WeaponSpawnSound,
			AttachmentSound,
			LoadedBlueprintCount,
			GunBlueprintCount,
			ChangedGunBlueprintCount);
		bSuccess &= TMPatchGunFeedbackSoundDefaultByPath(
			TEXT("/Game/MP_System_V3/Game/Blueprints/Core/BP_InteractionMaster.BP_InteractionMaster"),
			nullptr,
			nullptr,
			LoadedBlueprintCount,
			GunBlueprintCount,
			ChangedGunBlueprintCount);
		bSuccess &= TMPatchGunFeedbackSoundDefaultByPath(
			TEXT("/Game/MP_System_V3/Game/Blueprints/Core/BP_Magazine_Master.BP_Magazine_Master"),
			nullptr,
			nullptr,
			LoadedBlueprintCount,
			GunBlueprintCount,
			ChangedGunBlueprintCount);

		for (const FAssetData& BlueprintAsset : BlueprintAssets)
		{
			UBlueprint* Blueprint = Cast<UBlueprint>(BlueprintAsset.GetAsset());
			if (!Blueprint)
			{
				continue;
			}

			++LoadedBlueprintCount;
			if (Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(AGun::StaticClass()))
			{
				++GunBlueprintCount;
			}

			bSuccess &= TMPatchGunFeedbackSoundDefault(
				Blueprint,
				WeaponSpawnSound,
				AttachmentSound,
				ChangedGunBlueprintCount);
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMMenuSoundtrackFeedback] Summary: LoadedBlueprints=%d GunBlueprints=%d ChangedGunBlueprints=%d Success=%d"),
			LoadedBlueprintCount,
			GunBlueprintCount,
			ChangedGunBlueprintCount,
			bSuccess ? 1 : 0);
		return bSuccess;
	}

	bool TMDumpYellowUIGraphColors()
	{
		const TCHAR* WidgetBlueprintPaths[] =
		{
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Loadout.W_Loadout"),
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Attachments.W_Attachments"),
			TEXT("/Game/MP_System_V3/Game/Blueprints/Widgets/W_Attachment_Layer.W_Attachment_Layer")
		};

		for (const TCHAR* BlueprintPath : WidgetBlueprintPaths)
		{
			TMDumpYellowWidgetColorGraph(BlueprintPath);
		}

		return true;
	}
}

int32 UTMAnimGraphPatchCommandlet::Main(const FString& Params)
{
	if (Params.Contains(TEXT("DumpAnimGraphLinks"), ESearchCase::IgnoreCase))
	{
		return TMDumpTargetAnimBlueprintGraphs() ? 0 : 1;
	}

	if (Params.Contains(TEXT("DumpFullAnimGraphLinks"), ESearchCase::IgnoreCase))
	{
		return TMDumpTargetAnimBlueprintFullGraphs() ? 0 : 1;
	}

	if (Params.Contains(TEXT("PatchLocalAimHeadRestore"), ESearchCase::IgnoreCase))
	{
		return TMPatchLocalAimHeadRestore() ? 0 : 1;
	}

	if (Params.Contains(TEXT("FixDataTableRowStructs"), ESearchCase::IgnoreCase))
	{
		return TMFixBrokenDataTableRowStructBlueprints() ? 0 : 1;
	}

	if (Params.Contains(TEXT("RefreshAttachmentAssets"), ESearchCase::IgnoreCase))
	{
		return TMRefreshAttachmentAssets() ? 0 : 1;
	}

	if (Params.Contains(TEXT("SetACWILoadoutViewOffset"), ESearchCase::IgnoreCase))
	{
		return TMSetACWILoadoutViewOffset(Params) ? 0 : 1;
	}

	if (Params.Contains(TEXT("PatchSilencercoM4Tar"), ESearchCase::IgnoreCase))
	{
		return TMPatchSilencercoM4TarCompatibility() ? 0 : 1;
	}

	if (Params.Contains(TEXT("PatchDESilencercoOnly"), ESearchCase::IgnoreCase))
	{
		return TMPatchDESilencercoOnly() ? 0 : 1;
	}

	if (Params.Contains(TEXT("TintYellowUI"), ESearchCase::IgnoreCase))
	{
		return TMTintYellowUI() ? 0 : 1;
	}

	if (Params.Contains(TEXT("PatchIntroSkipText"), ESearchCase::IgnoreCase))
	{
		return TMPatchIntroSkipTextStyle() ? 0 : 1;
	}

	if (Params.Contains(TEXT("DumpYellowUIGraphColors"), ESearchCase::IgnoreCase))
	{
		return TMDumpYellowUIGraphColors() ? 0 : 1;
	}

	if (Params.Contains(TEXT("DumpLoadoutOffsetGraph"), ESearchCase::IgnoreCase))
	{
		return TMDumpLoadoutOffsetGraph() ? 0 : 1;
	}

	if (Params.Contains(TEXT("PatchLoadoutOffsetApplyLogging"), ESearchCase::IgnoreCase))
	{
		return TMPatchLoadoutOffsetApplyLogging() ? 0 : 1;
	}

	if (Params.Contains(TEXT("DumpMenuButtonWidgets"), ESearchCase::IgnoreCase))
	{
		return TMDumpMenuButtonWidgets() ? 0 : 1;
	}

	if (Params.Contains(TEXT("PatchMinimalMenuButtons"), ESearchCase::IgnoreCase))
	{
		return TMPatchMinimalMenuButtons() ? 0 : 1;
	}

	if (Params.Contains(TEXT("PatchMainMenuBigLabels"), ESearchCase::IgnoreCase))
	{
		return TMPatchMainMenuBigLabels() ? 0 : 1;
	}

	if (Params.Contains(TEXT("DumpMenuSoundGraph"), ESearchCase::IgnoreCase))
	{
		return TMDumpMenuSoundGraph() ? 0 : 1;
	}

	if (Params.Contains(TEXT("PatchMainMenuBeatAnimationDelay"), ESearchCase::IgnoreCase))
	{
		return TMInsertMainMenuBeatAnimationDelay() ? 0 : 1;
	}

	if (Params.Contains(TEXT("DumpIntroMainFlow"), ESearchCase::IgnoreCase))
	{
		return TMDumpIntroMainFlow() ? 0 : 1;
	}

	if (Params.Contains(TEXT("PatchUIButtonSounds"), ESearchCase::IgnoreCase))
	{
		return TMPatchUIButtonSounds() ? 0 : 1;
	}

	if (Params.Contains(TEXT("PatchLoadoutWeaponSelectionFeedback"), ESearchCase::IgnoreCase))
	{
		return TMPatchLoadoutWeaponSelectionFeedback() ? 0 : 1;
	}

	if (Params.Contains(TEXT("PatchLoadoutWeaponLayerIcons"), ESearchCase::IgnoreCase))
	{
		const bool bLayerPatched = TMPatchLoadoutWeaponLayerIcons();
		const bool bLoadoutPatched = TMPatchLoadoutCreatedWeaponLayerIcons();
		return (bLayerPatched && bLoadoutPatched) ? 0 : 1;
	}

	if (Params.Contains(TEXT("PatchMainMenuLoadoutPreviewCleanup"), ESearchCase::IgnoreCase))
	{
		return TMPatchMainMenuLoadoutPreviewCleanup() ? 0 : 1;
	}

	if (Params.Contains(TEXT("PatchMenuSoundtrackOnly"), ESearchCase::IgnoreCase))
	{
		return TMPatchMenuSoundtrackOnly() ? 0 : 1;
	}

	if (Params.Contains(TEXT("PatchAttachmentFeedbackFX"), ESearchCase::IgnoreCase))
	{
		return TMPatchAttachmentFeedbackFX() ? 0 : 1;
	}

	if (Params.Contains(TEXT("PatchMenuSoundtrackAndFeedbackSounds"), ESearchCase::IgnoreCase))
	{
		return TMPatchMenuSoundtrackAndFeedbackSounds() ? 0 : 1;
	}

	if (Params.Contains(TEXT("PinLeftHandFabrikAlpha"), ESearchCase::IgnoreCase))
	{
		return TMPatchActiveLeftHandFabrikConstantAlpha() ? 0 : 1;
	}

	if (Params.Contains(TEXT("RestoreFabrikateHalfAlpha"), ESearchCase::IgnoreCase))
	{
		bool bSuccess = true;
		for (const TCHAR* TargetAnimBlueprintPath : TargetAnimBlueprintPaths)
		{
			bSuccess &= TMPatchFabrikateTransformsRestoreHalfAlpha(TargetAnimBlueprintPath);
		}

		return bSuccess ? 0 : 1;
	}

	if (Params.Contains(TEXT("FixScarFakeMagazineBone"), ESearchCase::IgnoreCase))
	{
		return TMPatchScarFakeMagazineBone() ? 0 : 1;
	}

	if (Params.Contains(TEXT("FixKrissFakeAnimGraph"), ESearchCase::IgnoreCase))
	{
		return TMPatchKrissFakeAnimGraph() ? 0 : 1;
	}

	if (Params.Contains(TEXT("FixKrissFakeYaxisMagazine"), ESearchCase::IgnoreCase))
	{
		return TMPatchKrissFakeYaxisMagazine() ? 0 : 1;
	}

	if (Params.Contains(TEXT("LeftHandWeaponOffset"), ESearchCase::IgnoreCase))
	{
		bool bSuccess = true;
		for (const TCHAR* TargetAnimBlueprintPath : TargetAnimBlueprintPaths)
		{
			bSuccess &= TMPatchLeftHandWeaponOffsetOnly(TargetAnimBlueprintPath);
		}

		return bSuccess ? 0 : 1;
	}

	bool bSuccess = true;
	for (const TCHAR* TargetAnimBlueprintPath : TargetAnimBlueprintPaths)
	{
		bSuccess &= TMPatchAnimBlueprint(TargetAnimBlueprintPath);
	}

	return bSuccess ? 0 : 1;
}

#else

int32 UTMAnimGraphPatchCommandlet::Main(const FString& Params)
{
	UE_LOG(LogTemp, Error, TEXT("TMAnimGraphPatchCommandlet requires an editor build."));
	return 1;
}

#endif
