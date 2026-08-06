// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMLoadoutCategoryWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "UObject/UnrealType.h"

#include <initializer_list>

namespace
{
	const FName PrimaryCategory(TEXT("Primary"));
	const FName SecondaryCategory(TEXT("Secondary"));
	const FName SpecialCategory(TEXT("Special"));
	const FName MeleeCategory(TEXT("Melee"));
	const FName ExplosiveCategory(TEXT("Explosive"));

	bool ClassPathMatchesAny(const FString& ClassPath, std::initializer_list<const TCHAR*> Tokens)
	{
		for (const TCHAR* Token : Tokens)
		{
			if (ClassPath.Contains(Token, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}
}

void UTMLoadoutCategoryWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	if (SelectedLoadoutCategory.IsNone())
	{
		SelectedLoadoutCategory = PrimaryCategory;
	}
	ApplyLoadoutCategoryIcons();
}

void UTMLoadoutCategoryWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (B_Primary)
	{
		B_Primary->OnClicked.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandlePrimaryClicked);
		B_Primary->OnHovered.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandlePrimaryHovered);
		B_Primary->OnUnhovered.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleCategoryUnhovered);
		B_Primary->OnClicked.AddDynamic(this, &UTMLoadoutCategoryWidget::HandlePrimaryClicked);
		B_Primary->OnHovered.AddDynamic(this, &UTMLoadoutCategoryWidget::HandlePrimaryHovered);
		B_Primary->OnUnhovered.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleCategoryUnhovered);
	}
	if (B_Primary_R)
	{
		B_Primary_R->OnClicked.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandlePrimaryClicked);
		B_Primary_R->OnHovered.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandlePrimaryHovered);
		B_Primary_R->OnUnhovered.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleCategoryUnhovered);
		B_Primary_R->OnClicked.AddDynamic(this, &UTMLoadoutCategoryWidget::HandlePrimaryClicked);
		B_Primary_R->OnHovered.AddDynamic(this, &UTMLoadoutCategoryWidget::HandlePrimaryHovered);
		B_Primary_R->OnUnhovered.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleCategoryUnhovered);
	}
	if (B_Secondary)
	{
		B_Secondary->OnClicked.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleSecondaryClicked);
		B_Secondary->OnHovered.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleSecondaryHovered);
		B_Secondary->OnUnhovered.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleCategoryUnhovered);
		B_Secondary->OnClicked.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleSecondaryClicked);
		B_Secondary->OnHovered.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleSecondaryHovered);
		B_Secondary->OnUnhovered.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleCategoryUnhovered);
	}
	if (B_Secondary_R)
	{
		B_Secondary_R->OnClicked.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleSecondaryClicked);
		B_Secondary_R->OnHovered.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleSecondaryHovered);
		B_Secondary_R->OnUnhovered.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleCategoryUnhovered);
		B_Secondary_R->OnClicked.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleSecondaryClicked);
		B_Secondary_R->OnHovered.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleSecondaryHovered);
		B_Secondary_R->OnUnhovered.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleCategoryUnhovered);
	}
	if (B_Special)
	{
		B_Special->OnClicked.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleSpecialClicked);
		B_Special->OnHovered.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleSpecialHovered);
		B_Special->OnUnhovered.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleCategoryUnhovered);
		B_Special->OnClicked.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleSpecialClicked);
		B_Special->OnHovered.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleSpecialHovered);
		B_Special->OnUnhovered.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleCategoryUnhovered);
	}
	if (B_Special_R)
	{
		B_Special_R->OnClicked.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleSpecialClicked);
		B_Special_R->OnHovered.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleSpecialHovered);
		B_Special_R->OnUnhovered.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleCategoryUnhovered);
		B_Special_R->OnClicked.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleSpecialClicked);
		B_Special_R->OnHovered.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleSpecialHovered);
		B_Special_R->OnUnhovered.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleCategoryUnhovered);
	}
	if (B_Melee)
	{
		B_Melee->OnClicked.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleMeleeClicked);
		B_Melee->OnHovered.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleMeleeHovered);
		B_Melee->OnUnhovered.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleCategoryUnhovered);
		B_Melee->OnClicked.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleMeleeClicked);
		B_Melee->OnHovered.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleMeleeHovered);
		B_Melee->OnUnhovered.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleCategoryUnhovered);
	}
	if (B_Melee_R)
	{
		B_Melee_R->OnClicked.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleMeleeClicked);
		B_Melee_R->OnHovered.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleMeleeHovered);
		B_Melee_R->OnUnhovered.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleCategoryUnhovered);
		B_Melee_R->OnClicked.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleMeleeClicked);
		B_Melee_R->OnHovered.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleMeleeHovered);
		B_Melee_R->OnUnhovered.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleCategoryUnhovered);
	}
	if (B_Explosive)
	{
		B_Explosive->OnClicked.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleExplosiveClicked);
		B_Explosive->OnHovered.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleExplosiveHovered);
		B_Explosive->OnUnhovered.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleCategoryUnhovered);
		B_Explosive->OnClicked.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleExplosiveClicked);
		B_Explosive->OnHovered.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleExplosiveHovered);
		B_Explosive->OnUnhovered.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleCategoryUnhovered);
	}
	if (B_Explosive_R)
	{
		B_Explosive_R->OnClicked.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleExplosiveClicked);
		B_Explosive_R->OnHovered.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleExplosiveHovered);
		B_Explosive_R->OnUnhovered.RemoveDynamic(this, &UTMLoadoutCategoryWidget::HandleCategoryUnhovered);
		B_Explosive_R->OnClicked.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleExplosiveClicked);
		B_Explosive_R->OnHovered.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleExplosiveHovered);
		B_Explosive_R->OnUnhovered.AddDynamic(this, &UTMLoadoutCategoryWidget::HandleCategoryUnhovered);
	}

	RefreshLoadoutCategoryFromActiveWeapon();
}

void UTMLoadoutCategoryWidget::NativeDestruct()
{
	UnbindCategoryButton(B_Primary);
	UnbindCategoryButton(B_Primary_R);
	UnbindCategoryButton(B_Secondary);
	UnbindCategoryButton(B_Secondary_R);
	UnbindCategoryButton(B_Special);
	UnbindCategoryButton(B_Special_R);
	UnbindCategoryButton(B_Melee);
	UnbindCategoryButton(B_Melee_R);
	UnbindCategoryButton(B_Explosive);
	UnbindCategoryButton(B_Explosive_R);

	Super::NativeDestruct();
}

void UTMLoadoutCategoryWidget::SetSelectedLoadoutCategory(const FName Category)
{
	SelectedLoadoutCategory = Category.IsNone() ? PrimaryCategory : Category;
	ApplyLoadoutCategoryIcons();
}

void UTMLoadoutCategoryWidget::RefreshLoadoutCategoryFromActiveWeapon()
{
	const FName ActiveWeaponCategory = ResolveCategoryFromActiveWeapon();
	SetSelectedLoadoutCategory(ActiveWeaponCategory.IsNone() ? PrimaryCategory : ActiveWeaponCategory);
}

void UTMLoadoutCategoryWidget::HandlePrimaryClicked()
{
	SetSelectedLoadoutCategory(PrimaryCategory);
}

void UTMLoadoutCategoryWidget::HandleSecondaryClicked()
{
	SetSelectedLoadoutCategory(SecondaryCategory);
}

void UTMLoadoutCategoryWidget::HandleSpecialClicked()
{
	SetSelectedLoadoutCategory(SpecialCategory);
}

void UTMLoadoutCategoryWidget::HandleMeleeClicked()
{
	SetSelectedLoadoutCategory(MeleeCategory);
}

void UTMLoadoutCategoryWidget::HandleExplosiveClicked()
{
	SetSelectedLoadoutCategory(ExplosiveCategory);
}

void UTMLoadoutCategoryWidget::HandlePrimaryHovered()
{
	SetHoveredLoadoutCategory(PrimaryCategory);
}

void UTMLoadoutCategoryWidget::HandleSecondaryHovered()
{
	SetHoveredLoadoutCategory(SecondaryCategory);
}

void UTMLoadoutCategoryWidget::HandleSpecialHovered()
{
	SetHoveredLoadoutCategory(SpecialCategory);
}

void UTMLoadoutCategoryWidget::HandleMeleeHovered()
{
	SetHoveredLoadoutCategory(MeleeCategory);
}

void UTMLoadoutCategoryWidget::HandleExplosiveHovered()
{
	SetHoveredLoadoutCategory(ExplosiveCategory);
}

void UTMLoadoutCategoryWidget::HandleCategoryUnhovered()
{
	SetHoveredLoadoutCategory(NAME_None);
}

void UTMLoadoutCategoryWidget::UnbindCategoryButton(UButton* Button)
{
	if (!Button)
	{
		return;
	}

	Button->OnClicked.RemoveAll(this);
	Button->OnHovered.RemoveAll(this);
	Button->OnUnhovered.RemoveAll(this);
}

void UTMLoadoutCategoryWidget::SetHoveredLoadoutCategory(const FName Category)
{
	HoveredLoadoutCategory = Category;
	ApplyLoadoutCategoryIcons();
}

void UTMLoadoutCategoryWidget::ApplyLoadoutCategoryIcons()
{
	ApplyImageTexture(
		TM_Primary_CategoryIconImage,
		ResolveCategoryTexture(
			PrimaryCategory,
			HoveredLoadoutCategory == PrimaryCategory || SelectedLoadoutCategory == PrimaryCategory,
			TM_Primary_CategoryIconImage));
	ApplyImageTexture(
		TM_Secondary_CategoryIconImage,
		ResolveCategoryTexture(
			SecondaryCategory,
			HoveredLoadoutCategory == SecondaryCategory || SelectedLoadoutCategory == SecondaryCategory,
			TM_Secondary_CategoryIconImage));
	ApplyImageTexture(
		TM_Special_CategoryIconImage,
		ResolveCategoryTexture(
			SpecialCategory,
			HoveredLoadoutCategory == SpecialCategory || SelectedLoadoutCategory == SpecialCategory,
			TM_Special_CategoryIconImage));
	ApplyImageTexture(
		TM_Melee_CategoryIconImage,
		ResolveCategoryTexture(
			MeleeCategory,
			HoveredLoadoutCategory == MeleeCategory || SelectedLoadoutCategory == MeleeCategory,
			TM_Melee_CategoryIconImage));
	ApplyImageTexture(
		TM_Explosive_CategoryIconImage,
		ResolveCategoryTexture(
			ExplosiveCategory,
			HoveredLoadoutCategory == ExplosiveCategory || SelectedLoadoutCategory == ExplosiveCategory,
			TM_Explosive_CategoryIconImage));
}

FName UTMLoadoutCategoryWidget::ResolveCategoryFromActiveWeapon() const
{
	const FObjectPropertyBase* ActiveWeaponProperty =
		FindFProperty<FObjectPropertyBase>(GetClass(), TEXT("ActiveWeapon"));
	const UObject* ActiveWeapon = ActiveWeaponProperty
		? ActiveWeaponProperty->GetObjectPropertyValue_InContainer(this)
		: nullptr;
	if (!ActiveWeapon)
	{
		return NAME_None;
	}

	const FString ClassPath = GetPathNameSafe(ActiveWeapon->GetClass());
	if (ClassPathMatchesAny(
		ClassPath,
		{ TEXT("/Weapons/Primary/"), TEXT("BP_ACWI"), TEXT("BP_Kriss"), TEXT("BP_Scar"), TEXT("BP_Shotgun"), TEXT("BP_Sniper"), TEXT("BP_TAR") }))
	{
		return PrimaryCategory;
	}

	if (ClassPathMatchesAny(ClassPath, { TEXT("/Weapons/Secondary/"), TEXT("BP_DE"), TEXT("BP_M9") }))
	{
		return SecondaryCategory;
	}

	if (ClassPathMatchesAny(ClassPath, { TEXT("/Weapons/Melee/"), TEXT("BP_Knife"), TEXT("BP_Kunai"), TEXT("BP_Bayonet"), TEXT("BP_Cleaver") }))
	{
		return MeleeCategory;
	}

	if (ClassPathMatchesAny(ClassPath, { TEXT("/Weapons/Explosives/"), TEXT("BP_Frag"), TEXT("BP_Tripmine") }))
	{
		return ExplosiveCategory;
	}

	if (ClassPathMatchesAny(ClassPath, { TEXT("/Weapons/Special/"), TEXT("BP_Special") }))
	{
		return SpecialCategory;
	}

	return NAME_None;
}

UTexture2D* UTMLoadoutCategoryWidget::ResolveCategoryTexture(
	const FName Category,
	const bool bActive,
	const UImage* Image) const
{
	UTexture2D* IdleTexture = nullptr;
	UTexture2D* ActiveTexture = nullptr;

	if (Category == PrimaryCategory)
	{
		IdleTexture = PrimaryIdleTexture;
		ActiveTexture = PrimaryActiveTexture;
	}
	else if (Category == SecondaryCategory)
	{
		IdleTexture = SecondaryIdleTexture;
		ActiveTexture = SecondaryActiveTexture;
	}
	else if (Category == SpecialCategory)
	{
		IdleTexture = SpecialIdleTexture;
		ActiveTexture = SpecialActiveTexture;
	}
	else if (Category == MeleeCategory)
	{
		IdleTexture = MeleeIdleTexture;
		ActiveTexture = MeleeActiveTexture;
	}
	else if (Category == ExplosiveCategory)
	{
		IdleTexture = ExplosiveIdleTexture;
		ActiveTexture = ExplosiveActiveTexture;
	}

	if (bActive && ActiveTexture)
	{
		return ActiveTexture;
	}

	return IdleTexture ? IdleTexture : GetImageTexture(Image);
}

UTexture2D* UTMLoadoutCategoryWidget::GetImageTexture(const UImage* Image)
{
	if (!Image)
	{
		return nullptr;
	}

	return Cast<UTexture2D>(Image->GetBrush().GetResourceObject());
}

void UTMLoadoutCategoryWidget::ApplyImageTexture(UImage* Image, UTexture2D* Texture)
{
	if (!Image || !Texture || Image->GetBrush().GetResourceObject() == Texture)
	{
		return;
	}

	FSlateBrush Brush = Image->GetBrush();
	const FVector2D ImageSize = Brush.GetImageSize();
	Brush.SetResourceObject(Texture);
	if (!ImageSize.IsNearlyZero())
	{
		Brush.SetImageSize(ImageSize);
	}
	Image->SetBrush(Brush);
}
