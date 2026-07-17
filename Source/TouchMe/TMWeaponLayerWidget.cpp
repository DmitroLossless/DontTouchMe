// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMWeaponLayerWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/UserDefinedStruct.h"
#include "Engine/SkeletalMesh.h"

namespace
{
const FVector2D WeaponIconBrushSize(512.0f, 128.0f);
const FVector2D WeaponIconWidgetSize(288.0f, 72.0f);
const FLinearColor WeaponSelectionFrameColor = FLinearColor::FromSRGBColor(FColor(255, 212, 32, 255));

FString NormalizeWeaponIconToken(const FString& Token)
{
	FString Normalized = Token;
	Normalized.TrimStartAndEndInline();
	Normalized.ReplaceInline(TEXT(" "), TEXT(""));
	Normalized.ReplaceInline(TEXT("_"), TEXT(""));
	Normalized.ReplaceInline(TEXT("-"), TEXT(""));
	return Normalized;
}

bool IsWeaponMeshObject(const UObject* Object)
{
	return Object && (Object->IsA<USkeletalMesh>() || Object->IsA<UStaticMesh>());
}

void CollectWeaponMeshesFromProperty(const FProperty* Property, const void* ValuePtr, TArray<UObject*>& OutMeshes)
{
	if (!Property || !ValuePtr)
	{
		return;
	}

	if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		UObject* ObjectValue = ObjectProperty->GetObjectPropertyValue(ValuePtr);
		if (IsWeaponMeshObject(ObjectValue))
		{
			OutMeshes.AddUnique(ObjectValue);
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
			CollectWeaponMeshesFromProperty(ChildProperty, ChildValuePtr, OutMeshes);
		}
	}
}

FSlateBrush MakeWeaponIconBrush(UTexture2D* Texture)
{
	FSlateBrush Brush;
	Brush.DrawAs = Texture ? ESlateBrushDrawType::Image : ESlateBrushDrawType::NoDrawType;
	Brush.ImageSize = WeaponIconBrushSize;
	Brush.TintColor = FSlateColor(FLinearColor::White);
	if (Texture)
	{
		Brush.SetResourceObject(Texture);
	}
	return Brush;
}

FSlateBrush MakeWeaponFrameBrush(const bool bVisible)
{
	FSlateBrush Brush;
	Brush.DrawAs = bVisible ? ESlateBrushDrawType::Border : ESlateBrushDrawType::NoDrawType;
	Brush.ImageSize = WeaponIconBrushSize;
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

FString MakeWeaponIconObjectPath(const UObject* Mesh)
{
	if (!Mesh)
	{
		return FString();
	}

	const FString IconName = FString::Printf(TEXT("T_%s_Icon"), *Mesh->GetName());
	return FString::Printf(TEXT("/Game/UI/Generated/Icons/%s.%s"), *IconName, *IconName);
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

	UTexture2D* IconTexture = ResolveWeaponIconTexture(LookupToken);
	if (!IconTexture)
	{
		return;
	}

	SetVisibility(ESlateVisibility::Visible);

	if (USizeBox* Box = IconBox.Get())
	{
		Box->SetWidthOverride(WeaponIconWidgetSize.X);
		Box->SetHeightOverride(WeaponIconWidgetSize.Y);
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
		Image->SetDesiredSizeOverride(WeaponIconBrushSize);
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
	ApplySelectionFrame();
}

FString UTMWeaponLayerWidget::GetWeaponLookupToken() const
{
	const UClass* WidgetClass = GetClass();
	if (!WidgetClass)
	{
		return FString();
	}

	if (const FNameProperty* NameProperty = FindFProperty<FNameProperty>(WidgetClass, TEXT("Name")))
	{
		return NameProperty->GetPropertyValue_InContainer(this).ToString();
	}

	if (const FStrProperty* StringProperty = FindFProperty<FStrProperty>(WidgetClass, TEXT("Name")))
	{
		return StringProperty->GetPropertyValue_InContainer(this);
	}

	if (const FTextProperty* TextProperty = FindFProperty<FTextProperty>(WidgetClass, TEXT("Name")))
	{
		return TextProperty->GetPropertyValue_InContainer(this).ToString();
	}

	return FString();
}

UTexture2D* UTMWeaponLayerWidget::ResolveWeaponIconTexture(const FString& LookupToken) const
{
	const FString NormalizedLookupToken = NormalizeWeaponIconToken(LookupToken);
	if (NormalizedLookupToken.IsEmpty())
	{
		return nullptr;
	}

	UDataTable* WeaponTable = LoadObject<UDataTable>(
		nullptr,
		TEXT("/Game/MP_System_V3/Game/Blueprints/DataTables/DT_Weapons.DT_Weapons"));
	if (!WeaponTable || !WeaponTable->GetRowStruct())
	{
		return nullptr;
	}

	for (const TPair<FName, uint8*>& RowPair : WeaponTable->GetRowMap())
	{
		if (!NormalizeWeaponIconToken(RowPair.Key.ToString()).Equals(NormalizedLookupToken, ESearchCase::IgnoreCase))
		{
			continue;
		}

		TArray<UObject*> RowMeshes;
		for (TFieldIterator<FProperty> PropertyIt(WeaponTable->GetRowStruct()); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(RowPair.Value);
			CollectWeaponMeshesFromProperty(Property, ValuePtr, RowMeshes);
		}

		for (UObject* Mesh : RowMeshes)
		{
			const FString IconObjectPath = MakeWeaponIconObjectPath(Mesh);
			if (UTexture2D* IconTexture = LoadObject<UTexture2D>(nullptr, *IconObjectPath))
			{
				return IconTexture;
			}
		}
	}

	return nullptr;
}

bool UTMWeaponLayerWidget::ShouldCollapseWeaponRow(const FString& LookupToken) const
{
	const FString NormalizedLookupToken = NormalizeWeaponIconToken(LookupToken);
	return NormalizedLookupToken.Equals(TEXT("Sniper"), ESearchCase::IgnoreCase)
		|| NormalizedLookupToken.Equals(TEXT("BPSniper"), ESearchCase::IgnoreCase);
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

void UTMWeaponLayerWidget::ApplySelectionFrame() const
{
	if (UImage* Frame = IconFrame.Get())
	{
		Frame->SetVisibility(ESlateVisibility::HitTestInvisible);
		Frame->SetBrush(MakeWeaponFrameBrush(bWeaponIconSelected));
		Frame->SetColorAndOpacity(bWeaponIconSelected ? WeaponSelectionFrameColor : FLinearColor::Transparent);
	}
}

void UTMWeaponLayerWidget::ApplyHoverScale() const
{
	if (UOverlay* Overlay = IconOverlay.Get())
	{
		const UButton* Button = WeaponButton.Get();
		const float HoverScale = Button && Button->IsHovered() ? 1.07f : 1.0f;
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
