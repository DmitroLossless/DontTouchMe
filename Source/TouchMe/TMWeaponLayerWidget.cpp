// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMWeaponLayerWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "UI/TMWeaponIconResolver.h"

namespace
{
const FVector2D WeaponIconMaxWidgetSize(288.0f, 72.0f);
const FLinearColor WeaponSelectionFrameColor = FLinearColor::FromSRGBColor(FColor(255, 212, 32, 255));

FVector2D GetWeaponIconTextureSize(const UTexture2D* Texture)
{
	if (!Texture || Texture->GetSizeX() <= 0 || Texture->GetSizeY() <= 0)
	{
		return FVector2D(512.0f, 128.0f);
	}

	return FVector2D(static_cast<float>(Texture->GetSizeX()), static_cast<float>(Texture->GetSizeY()));
}

FVector2D GetWeaponIconWidgetSize(const UTexture2D* Texture)
{
	const FVector2D TextureSize = GetWeaponIconTextureSize(Texture);
	const float Scale = FMath::Min(
		WeaponIconMaxWidgetSize.X / TextureSize.X,
		WeaponIconMaxWidgetSize.Y / TextureSize.Y);
	return TextureSize * FMath::Max(Scale, 0.001f);
}

FSlateBrush MakeWeaponIconBrush(UTexture2D* Texture)
{
	FSlateBrush Brush;
	Brush.DrawAs = Texture ? ESlateBrushDrawType::Image : ESlateBrushDrawType::NoDrawType;
	Brush.ImageSize = GetWeaponIconTextureSize(Texture);
	Brush.TintColor = FSlateColor(FLinearColor::White);
	if (Texture)
	{
		Brush.SetResourceObject(Texture);
	}
	return Brush;
}

FSlateBrush MakeWeaponFrameBrush(const bool bVisible, const FVector2D& ImageSize)
{
	FSlateBrush Brush;
	Brush.DrawAs = bVisible ? ESlateBrushDrawType::Border : ESlateBrushDrawType::NoDrawType;
	Brush.ImageSize = ImageSize;
	Brush.Margin = FMargin(0.004f, 0.016f);
	Brush.TintColor = FSlateColor(bVisible ? WeaponSelectionFrameColor : FLinearColor::Transparent);
	if (bVisible)
	{
		Brush.SetResourceObject(LoadObject<UTexture2D>(
			nullptr,
			TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")));
	}
	return Brush;
}
}

UTMWeaponLayerWidget::UTMWeaponLayerWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UTMWeaponLayerWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	RefreshWeaponIcon();
}

void UTMWeaponLayerWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshWeaponIcon();
}

void UTMWeaponLayerWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	ApplyHoverScale();
}

void UTMWeaponLayerWidget::RefreshWeaponIcon()
{
	ResolveIconWidgets();
	HideNameText();

	const FString LookupToken = GetWeaponLookupToken();
	if (ShouldCollapseWeaponRow(LookupToken))
	{
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	UTexture2D* IconTexture = ResolveWeaponIconTexture(LookupToken, bWeaponIconSelected);
	if (!IconTexture)
	{
		return;
	}

	SetVisibility(ESlateVisibility::Visible);
	const FVector2D TextureSize = GetWeaponIconTextureSize(IconTexture);
	const FVector2D WidgetSize = GetWeaponIconWidgetSize(IconTexture);

	if (USizeBox* Box = IconBox.Get())
	{
		Box->SetWidthOverride(WidgetSize.X);
		Box->SetHeightOverride(WidgetSize.Y);
		Box->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	if (UOverlay* Overlay = IconOverlay.Get())
	{
		Overlay->SetVisibility(ESlateVisibility::HitTestInvisible);
		Overlay->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	}

	if (UScaleBox* ScaleBox = IconScaleBox.Get())
	{
		ScaleBox->SetVisibility(ESlateVisibility::HitTestInvisible);
		ScaleBox->SetStretch(EStretch::ScaleToFit);
		ScaleBox->SetStretchDirection(EStretchDirection::Both);
	}

	if (UImage* Image = IconImage.Get())
	{
		Image->SetVisibility(ESlateVisibility::HitTestInvisible);
		Image->SetDesiredSizeOverride(TextureSize);
		Image->SetBrush(MakeWeaponIconBrush(IconTexture));
		Image->SetColorAndOpacity(FLinearColor::White);
	}

	if (UImage* Frame = IconFrame.Get())
	{
		Frame->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	ApplySelectionFrame();
	ApplyHoverScale();
}

void UTMWeaponLayerWidget::SetWeaponIconSelected(const bool bSelected)
{
	bWeaponIconSelected = bSelected;
	ResolveIconWidgets();
	ApplyWeaponIconBrush();
	ApplySelectionFrame();
}

FString UTMWeaponLayerWidget::GetWeaponLookupToken() const
{
	return TMWeaponIconResolver::GetWidgetWeaponLookupToken(this);
}

UTexture2D* UTMWeaponLayerWidget::ResolveWeaponIconTexture(const FString& LookupToken, const bool bSelected) const
{
	return TMWeaponIconResolver::ResolveIconTexture(this, LookupToken, bSelected);
}

bool UTMWeaponLayerWidget::ShouldCollapseWeaponRow(const FString& LookupToken) const
{
	return TMWeaponIconResolver::ShouldCollapseWeaponRow(LookupToken)
		|| TMWeaponIconResolver::ShouldCollapseWeaponRow(this);
}

void UTMWeaponLayerWidget::ResolveIconWidgets()
{
	if (!WeaponButton.IsValid())
	{
		WeaponButton = Cast<UButton>(GetWidgetFromName(TEXT("B_Weapon")));
	}
	if (!IconBox.IsValid())
	{
		IconBox = Cast<USizeBox>(GetWidgetFromName(TEXT("TM_WeaponIconBox")));
	}
	if (!IconOverlay.IsValid())
	{
		IconOverlay = Cast<UOverlay>(GetWidgetFromName(TEXT("TM_WeaponIconOverlay")));
	}
	if (!IconScaleBox.IsValid())
	{
		IconScaleBox = Cast<UScaleBox>(GetWidgetFromName(TEXT("TM_WeaponIconScaleBox")));
	}
	if (!IconImage.IsValid())
	{
		IconImage = Cast<UImage>(GetWidgetFromName(TEXT("TM_WeaponIconImage")));
	}
	if (!IconFrame.IsValid())
	{
		IconFrame = Cast<UImage>(GetWidgetFromName(TEXT("TM_WeaponIconFrame")));
	}
	if (!NameText.IsValid())
	{
		NameText = Cast<UTextBlock>(GetWidgetFromName(TEXT("NameText")));
	}
}

void UTMWeaponLayerWidget::ApplyWeaponIconBrush() const
{
	UImage* Image = IconImage.Get();
	if (!Image)
	{
		return;
	}

	UTexture2D* IconTexture = ResolveWeaponIconTexture(GetWeaponLookupToken(), bWeaponIconSelected);
	if (!IconTexture)
	{
		Image->SetBrush(MakeWeaponIconBrush(nullptr));
		return;
	}

	const FVector2D TextureSize = GetWeaponIconTextureSize(IconTexture);
	if (USizeBox* Box = IconBox.Get())
	{
		const FVector2D WidgetSize = GetWeaponIconWidgetSize(IconTexture);
		Box->SetWidthOverride(WidgetSize.X);
		Box->SetHeightOverride(WidgetSize.Y);
	}

	Image->SetDesiredSizeOverride(TextureSize);
	Image->SetBrush(MakeWeaponIconBrush(IconTexture));
	Image->SetColorAndOpacity(FLinearColor::White);
}

void UTMWeaponLayerWidget::ApplySelectionFrame() const
{
	if (UImage* Frame = IconFrame.Get())
	{
		const UTexture2D* IconTexture = ResolveWeaponIconTexture(GetWeaponLookupToken(), bWeaponIconSelected);
		const FVector2D TextureSize = GetWeaponIconTextureSize(IconTexture);
		Frame->SetVisibility(ESlateVisibility::HitTestInvisible);
		Frame->SetDesiredSizeOverride(TextureSize);
		Frame->SetBrush(MakeWeaponFrameBrush(false, TextureSize));
		Frame->SetColorAndOpacity(FLinearColor::Transparent);
	}
}

void UTMWeaponLayerWidget::ApplyHoverScale() const
{
	if (UOverlay* Overlay = IconOverlay.Get())
	{
		const UButton* Button = WeaponButton.Get();
		const bool bCanClickWeapon = Button && Button->GetIsEnabled() && !bWeaponIconSelected;
		const float HoverScale = bCanClickWeapon && Button->IsHovered() ? 1.07f : 1.0f;
		Overlay->SetRenderScale(FVector2D(HoverScale, HoverScale));
	}
}

void UTMWeaponLayerWidget::HideNameText() const
{
	if (UTextBlock* TextBlock = NameText.Get())
	{
		TextBlock->SetRenderOpacity(0.0f);
		TextBlock->SetVisibility(ESlateVisibility::Collapsed);
	}
}
