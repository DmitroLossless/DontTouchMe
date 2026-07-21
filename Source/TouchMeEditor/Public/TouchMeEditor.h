#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FExtender;
class FMenuBuilder;

class FTouchMeEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	void GenerateIconsForAssets(TArray<FAssetData> SelectedAssets) const;
	void GenerateLoadoutWeaponActiveIcons() const;

private:
	TSharedRef<FExtender> OnExtendAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets);
	void BuildAssetSelectionMenu(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets) const;

	static bool IsSupportedMeshAsset(const FAssetData& AssetData);

private:
	FDelegateHandle ContentBrowserAssetExtenderDelegateHandle;
};
