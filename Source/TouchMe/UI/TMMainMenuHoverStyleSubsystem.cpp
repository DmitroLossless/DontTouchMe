// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMMainMenuHoverStyleSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/ContentWidget.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
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
	const FLinearColor MainMenuDialogYellow(0.672443f, 0.381326f, 0.025187f, 1.0f);
	const FLinearColor MainMenuDialogHoverRed(1.0f, 0.0f, 0.0f, 1.0f);
	const FName MainMenuLabelHoverTypeface(TEXT("Light"));

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

	TrackedLabels.Reset();
	TrackedQuitOptionLabels.Reset();
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

bool UTMMainMenuHoverStyleSubsystem::IsMainMenuWidget(const UUserWidget* Widget)
{
	const UClass* WidgetClass = Widget ? Widget->GetClass() : nullptr;
	return WidgetClass && WidgetClass->GetName().Contains(TEXT("W_MainMenu"), ESearchCase::IgnoreCase);
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
