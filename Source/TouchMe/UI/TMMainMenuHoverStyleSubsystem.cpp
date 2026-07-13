// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMMainMenuHoverStyleSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/ContentWidget.h"
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
		TEXT("B_Quit")
	};

	constexpr float MainMenuLabelHoverScale = 1.07f;
	const FName MainMenuLabelHoverTypeface(TEXT("Light"));

	void VisitWidgetTree(UWidget* Widget, TFunctionRef<void(UWidget*)> Visitor)
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
				VisitWidgetTree(PanelWidget->GetChildAt(ChildIndex), Visitor);
			}
			return;
		}

		if (UContentWidget* ContentWidget = Cast<UContentWidget>(Widget))
		{
			VisitWidgetTree(ContentWidget->GetContent(), Visitor);
		}
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

	TrackedLabels.Reset();
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
		if (!IsValid(Widget) || Widget->GetWorld() != World || !IsMainMenuWidget(Widget) || !Widget->IsVisible())
		{
			continue;
		}

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
			SetLabelHovered(Label, Button->IsHovered());
		}
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
		|| TrimmedText.Equals(TEXT("Settings"), ESearchCase::IgnoreCase)
		|| TrimmedText.Equals(TEXT("Quit"), ESearchCase::IgnoreCase);
}

UTextBlock* UTMMainMenuHoverStyleSubsystem::FindLargeLabelText(UWidget* RootWidget)
{
	UTextBlock* Result = nullptr;
	VisitWidgetTree(RootWidget, [&Result](UWidget* Widget)
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
		Style = &TrackedLabels.Add(TextBlock, NewStyle);
	}

	if (Style->bHovered == bHovered)
	{
		return;
	}

	Style->bHovered = bHovered;

	if (bHovered)
	{
		FSlateFontInfo HoverFont = Style->NormalFont;
		HoverFont.TypefaceFontName = MainMenuLabelHoverTypeface;

		FWidgetTransform HoverTransform = Style->NormalTransform;
		HoverTransform.Scale = Style->NormalTransform.Scale * MainMenuLabelHoverScale;

		TextBlock->SetFont(HoverFont);
		TextBlock->SetRenderTransformPivot(FVector2D(0.0f, 0.5f));
		TextBlock->SetRenderTransform(HoverTransform);
		return;
	}

	TextBlock->SetFont(Style->NormalFont);
	TextBlock->SetRenderTransform(Style->NormalTransform);
	TextBlock->SetRenderTransformPivot(Style->NormalPivot);
}
