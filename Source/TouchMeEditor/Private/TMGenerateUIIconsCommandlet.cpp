#include "TMGenerateUIIconsCommandlet.h"

#include "AssetRegistry/AssetData.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "TouchMeEditor.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

namespace
{
	bool TMIsSupportedIconMesh(const UObject* Object)
	{
		if (!Object || (!Object->IsA<UStaticMesh>() && !Object->IsA<USkeletalMesh>()))
		{
			return false;
		}

		const FString MeshName = Object->GetName();
		return !MeshName.EndsWith(TEXT("_NR"), ESearchCase::IgnoreCase)
			&& !MeshName.Contains(TEXT("NoRender"), ESearchCase::IgnoreCase)
			&& !MeshName.Contains(TEXT("No_Render"), ESearchCase::IgnoreCase);
	}

	void TMCollectMeshAssetsFromProperty(const FProperty* Property, const void* ValuePtr, TArray<UObject*>& OutMeshes)
	{
		if (!Property || !ValuePtr)
		{
			return;
		}

		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			UObject* Object = ObjectProperty->GetObjectPropertyValue(ValuePtr);
			if (TMIsSupportedIconMesh(Object))
			{
				OutMeshes.AddUnique(Object);
			}
			return;
		}

		if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
		{
			const FSoftObjectPtr SoftObject = SoftObjectProperty->GetPropertyValue(ValuePtr);
			UObject* Object = SoftObject.LoadSynchronous();
			if (TMIsSupportedIconMesh(Object))
			{
				OutMeshes.AddUnique(Object);
			}
			return;
		}

		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
			for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
			{
				TMCollectMeshAssetsFromProperty(ArrayProperty->Inner, ArrayHelper.GetRawPtr(Index), OutMeshes);
			}
		}
	}

	TArray<FAssetData> TMCollectMeshAssetsFromDataTable(UDataTable* DataTable)
	{
		TArray<FAssetData> MeshAssets;
		if (!DataTable || !DataTable->GetRowStruct())
		{
			return MeshAssets;
		}

		const UScriptStruct* RowStruct = DataTable->GetRowStruct();
		for (const TPair<FName, uint8*>& RowPair : DataTable->GetRowMap())
		{
			TArray<UObject*> RowMeshes;
			for (TFieldIterator<FProperty> PropertyIt(RowStruct); PropertyIt; ++PropertyIt)
			{
				const FProperty* Property = *PropertyIt;
				const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(RowPair.Value);
				TMCollectMeshAssetsFromProperty(Property, ValuePtr, RowMeshes);
			}

			for (UObject* Mesh : RowMeshes)
			{
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMIconGenerator] Data table row %s uses mesh %s."),
					*RowPair.Key.ToString(),
					*Mesh->GetPathName());
				MeshAssets.AddUnique(FAssetData(Mesh));
			}
		}

		return MeshAssets;
	}
}

int32 UTMGenerateUIIconsCommandlet::Main(const FString& Params)
{
	FString TablePath;
	if (FParse::Value(*Params, TEXT("Table="), TablePath)
		|| FParse::Value(*Params, TEXT("DataTable="), TablePath))
	{
		UDataTable* DataTable = LoadObject<UDataTable>(nullptr, *TablePath);
		if (!DataTable)
		{
			UE_LOG(LogTemp, Error, TEXT("[TMIconGenerator] Failed to load data table: %s"), *TablePath);
			return 1;
		}

		TArray<FAssetData> MeshAssets = TMCollectMeshAssetsFromDataTable(DataTable);
		if (MeshAssets.IsEmpty())
		{
			UE_LOG(LogTemp, Error, TEXT("[TMIconGenerator] No supported mesh assets found in %s."), *TablePath);
			return 1;
		}

		FTouchMeEditorModule& TouchMeEditorModule =
			FModuleManager::LoadModuleChecked<FTouchMeEditorModule>(TEXT("TouchMeEditor"));
		TouchMeEditorModule.GenerateIconsForAssets(MoveTemp(MeshAssets));
		return 0;
	}

	FString AssetPath;
	if (!FParse::Value(*Params, TEXT("Asset="), AssetPath))
	{
		AssetPath = TEXT("/Game/MP_System_V3/Game/Blueprints/DataTables/DT_Optics.DT_Optics");
		UDataTable* DataTable = LoadObject<UDataTable>(nullptr, *AssetPath);
		if (DataTable)
		{
			TArray<FAssetData> MeshAssets = TMCollectMeshAssetsFromDataTable(DataTable);
			if (MeshAssets.IsEmpty())
			{
				UE_LOG(LogTemp, Error, TEXT("[TMIconGenerator] No supported mesh assets found in %s."), *AssetPath);
				return 1;
			}

			FTouchMeEditorModule& TouchMeEditorModule =
				FModuleManager::LoadModuleChecked<FTouchMeEditorModule>(TEXT("TouchMeEditor"));
			TouchMeEditorModule.GenerateIconsForAssets(MoveTemp(MeshAssets));
			return 0;
		}

		AssetPath = TEXT("/Game/AdvanceWeaponPack/Mesh/Attachment/Sight/SM_Holographic_Sight.SM_Holographic_Sight");
	}

	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		UE_LOG(LogTemp, Error, TEXT("[TMIconGenerator] Failed to load asset: %s"), *AssetPath);
		return 1;
	}

	const FAssetData AssetData(Asset);
	FTouchMeEditorModule& TouchMeEditorModule =
		FModuleManager::LoadModuleChecked<FTouchMeEditorModule>(TEXT("TouchMeEditor"));
	TouchMeEditorModule.GenerateIconsForAssets({ AssetData });

	return 0;
}
