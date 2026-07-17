// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TMWeaponLayerWidget.generated.h"

class UButton;
class UImage;
class UOverlay;
class UScaleBox;
class USizeBox;
class UTextBlock;
class UTexture2D;

UCLASS(Blueprintable)
class TOUCHME_API UTMWeaponLayerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	explicit UTMWeaponLayerWidget(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable)
	void RefreshWeaponIcon();

	void SetWeaponIconSelected(bool bSelected);
	FString GetWeaponLookupToken() const;

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UTexture2D* ResolveWeaponIconTexture(const FString& LookupToken) const;
	bool ShouldCollapseWeaponRow(const FString& LookupToken) const;
	void ResolveIconWidgets();
	void ApplySelectionFrame() const;
	void ApplyHoverScale() const;
	void HideNameText() const;

	TWeakObjectPtr<UButton> WeaponButton;
	TWeakObjectPtr<USizeBox> IconBox;
	TWeakObjectPtr<UOverlay> IconOverlay;
	TWeakObjectPtr<UScaleBox> IconScaleBox;
	TWeakObjectPtr<UImage> IconImage;
	TWeakObjectPtr<UImage> IconFrame;
	TWeakObjectPtr<UTextBlock> NameText;
	bool bWeaponIconSelected = false;
};
