#include "TMGenerateUIIconsCommandlet.h"

#include "AssetRegistry/AssetData.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "TouchMeEditor.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

namespace
{
	const TCHAR* TMLoadoutWeaponDataTablePath = TEXT("/Game/MP_System_V3/Game/Blueprints/DataTables/DT_Weapons.DT_Weapons");

	UObject* TMResolveObjectRedirector(UObject* Object)
	{
		while (UObjectRedirector* Redirector = Cast<UObjectRedirector>(Object))
		{
			UObject* DestinationObject = Redirector->DestinationObject;
			if (!DestinationObject || DestinationObject == Object)
			{
				break;
			}

			Object = DestinationObject;
		}

		return Object;
	}

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
			UObject* Object = TMResolveObjectRedirector(ObjectProperty->GetObjectPropertyValue(ValuePtr));
			if (TMIsSupportedIconMesh(Object))
			{
				OutMeshes.AddUnique(Object);
			}
			return;
		}

		if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
		{
			const FSoftObjectPtr SoftObject = SoftObjectProperty->GetPropertyValue(ValuePtr);
			UObject* Object = TMResolveObjectRedirector(SoftObject.LoadSynchronous());
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

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (!StructProperty->Struct)
			{
				return;
			}

			for (TFieldIterator<FProperty> ChildPropertyIt(StructProperty->Struct); ChildPropertyIt; ++ChildPropertyIt)
			{
				const FProperty* ChildProperty = *ChildPropertyIt;
				const void* ChildValuePtr = ChildProperty->ContainerPtrToValuePtr<void>(ValuePtr);
				TMCollectMeshAssetsFromProperty(ChildProperty, ChildValuePtr, OutMeshes);
			}
		}
	}

	bool TMIsCoreDataProperty(const FProperty* Property)
	{
		if (!Property)
		{
			return false;
		}

		const auto MatchesCoreDataName = [](const FString& Name)
		{
			return Name.Equals(TEXT("CoreData"), ESearchCase::IgnoreCase)
				|| Name.StartsWith(TEXT("CoreData_"), ESearchCase::IgnoreCase);
		};

		return MatchesCoreDataName(Property->GetName())
			|| MatchesCoreDataName(Property->GetAuthoredName())
			|| MatchesCoreDataName(Property->GetDisplayNameText().ToString());
	}

	bool TMIsMeshProperty(const FProperty* Property)
	{
		if (!Property)
		{
			return false;
		}

		const auto MatchesMeshName = [](const FString& Name)
		{
			return Name.Equals(TEXT("Mesh"), ESearchCase::IgnoreCase)
				|| Name.StartsWith(TEXT("Mesh_"), ESearchCase::IgnoreCase)
				|| Name.Equals(TEXT("DT_Mesh"), ESearchCase::IgnoreCase)
				|| Name.StartsWith(TEXT("DT_Mesh_"), ESearchCase::IgnoreCase);
		};

		return MatchesMeshName(Property->GetName())
			|| MatchesMeshName(Property->GetAuthoredName())
			|| MatchesMeshName(Property->GetDisplayNameText().ToString());
	}

	void TMAddCoreDataMeshObject(UObject* Object, TArray<UObject*>& OutMeshes)
	{
		Object = TMResolveObjectRedirector(Object);
		if (TMIsSupportedIconMesh(Object))
		{
			OutMeshes.AddUnique(Object);
		}
	}

	void TMCollectCoreDataMeshAssetsFromMeshProperty(
		const FProperty* Property,
		const void* ValuePtr,
		TArray<UObject*>& OutMeshes)
	{
		if (!Property || !ValuePtr)
		{
			return;
		}

		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			TMAddCoreDataMeshObject(ObjectProperty->GetObjectPropertyValue(ValuePtr), OutMeshes);
			return;
		}

		if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
		{
			const FSoftObjectPtr SoftObject = SoftObjectProperty->GetPropertyValue(ValuePtr);
			TMAddCoreDataMeshObject(SoftObject.LoadSynchronous(), OutMeshes);
			return;
		}

		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
			for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
			{
				TMCollectCoreDataMeshAssetsFromMeshProperty(
					ArrayProperty->Inner,
					ArrayHelper.GetRawPtr(Index),
					OutMeshes);
			}
			return;
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (!StructProperty->Struct)
			{
				return;
			}

			for (TFieldIterator<FProperty> ChildPropertyIt(StructProperty->Struct); ChildPropertyIt; ++ChildPropertyIt)
			{
				const FProperty* ChildProperty = *ChildPropertyIt;
				const void* ChildValuePtr = ChildProperty->ContainerPtrToValuePtr<void>(ValuePtr);
				TMCollectCoreDataMeshAssetsFromMeshProperty(ChildProperty, ChildValuePtr, OutMeshes);
			}
		}
	}

	void TMCollectCoreDataMeshAssetsFromProperty(
		const FProperty* Property,
		const void* ValuePtr,
		const int32 CoreDataDepth,
		TArray<UObject*>& OutMeshes)
	{
		if (!Property || !ValuePtr)
		{
			return;
		}

		if (CoreDataDepth == 1 && TMIsMeshProperty(Property))
		{
			TMCollectCoreDataMeshAssetsFromMeshProperty(Property, ValuePtr, OutMeshes);
			return;
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (!StructProperty->Struct)
			{
				return;
			}

			const int32 ChildCoreDataDepth = TMIsCoreDataProperty(Property) ? 1 : (CoreDataDepth > 0 ? CoreDataDepth + 1 : 0);
			for (TFieldIterator<FProperty> ChildPropertyIt(StructProperty->Struct); ChildPropertyIt; ++ChildPropertyIt)
			{
				const FProperty* ChildProperty = *ChildPropertyIt;
				const void* ChildValuePtr = ChildProperty->ContainerPtrToValuePtr<void>(ValuePtr);
				TMCollectCoreDataMeshAssetsFromProperty(ChildProperty, ChildValuePtr, ChildCoreDataDepth, OutMeshes);
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

	FString TMMakeGeneratedIconPath(const UObject* Mesh, const bool bActive)
	{
		if (!Mesh)
		{
			return FString();
		}

		const FString IconName = FString::Printf(TEXT("T_%s_Icon%s"), *Mesh->GetName(), bActive ? TEXT("_Active") : TEXT(""));
		return FString::Printf(TEXT("/Game/UI/Generated/Icons/%s.%s"), *IconName, *IconName);
	}

	bool TMShouldSkipLoadoutWeaponIconVerification(const FName RowName, const UObject* Mesh)
	{
		return RowName.ToString().Equals(TEXT("Sniper"), ESearchCase::IgnoreCase)
			|| (Mesh && Mesh->GetName().Equals(TEXT("Sniper"), ESearchCase::IgnoreCase));
	}

	bool TMVerifyLoadoutWeaponIcons()
	{
		UDataTable* WeaponTable = LoadObject<UDataTable>(nullptr, TMLoadoutWeaponDataTablePath);
		if (!WeaponTable || !WeaponTable->GetRowStruct())
		{
			UE_LOG(LogTemp, Error, TEXT("[TMIconVerifier] Failed to load weapon data table: %s."), TMLoadoutWeaponDataTablePath);
			return false;
		}

		bool bAllRowsValid = true;
		for (const TPair<FName, uint8*>& RowPair : WeaponTable->GetRowMap())
		{
			TArray<UObject*> RowMeshes;
			for (TFieldIterator<FProperty> PropertyIt(WeaponTable->GetRowStruct()); PropertyIt; ++PropertyIt)
			{
				const FProperty* Property = *PropertyIt;
				const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(RowPair.Value);
				TMCollectCoreDataMeshAssetsFromProperty(Property, ValuePtr, 0, RowMeshes);
			}

			if (RowMeshes.IsEmpty())
			{
				UE_LOG(LogTemp, Error, TEXT("[TMIconVerifier] Row %s has no supported CoreData.Mesh."), *RowPair.Key.ToString());
				bAllRowsValid = false;
				continue;
			}

			UObject* Mesh = RowMeshes[0];
			if (TMShouldSkipLoadoutWeaponIconVerification(RowPair.Key, Mesh))
			{
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMIconVerifier] Row=%s CoreData.Mesh=%s skipped because the row is collapsed in loadout."),
					*RowPair.Key.ToString(),
					*Mesh->GetPathName());
				continue;
			}

			const FString IconPath = TMMakeGeneratedIconPath(Mesh, false);
			const FString ActiveIconPath = TMMakeGeneratedIconPath(Mesh, true);
			const bool bHasIcon = LoadObject<UTexture2D>(nullptr, *IconPath) != nullptr;
			const bool bHasActiveIcon = LoadObject<UTexture2D>(nullptr, *ActiveIconPath) != nullptr;
			if (bHasIcon && bHasActiveIcon)
			{
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMIconVerifier] Row=%s CoreData.Mesh=%s Icon=OK ActiveIcon=OK"),
					*RowPair.Key.ToString(),
					*Mesh->GetPathName());
			}
			else
			{
				UE_LOG(
					LogTemp,
					Error,
					TEXT("[TMIconVerifier] Row=%s CoreData.Mesh=%s Icon=%s ActiveIcon=%s"),
					*RowPair.Key.ToString(),
					*Mesh->GetPathName(),
					bHasIcon ? TEXT("OK") : *IconPath,
					bHasActiveIcon ? TEXT("OK") : *ActiveIconPath);
			}

			bAllRowsValid &= bHasIcon && bHasActiveIcon;
		}

		return bAllRowsValid;
	}
}

int32 UTMGenerateUIIconsCommandlet::Main(const FString& Params)
{
	if (FParse::Param(*Params, TEXT("VerifyLoadoutWeaponIcons"))
		|| FParse::Param(*Params, TEXT("VerifyLoadoutWeapons")))
	{
		return TMVerifyLoadoutWeaponIcons() ? 0 : 1;
	}

	if (FParse::Param(*Params, TEXT("LoadoutWeapons"))
		|| FParse::Param(*Params, TEXT("LoadoutWeaponIcons")))
	{
		FTouchMeEditorModule& TouchMeEditorModule =
			FModuleManager::LoadModuleChecked<FTouchMeEditorModule>(TEXT("TouchMeEditor"));
		TouchMeEditorModule.GenerateLoadoutWeaponActiveIcons();
		return 0;
	}

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
