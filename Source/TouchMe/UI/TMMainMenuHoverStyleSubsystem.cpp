// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMMainMenuHoverStyleSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/ContentWidget.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"

namespace
{
	const FName MainMenuButtonNames[] =
	{
		TEXT("B_Multiplayer"),
		TEXT("B_Loadout"),
		TEXT("B_Settings"),
		TEXT("B_Quit"),
		TEXT("B_Join"),
		TEXT("B_Host"),
		TEXT("B_Loadout_Hide"),
		TEXT("B_Settings_Hide")
	};

	constexpr float MainMenuLabelHoverScale = 1.07f;
	constexpr float MainMenuSubmenuLabelHoverFontScale = 1.15f;
	constexpr float QuitConfirmationOptionHoverFontScale = 1.12f;
	constexpr float LoadoutCategoryButtonDisplayScale = 0.30f;
	const FLinearColor MainMenuDialogYellow(0.672443f, 0.381326f, 0.025187f, 1.0f);
	const FLinearColor MainMenuDialogHoverRed(1.0f, 0.0f, 0.0f, 1.0f);
	const FName MainMenuLabelHoverTypeface(TEXT("Light"));

	struct FLoadoutCategoryButtonSpec
	{
		FName ButtonName;
		FName Token;
		FName IconBoxName;
		FName IconImageName;
		FVector2D FallbackNativeSize = FVector2D(128.0f, 64.0f);
	};

	const FLoadoutCategoryButtonSpec LoadoutCategoryButtonSpecs[] =
	{
		{ TEXT("B_Primary"), TEXT("Primary"), TEXT("TM_Primary_CategoryIconBox"), TEXT("TM_Primary_CategoryIconImage"), FVector2D(1881.0f, 560.0f) },
		{ TEXT("B_Secondary"), TEXT("Secondary"), TEXT("TM_Secondary_CategoryIconBox"), TEXT("TM_Secondary_CategoryIconImage"), FVector2D(1688.0f, 639.0f) },
		{ TEXT("B_Special"), TEXT("Special"), TEXT("TM_Special_CategoryIconBox"), TEXT("TM_Special_CategoryIconImage"), FVector2D(1679.0f, 648.0f) },
		{ TEXT("B_Melee"), TEXT("Melee"), TEXT("TM_Melee_CategoryIconBox"), TEXT("TM_Melee_CategoryIconImage"), FVector2D(1672.0f, 645.0f) },
		{ TEXT("B_Explosive"), TEXT("Explosive"), TEXT("TM_Explosive_CategoryIconBox"), TEXT("TM_Explosive_CategoryIconImage"), FVector2D(1685.0f, 654.0f) }
	};

	FString MakeLoadoutCategoryIconObjectPath(const FName Token, const bool bActive)
	{
		const FString TokenString = Token.ToString();
		const FString AssetName = FString::Printf(TEXT("T_Menu_%s_Icon%s"), *TokenString, bActive ? TEXT("_Active") : TEXT(""));
		return FString::Printf(TEXT("/Game/UI/Generated/Icons/%s.%s"), *AssetName, *AssetName);
	}

	UTexture2D* LoadLoadoutCategoryIconTexture(const FName Token, const bool bActive)
	{
		static TMap<FString, TWeakObjectPtr<UTexture2D>> TextureCache;

		const FString CacheKey = Token.ToString() + (bActive ? TEXT("_Active") : TEXT(""));
		if (const TWeakObjectPtr<UTexture2D>* CachedTexture = TextureCache.Find(CacheKey))
		{
			if (CachedTexture->IsValid())
			{
				return CachedTexture->Get();
			}
		}

		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *MakeLoadoutCategoryIconObjectPath(Token, bActive));
		TextureCache.Add(CacheKey, Texture);
		return Texture;
	}

	FVector2D GetLoadoutCategoryTextureSize(const UTexture2D* Texture)
	{
		if (!Texture)
		{
			return FVector2D::ZeroVector;
		}

		const int32 TextureWidth = Texture->GetSizeX();
		const int32 TextureHeight = Texture->GetSizeY();
		if (TextureWidth > 0 && TextureHeight > 0)
		{
			return FVector2D(static_cast<float>(TextureWidth), static_cast<float>(TextureHeight));
		}

		return FVector2D::ZeroVector;
	}

	FVector2D GetLoadoutCategoryNativeSize(
		const FLoadoutCategoryButtonSpec& Spec,
		const UTexture2D* NormalTexture,
		const UImage* IconImage)
	{
		const FVector2D NormalTextureSize = GetLoadoutCategoryTextureSize(NormalTexture);
		if (NormalTextureSize.X > 0.0f && NormalTextureSize.Y > 0.0f)
		{
			return NormalTextureSize;
		}

		if (IconImage)
		{
			const FSlateBrush Brush = IconImage->GetBrush();
			if (const UTexture2D* Texture = Cast<UTexture2D>(Brush.GetResourceObject()))
			{
				const FVector2D BrushTextureSize = GetLoadoutCategoryTextureSize(Texture);
				if (BrushTextureSize.X > 0.0f && BrushTextureSize.Y > 0.0f)
				{
					return BrushTextureSize;
				}
			}

			const FVector2D BrushSize = Brush.GetImageSize();
			if (BrushSize.X > 0.0f && BrushSize.Y > 0.0f)
			{
				return BrushSize;
			}
		}

		return Spec.FallbackNativeSize;
	}

	FVector2D GetLoadoutCategoryDisplaySize(const FVector2D NativeSize, const UImage* IconImage)
	{
		const FVector2D DefaultSize = NativeSize * LoadoutCategoryButtonDisplayScale;
		if (!IconImage || NativeSize.X <= 0.0f || NativeSize.Y <= 0.0f)
		{
			return DefaultSize;
		}

		const float LayoutHeight = IconImage->GetCachedGeometry().GetLocalSize().Y;
		if (LayoutHeight <= 1.0f || LayoutHeight >= DefaultSize.Y)
		{
			return DefaultSize;
		}

		return FVector2D((NativeSize.X / NativeSize.Y) * LayoutHeight, LayoutHeight);
	}

	bool IsLoadoutCategoryRulerEnabled()
	{
		static const bool bEnabled = []()
		{
			return FPlatformMisc::GetEnvironmentVariable(TEXT("DTM_MENU_ICON_RULER")).Equals(TEXT("1"));
		}();

		return bEnabled;
	}

	void AppendLoadoutCategoryRulerSample(
		const FLoadoutCategoryButtonSpec& Spec,
		const UImage* IconImage,
		const bool bHovered,
		const FVector2D SourceSize,
		const FVector2D RequestedDrawSize)
	{
		if (!IsLoadoutCategoryRulerEnabled() || !IconImage)
		{
			return;
		}

		const FGeometry& Geometry = IconImage->GetCachedGeometry();
		const FVector2D LocalSize = Geometry.GetLocalSize();
		if (LocalSize.X <= 0.0f || LocalSize.Y <= 0.0f)
		{
			return;
		}

		const FVector2D TopLeft = Geometry.LocalToAbsolute(FVector2D::ZeroVector);
		const FVector2D BottomRight = Geometry.LocalToAbsolute(LocalSize);
		const FString OutputPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AI_MenuHoverAspectImport"), TEXT("menu_icon_draw_rects.csv"));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);

		static TSet<FString> WrittenKeys;
		static bool bHeaderWritten = false;
		if (!bHeaderWritten && !FPaths::FileExists(OutputPath))
		{
			FFileHelper::SaveStringToFile(
				TEXT("Icon,State,SourceW,SourceH,RequestedW,RequestedH,Left,Top,Right,Bottom,Width,Height,SourceRatio,DrawRatio,RatioDelta\n"),
				*OutputPath);
		}
		bHeaderWritten = true;

		const FString State = bHovered ? TEXT("hover") : TEXT("normal");
		const int32 Left = FMath::RoundToInt(TopLeft.X);
		const int32 Top = FMath::RoundToInt(TopLeft.Y);
		const int32 Right = FMath::RoundToInt(BottomRight.X);
		const int32 Bottom = FMath::RoundToInt(BottomRight.Y);
		const int32 Width = Right - Left;
		const int32 Height = Bottom - Top;
		const double SourceRatio = SourceSize.Y > 0.0f ? static_cast<double>(SourceSize.X / SourceSize.Y) : 0.0;
		const double DrawRatio = Height > 0 ? static_cast<double>(Width) / static_cast<double>(Height) : 0.0;
		const double RatioDelta = FMath::Abs(SourceRatio - DrawRatio);

		const FString Key = FString::Printf(TEXT("%s_%s_%d_%d_%d_%d"), *Spec.Token.ToString(), *State, Left, Top, Width, Height);
		if (WrittenKeys.Contains(Key))
		{
			return;
		}
		WrittenKeys.Add(Key);

		const FString Line = FString::Printf(
			TEXT("%s,%s,%.0f,%.0f,%.0f,%.0f,%d,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f\n"),
			*Spec.Token.ToString(),
			*State,
			SourceSize.X,
			SourceSize.Y,
			RequestedDrawSize.X,
			RequestedDrawSize.Y,
			Left,
			Top,
			Right,
			Bottom,
			Width,
			Height,
			SourceRatio,
			DrawRatio,
			RatioDelta);
		FFileHelper::SaveStringToFile(Line, *OutputPath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
	}

	void ApplyLoadoutCategoryIconTexture(
		const FLoadoutCategoryButtonSpec& Spec,
		UImage* IconImage,
		const bool bHovered,
		const FVector2D DrawSize)
	{
		if (!IconImage)
		{
			return;
		}

		UTexture2D* NormalTexture = LoadLoadoutCategoryIconTexture(Spec.Token, false);
		UTexture2D* ActiveTexture = LoadLoadoutCategoryIconTexture(Spec.Token, true);
		UTexture2D* DesiredTexture = (bHovered && ActiveTexture) ? ActiveTexture : NormalTexture;
		if (!DesiredTexture)
		{
			return;
		}

		FSlateBrush Brush = IconImage->GetBrush();
		if (Brush.GetResourceObject() != DesiredTexture
			|| !Brush.GetImageSize().Equals(DrawSize, 0.5f)
			|| Brush.DrawAs != ESlateBrushDrawType::Image)
		{
			Brush.DrawAs = ESlateBrushDrawType::Image;
			Brush.SetResourceObject(DesiredTexture);
			Brush.SetImageSize(DrawSize);
			Brush.TintColor = FSlateColor(FLinearColor::White);
			IconImage->SetBrush(Brush);
		}
	}

	void VisitMainMenuHoverWidgetTree(UWidget* Widget, TFunctionRef<void(UWidget*)> Visitor)
	{
		if (!Widget)
		{
			return;
		}

		Visitor(Widget);

		if (UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget))
		{
			const int32 ChildCount = PanelWidget->GetChildrenCount();
			for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
			{
				VisitMainMenuHoverWidgetTree(PanelWidget->GetChildAt(ChildIndex), Visitor);
			}
			return;
		}

		if (UContentWidget* ContentWidget = Cast<UContentWidget>(Widget))
		{
			VisitMainMenuHoverWidgetTree(ContentWidget->GetContent(), Visitor);
		}
	}

	bool IsMainMenuHoverWidgetTreeHovered(UWidget* RootWidget)
	{
		bool bHovered = false;
		VisitMainMenuHoverWidgetTree(RootWidget, [&bHovered](UWidget* Widget)
		{
			if (!bHovered && Widget && Widget->IsHovered())
			{
				bHovered = true;
			}
		});

		return bHovered;
	}

	bool IsMainMenuSubmenuLabel(const FString& Text)
	{
		const FString TrimmedText = Text.TrimStartAndEnd();
		return TrimmedText.Equals(TEXT("Join Match"), ESearchCase::IgnoreCase)
			|| TrimmedText.Equals(TEXT("Host Match"), ESearchCase::IgnoreCase);
	}

	bool IsQuitConfirmationTitle(const FString& Text)
	{
		return Text.TrimStartAndEnd().Equals(TEXT("QUIT TO DESKTOP"), ESearchCase::IgnoreCase);
	}

	bool IsQuitConfirmationQuestion(const FString& Text)
	{
		return Text.TrimStartAndEnd().Contains(TEXT("quit the game"), ESearchCase::IgnoreCase);
	}

	bool IsQuitConfirmationOption(const FString& Text)
	{
		const FString TrimmedText = Text.TrimStartAndEnd();
		return TrimmedText.Equals(TEXT("Yes"), ESearchCase::IgnoreCase)
			|| TrimmedText.Equals(TEXT("No"), ESearchCase::IgnoreCase);
	}

	bool IsQuitConfirmationWidget(const UUserWidget* Widget)
	{
		const UClass* WidgetClass = Widget ? Widget->GetClass() : nullptr;
		return WidgetClass && WidgetClass->GetName().Contains(TEXT("W_PopUpMessage"), ESearchCase::IgnoreCase);
	}

	void ClearQuitConfirmationBackgrounds(UUserWidget* Widget)
	{
		if (!IsQuitConfirmationWidget(Widget) || !Widget || !Widget->WidgetTree)
		{
			return;
		}

		VisitMainMenuHoverWidgetTree(Widget->WidgetTree->RootWidget, [](UWidget* ChildWidget)
		{
			if (UBorder* Border = Cast<UBorder>(ChildWidget))
			{
				FSlateBrush Brush = Border->Background;
				Brush.TintColor = FSlateColor(FLinearColor::Transparent);
				Border->SetBrush(Brush);
				Border->SetBrushColor(FLinearColor::Transparent);
				return;
			}

			if (UImage* Image = Cast<UImage>(ChildWidget))
			{
				FSlateBrush Brush = Image->GetBrush();
				Brush.TintColor = FSlateColor(FLinearColor::Transparent);
				Image->SetBrush(Brush);
				Image->SetColorAndOpacity(FLinearColor::Transparent);
			}
		});
	}
}

void UTMMainMenuHoverStyleSubsystem::Deinitialize()
{
	for (TPair<TWeakObjectPtr<UTextBlock>, FTrackedLabelStyle>& Pair : TrackedLabels)
	{
		UTextBlock* TextBlock = Pair.Key.Get();
		if (!IsValid(TextBlock))
		{
			continue;
		}

		TextBlock->SetFont(Pair.Value.NormalFont);
		TextBlock->SetRenderTransform(Pair.Value.NormalTransform);
		TextBlock->SetRenderTransformPivot(Pair.Value.NormalPivot);
	}

	for (TPair<TWeakObjectPtr<UTextBlock>, FTrackedLabelStyle>& Pair : TrackedQuitOptionLabels)
	{
		UTextBlock* TextBlock = Pair.Key.Get();
		if (!IsValid(TextBlock))
		{
			continue;
		}

		TextBlock->SetFont(Pair.Value.NormalFont);
		TextBlock->SetRenderTransform(Pair.Value.NormalTransform);
		TextBlock->SetRenderTransformPivot(Pair.Value.NormalPivot);
		TextBlock->SetColorAndOpacity(Pair.Value.NormalColor);
	}

	for (TPair<TWeakObjectPtr<UWidget>, FTrackedWidgetTransform>& Pair : TrackedLoadoutCategoryButtons)
	{
		UWidget* Widget = Pair.Key.Get();
		if (!IsValid(Widget))
		{
			continue;
		}

		Widget->SetRenderTransform(Pair.Value.NormalTransform);
		Widget->SetRenderTransformPivot(Pair.Value.NormalPivot);
	}

	TrackedLabels.Reset();
	TrackedQuitOptionLabels.Reset();
	TrackedLoadoutCategoryButtons.Reset();
	Super::Deinitialize();
}

void UTMMainMenuHoverStyleSubsystem::Tick(const float DeltaTime)
{
	(void)DeltaTime;
	ApplyMainMenuHoverStyle();
}

TStatId UTMMainMenuHoverStyleSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTMMainMenuHoverStyleSubsystem, STATGROUP_Tickables);
}

bool UTMMainMenuHoverStyleSubsystem::IsTickable() const
{
	return !HasAnyFlags(RF_ClassDefaultObject);
}

void UTMMainMenuHoverStyleSubsystem::ApplyMainMenuHoverStyle()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	TSet<TWeakObjectPtr<UTextBlock>> SeenLabels;
	TSet<TWeakObjectPtr<UWidget>> SeenLoadoutCategoryButtons;

	for (TObjectIterator<UUserWidget> WidgetIt; WidgetIt; ++WidgetIt)
	{
		UUserWidget* Widget = *WidgetIt;
		if (!IsValid(Widget) || Widget->GetWorld() != World || !Widget->IsVisible())
		{
			continue;
		}

		if (IsMainMenuWidget(Widget))
		{
			for (const FName ButtonName : MainMenuButtonNames)
			{
				UButton* Button = Cast<UButton>(Widget->GetWidgetFromName(ButtonName));
				if (!IsValid(Button))
				{
					continue;
				}

				UTextBlock* Label = FindLargeLabelText(Button);
				if (!IsValid(Label))
				{
					continue;
				}

				SeenLabels.Add(Label);
				SetLabelHovered(Label, Button->IsHovered() || IsMainMenuHoverWidgetTreeHovered(Button));
			}
		}

		ApplyQuitConfirmationStyle(Widget, SeenLabels);
		ApplyLoadoutCategoryButtonStyle(Widget, SeenLoadoutCategoryButtons);
	}

	for (auto It = TrackedLabels.CreateIterator(); It; ++It)
	{
		UTextBlock* TextBlock = It.Key().Get();
		if (!IsValid(TextBlock))
		{
			It.RemoveCurrent();
			continue;
		}

		if (!SeenLabels.Contains(TextBlock))
		{
			SetLabelHovered(TextBlock, false);
		}
	}

	for (auto It = TrackedQuitOptionLabels.CreateIterator(); It; ++It)
	{
		UTextBlock* TextBlock = It.Key().Get();
		if (!IsValid(TextBlock))
		{
			It.RemoveCurrent();
			continue;
		}

		if (!SeenLabels.Contains(TextBlock))
		{
			TextBlock->SetFont(It.Value().NormalFont);
			TextBlock->SetRenderTransform(It.Value().NormalTransform);
			TextBlock->SetRenderTransformPivot(It.Value().NormalPivot);
			TextBlock->SetColorAndOpacity(It.Value().NormalColor);
			It.RemoveCurrent();
		}
	}

	for (auto It = TrackedLoadoutCategoryButtons.CreateIterator(); It; ++It)
	{
		UWidget* Widget = It.Key().Get();
		if (!IsValid(Widget))
		{
			It.RemoveCurrent();
			continue;
		}

		if (!SeenLoadoutCategoryButtons.Contains(Widget))
		{
			Widget->SetRenderTransform(It.Value().NormalTransform);
			Widget->SetRenderTransformPivot(It.Value().NormalPivot);
			It.RemoveCurrent();
		}
	}
}

void UTMMainMenuHoverStyleSubsystem::ApplyQuitConfirmationStyle(
	UUserWidget* Widget,
	TSet<TWeakObjectPtr<UTextBlock>>& SeenLabels)
{
	if (!Widget || !Widget->WidgetTree || !Widget->WidgetTree->RootWidget)
	{
		return;
	}

	bool bHasQuitConfirmationText = false;
	VisitMainMenuHoverWidgetTree(Widget->WidgetTree->RootWidget, [&bHasQuitConfirmationText](UWidget* ChildWidget)
	{
		const UTextBlock* TextBlock = Cast<UTextBlock>(ChildWidget);
		if (!TextBlock || !TextBlock->IsVisible())
		{
			return;
		}

		const FString Text = TextBlock->GetText().ToString();
		bHasQuitConfirmationText = bHasQuitConfirmationText
			|| IsQuitConfirmationTitle(Text)
			|| IsQuitConfirmationQuestion(Text);
	});

	if (!bHasQuitConfirmationText)
	{
		return;
	}

	ClearQuitConfirmationBackgrounds(Widget);

	VisitMainMenuHoverWidgetTree(Widget->WidgetTree->RootWidget, [](UWidget* ChildWidget)
	{
		UTextBlock* TextBlock = Cast<UTextBlock>(ChildWidget);
		if (TextBlock && TextBlock->IsVisible() && IsQuitConfirmationTitle(TextBlock->GetText().ToString()))
		{
			TextBlock->SetColorAndOpacity(FSlateColor(MainMenuDialogYellow));
		}
	});

	VisitMainMenuHoverWidgetTree(Widget->WidgetTree->RootWidget, [this, &SeenLabels](UWidget* ChildWidget)
	{
		UButton* Button = Cast<UButton>(ChildWidget);
		if (!IsValid(Button) || !Button->IsVisible())
		{
			return;
		}

		UTextBlock* Label = FindQuitOptionLabelText(Button);
		if (!IsValid(Label) || !Label->IsVisible())
		{
			return;
		}

		SeenLabels.Add(Label);
		SetQuitOptionHovered(Label, Button->IsHovered() || IsMainMenuHoverWidgetTreeHovered(Button));
	});
}

void UTMMainMenuHoverStyleSubsystem::ApplyLoadoutCategoryButtonStyle(
	UUserWidget* Widget,
	TSet<TWeakObjectPtr<UWidget>>& SeenButtons)
{
	if (!IsLoadoutCategoryWidget(Widget))
	{
		return;
	}

	for (const FLoadoutCategoryButtonSpec& Spec : LoadoutCategoryButtonSpecs)
	{
		UButton* Button = Cast<UButton>(Widget->GetWidgetFromName(Spec.ButtonName));
		if (!IsValid(Button) || !Button->IsVisible())
		{
			continue;
		}

		UImage* IconImage = Cast<UImage>(Widget->GetWidgetFromName(Spec.IconImageName));
		UTexture2D* NormalTexture = LoadLoadoutCategoryIconTexture(Spec.Token, false);
		const FVector2D NativeSize = GetLoadoutCategoryNativeSize(Spec, NormalTexture, IconImage);
		const FVector2D DisplaySize = GetLoadoutCategoryDisplaySize(NativeSize, IconImage);
		const bool bHovered = Button->IsHovered() || IsMainMenuHoverWidgetTreeHovered(Button);

		if (USizeBox* IconBox = Cast<USizeBox>(Widget->GetWidgetFromName(Spec.IconBoxName)))
		{
			IconBox->SetWidthOverride(DisplaySize.X);
			IconBox->SetHeightOverride(DisplaySize.Y);
			IconBox->SetVisibility(ESlateVisibility::HitTestInvisible);
		}

		if (IconImage)
		{
			ApplyLoadoutCategoryIconTexture(Spec, IconImage, bHovered, DisplaySize);
			IconImage->SetDesiredSizeOverride(DisplaySize);
			IconImage->SetColorAndOpacity(FLinearColor::White);
			IconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
			AppendLoadoutCategoryRulerSample(Spec, IconImage, bHovered, NativeSize, DisplaySize);
		}

		SeenButtons.Add(Button);
		SetLoadoutCategoryButtonHovered(Button, bHovered);
	}
}

bool UTMMainMenuHoverStyleSubsystem::IsMainMenuWidget(const UUserWidget* Widget)
{
	const UClass* WidgetClass = Widget ? Widget->GetClass() : nullptr;
	return WidgetClass && WidgetClass->GetName().Contains(TEXT("W_MainMenu"), ESearchCase::IgnoreCase);
}

bool UTMMainMenuHoverStyleSubsystem::IsLoadoutCategoryWidget(const UUserWidget* Widget)
{
	const UClass* WidgetClass = Widget ? Widget->GetClass() : nullptr;
	if (!WidgetClass)
	{
		return false;
	}

	const FString ClassName = WidgetClass->GetName();
	return ClassName.Contains(TEXT("W_Loadout"), ESearchCase::IgnoreCase)
		|| ClassName.Contains(TEXT("W_Attachments"), ESearchCase::IgnoreCase)
		|| ClassName.Contains(TEXT("W_MainMenu"), ESearchCase::IgnoreCase);
}

bool UTMMainMenuHoverStyleSubsystem::IsMainMenuLargeLabel(const FString& Text)
{
	const FString TrimmedText = Text.TrimStartAndEnd();
	return TrimmedText.Equals(TEXT("Go"), ESearchCase::IgnoreCase)
		|| TrimmedText.Equals(TEXT("Instrument"), ESearchCase::IgnoreCase)
		|| TrimmedText.Equals(TEXT("Loadout"), ESearchCase::IgnoreCase)
		|| TrimmedText.Equals(TEXT("Settings"), ESearchCase::IgnoreCase)
		|| TrimmedText.Equals(TEXT("Quit"), ESearchCase::IgnoreCase)
		|| TrimmedText.Equals(TEXT("Join Match"), ESearchCase::IgnoreCase)
		|| TrimmedText.Equals(TEXT("Host Match"), ESearchCase::IgnoreCase);
}

UTextBlock* UTMMainMenuHoverStyleSubsystem::FindLargeLabelText(UWidget* RootWidget)
{
	UTextBlock* Result = nullptr;
	VisitMainMenuHoverWidgetTree(RootWidget, [&Result](UWidget* Widget)
	{
		if (Result)
		{
			return;
		}

		UTextBlock* TextBlock = Cast<UTextBlock>(Widget);
		if (!TextBlock || !IsMainMenuLargeLabel(TextBlock->GetText().ToString()))
		{
			return;
		}

		Result = TextBlock;
	});

	return Result;
}

UTextBlock* UTMMainMenuHoverStyleSubsystem::FindQuitOptionLabelText(UWidget* RootWidget)
{
	UTextBlock* Result = nullptr;
	VisitMainMenuHoverWidgetTree(RootWidget, [&Result](UWidget* Widget)
	{
		if (Result)
		{
			return;
		}

		UTextBlock* TextBlock = Cast<UTextBlock>(Widget);
		if (!TextBlock || !IsQuitConfirmationOption(TextBlock->GetText().ToString()))
		{
			return;
		}

		Result = TextBlock;
	});

	return Result;
}

void UTMMainMenuHoverStyleSubsystem::SetLabelHovered(UTextBlock* TextBlock, const bool bHovered)
{
	if (!IsValid(TextBlock))
	{
		return;
	}

	FTrackedLabelStyle* Style = TrackedLabels.Find(TextBlock);
	if (!Style)
	{
		FTrackedLabelStyle NewStyle;
		NewStyle.NormalFont = TextBlock->GetFont();
		NewStyle.NormalTransform = TextBlock->GetRenderTransform();
		NewStyle.NormalPivot = TextBlock->GetRenderTransformPivot();
		NewStyle.NormalColor = TextBlock->GetColorAndOpacity();
		Style = &TrackedLabels.Add(TextBlock, NewStyle);
	}

	if (Style->bHovered == bHovered && !bHovered)
	{
		return;
	}

	Style->bHovered = bHovered;

	if (bHovered)
	{
		const bool bSubmenuLabel = IsMainMenuSubmenuLabel(TextBlock->GetText().ToString());

		FSlateFontInfo HoverFont = Style->NormalFont;
		HoverFont.TypefaceFontName = MainMenuLabelHoverTypeface;
		if (bSubmenuLabel)
		{
			HoverFont.Size = FMath::Max(1, FMath::RoundToInt(Style->NormalFont.Size * MainMenuSubmenuLabelHoverFontScale));
		}

		FWidgetTransform HoverTransform = Style->NormalTransform;
		HoverTransform.Scale = Style->NormalTransform.Scale * (bSubmenuLabel ? 1.0f : MainMenuLabelHoverScale);

		TextBlock->SetFont(HoverFont);
		TextBlock->SetRenderTransformPivot(FVector2D(0.0f, 0.5f));
		TextBlock->SetRenderTransform(HoverTransform);
		return;
	}

	TextBlock->SetFont(Style->NormalFont);
	TextBlock->SetRenderTransform(Style->NormalTransform);
	TextBlock->SetRenderTransformPivot(Style->NormalPivot);
}

void UTMMainMenuHoverStyleSubsystem::SetQuitOptionHovered(UTextBlock* TextBlock, const bool bHovered)
{
	if (!IsValid(TextBlock))
	{
		return;
	}

	FTrackedLabelStyle* Style = TrackedQuitOptionLabels.Find(TextBlock);
	if (!Style)
	{
		FTrackedLabelStyle NewStyle;
		NewStyle.NormalFont = TextBlock->GetFont();
		NewStyle.NormalTransform = TextBlock->GetRenderTransform();
		NewStyle.NormalPivot = TextBlock->GetRenderTransformPivot();
		NewStyle.NormalColor = TextBlock->GetColorAndOpacity();
		Style = &TrackedQuitOptionLabels.Add(TextBlock, NewStyle);
	}

	Style->bHovered = bHovered;

	FSlateFontInfo Font = Style->NormalFont;
	FWidgetTransform Transform = Style->NormalTransform;
	if (bHovered)
	{
		Font.TypefaceFontName = MainMenuLabelHoverTypeface;
		Font.Size = FMath::Max(1, FMath::RoundToInt(Style->NormalFont.Size * QuitConfirmationOptionHoverFontScale));
		Transform.Scale = Style->NormalTransform.Scale * 1.04f;
	}

	TextBlock->SetColorAndOpacity(FSlateColor(bHovered ? MainMenuDialogHoverRed : MainMenuDialogYellow));
	TextBlock->SetFont(Font);
	TextBlock->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	TextBlock->SetRenderTransform(Transform);
}

void UTMMainMenuHoverStyleSubsystem::SetLoadoutCategoryButtonHovered(UWidget* Widget, const bool bHovered)
{
	if (!IsValid(Widget))
	{
		return;
	}

	FTrackedWidgetTransform* Style = TrackedLoadoutCategoryButtons.Find(Widget);
	if (!Style)
	{
		FTrackedWidgetTransform NewStyle;
		NewStyle.NormalTransform = Widget->GetRenderTransform();
		NewStyle.NormalPivot = Widget->GetRenderTransformPivot();
		Style = &TrackedLoadoutCategoryButtons.Add(Widget, NewStyle);
	}

	Style->bHovered = bHovered;

	Widget->SetRenderTransformPivot(Style->NormalPivot);
	Widget->SetRenderTransform(Style->NormalTransform);
}
