#include "TMGenerateUIIconsCommandlet.h"

#include "AssetRegistry/AssetData.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "TouchMeEditor.h"
#include "UObject/SoftObjectPath.h"

int32 UTMGenerateUIIconsCommandlet::Main(const FString& Params)
{
	FString AssetPath;
	if (!FParse::Value(*Params, TEXT("Asset="), AssetPath))
	{
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
