#include "TMAnimGraphPatchCommandlet.h"

#if WITH_EDITOR

#include "AnimGraphNode_CopyBone.h"
#include "AnimGraphNode_ModifyBone.h"
#include "Animation/AnimBlueprint.h"
#include "BoneControllers/AnimNode_ModifyBone.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNodeUtils.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

namespace
{
	const TCHAR* TargetAnimBlueprintPaths[] =
	{
		TEXT("/Game/Test/MPVS_SkeletonProbe/ImportedOnALS/ABP_UE5_MPSBones_OnALS.ABP_UE5_MPSBones_OnALS")
	};

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
			TEXT("Weapon"),
			TEXT("hand_r"),
			TEXT("hand_l"),
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
			Links.Add(FString::Printf(
				TEXT("%s.%s"),
				LinkedNode ? *LinkedNode->GetName() : TEXT("None"),
				LinkedPin ? *LinkedPin->PinName.ToString() : TEXT("None")));
		}
		return Links.Num() > 0 ? FString::Join(Links, TEXT(", ")) : TEXT("None");
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
				|| ModifyBoneNode->Node.TranslationMode == BMM_Ignore)
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

	bool TMVerifyDedicatedCameraOffsetRotationLink(
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
				if (TMIsDedicatedCameraOffsetRotationNode(LinkedModifyBoneNode, BoneName))
				{
					OffsetNode = LinkedModifyBoneNode;
					break;
				}
			}
		}

		UEdGraphPin* RotationPin = OffsetNode ? TMFindPinByName(OffsetNode, RotationPinName, EGPD_Input) : nullptr;
		const int32 LinkCount = RotationPin ? RotationPin->LinkedTo.Num() : 0;
		UE_LOG(
			LogTemp,
			Display,
			TEXT("Post-compile verify %s:%s dedicated %s Rotation pin has %d link(s)."),
			AnimBlueprint ? *AnimBlueprint->GetPathName() : TEXT("None"),
			Graph ? *Graph->GetName() : TEXT("None"),
			*BoneName.ToString(),
			LinkCount);
		return LinkCount > 0;
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

		if (!TMEnsureTransformVariable(AnimBlueprint, CameraWeaponOffsetPropertyName)
			|| !TMEnsureTransformVariable(AnimBlueprint, CameraWeaponOffsetAimingPropertyName))
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

		if (!TMClearCameraOffsetRotationPatchFromModifyBoneNodes(
				AnimBlueprint,
				MainAnimGraph,
				CameraFPBoneName,
				BMM_Ignore)
			|| !TMClearCameraOffsetRotationPatchFromModifyBoneNodes(
				AnimBlueprint,
				Graph,
				VisualPivotBoneName,
				BMM_Additive)
			|| !TMClearCameraOffsetRotationPatchFromModifyBoneNodes(
				AnimBlueprint,
				Graph,
				WeaponBoneName,
				BMM_Additive))
		{
			return false;
		}

		if (!TMInsertDedicatedCameraOffsetRotationAfterSourceModifyBone(
				AnimBlueprint,
				Graph,
				VisualPivotBoneName,
				BCS_BoneSpace,
				CameraWeaponOffsetPropertyName,
				160))
		{
			return false;
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
		FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);

		TMDumpRelevantAnimGraphLinks(AnimBlueprint, TEXT("AfterCompile"));

		if (!TMVerifyDedicatedCameraOffsetRotationLink(AnimBlueprint, Graph, VisualPivotBoneName))
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
			TEXT("Patched %s: CameraWeaponOffset.Rotation now drives the dedicated VB Control node in FabrikateTransforms."),
			TargetAnimBlueprintPath);
		return true;
	}
}

int32 UTMAnimGraphPatchCommandlet::Main(const FString& Params)
{
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
