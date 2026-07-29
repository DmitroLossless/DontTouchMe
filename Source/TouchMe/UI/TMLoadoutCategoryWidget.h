// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "TMLoadoutCategoryWidget.generated.h"

class UButton;
class UImage;
class UTexture2D;

UCLASS(Blueprintable)
class TOUCHME_API UTMLoadoutCategoryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "TM|Loadout")
	void SetSelectedLoadoutCategory(FName Category);

	UFUNCTION(BlueprintCallable, Category = "TM|Loadout")
	void RefreshLoadoutCategoryFromActiveWeapon();

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void HandlePrimaryClicked();

	UFUNCTION()
	void HandleSecondaryClicked();

	UFUNCTION()
	void HandleSpecialClicked();

	UFUNCTION()
	void HandleMeleeClicked();

	UFUNCTION()
	void HandleExplosiveClicked();

	UFUNCTION()
	void HandlePrimaryHovered();

	UFUNCTION()
	void HandleSecondaryHovered();

	UFUNCTION()
	void HandleSpecialHovered();

	UFUNCTION()
	void HandleMeleeHovered();

	UFUNCTION()
	void HandleExplosiveHovered();

	UFUNCTION()
	void HandleCategoryUnhovered();

	void UnbindCategoryButton(UButton* Button);
	void SetHoveredLoadoutCategory(FName Category);
	void ApplyLoadoutCategoryIcons();
	FName ResolveCategoryFromActiveWeapon() const;
	UTexture2D* ResolveCategoryTexture(FName Category, bool bActive, const UImage* Image) const;
	static UTexture2D* GetImageTexture(const UImage* Image);
	static void ApplyImageTexture(UImage* Image, UTexture2D* Texture);

	UPROPERTY(BlueprintReadOnly, Category = "TM|Loadout", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> B_Primary;

	UPROPERTY(BlueprintReadOnly, Category = "TM|Loadout", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> B_Primary_R;

	UPROPERTY(BlueprintReadOnly, Category = "TM|Loadout", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> B_Secondary;

	UPROPERTY(BlueprintReadOnly, Category = "TM|Loadout", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> B_Secondary_R;

	UPROPERTY(BlueprintReadOnly, Category = "TM|Loadout", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> B_Special;

	UPROPERTY(BlueprintReadOnly, Category = "TM|Loadout", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> B_Special_R;

	UPROPERTY(BlueprintReadOnly, Category = "TM|Loadout", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> B_Melee;

	UPROPERTY(BlueprintReadOnly, Category = "TM|Loadout", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> B_Melee_R;

	UPROPERTY(BlueprintReadOnly, Category = "TM|Loadout", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> B_Explosive;

	UPROPERTY(BlueprintReadOnly, Category = "TM|Loadout", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> B_Explosive_R;

	UPROPERTY(BlueprintReadOnly, Category = "TM|Loadout", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UImage> TM_Primary_CategoryIconImage;

	UPROPERTY(BlueprintReadOnly, Category = "TM|Loadout", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UImage> TM_Secondary_CategoryIconImage;

	UPROPERTY(BlueprintReadOnly, Category = "TM|Loadout", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UImage> TM_Special_CategoryIconImage;

	UPROPERTY(BlueprintReadOnly, Category = "TM|Loadout", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UImage> TM_Melee_CategoryIconImage;

	UPROPERTY(BlueprintReadOnly, Category = "TM|Loadout", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UImage> TM_Explosive_CategoryIconImage;

	UPROPERTY(EditDefaultsOnly, Category = "TM|Loadout Category Icons")
	TObjectPtr<UTexture2D> PrimaryIdleTexture;

	UPROPERTY(EditDefaultsOnly, Category = "TM|Loadout Category Icons")
	TObjectPtr<UTexture2D> PrimaryActiveTexture;

	UPROPERTY(EditDefaultsOnly, Category = "TM|Loadout Category Icons")
	TObjectPtr<UTexture2D> SecondaryIdleTexture;

	UPROPERTY(EditDefaultsOnly, Category = "TM|Loadout Category Icons")
	TObjectPtr<UTexture2D> SecondaryActiveTexture;

	UPROPERTY(EditDefaultsOnly, Category = "TM|Loadout Category Icons")
	TObjectPtr<UTexture2D> SpecialIdleTexture;

	UPROPERTY(EditDefaultsOnly, Category = "TM|Loadout Category Icons")
	TObjectPtr<UTexture2D> SpecialActiveTexture;

	UPROPERTY(EditDefaultsOnly, Category = "TM|Loadout Category Icons")
	TObjectPtr<UTexture2D> MeleeIdleTexture;

	UPROPERTY(EditDefaultsOnly, Category = "TM|Loadout Category Icons")
	TObjectPtr<UTexture2D> MeleeActiveTexture;

	UPROPERTY(EditDefaultsOnly, Category = "TM|Loadout Category Icons")
	TObjectPtr<UTexture2D> ExplosiveIdleTexture;

	UPROPERTY(EditDefaultsOnly, Category = "TM|Loadout Category Icons")
	TObjectPtr<UTexture2D> ExplosiveActiveTexture;

	FName SelectedLoadoutCategory;
	FName HoveredLoadoutCategory;
};
