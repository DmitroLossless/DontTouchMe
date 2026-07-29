// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMMainMenuHoverStyleSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ContentWidget.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/World.h"
#include "Sound/SoundBase.h"
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
	const TCHAR* MainMenuButtonHoverSoundPath =
		TEXT("/Game/Free_UI/CUE/MinimalistRND.MinimalistRND");
	constexpr float AttachmentTopButtonSelectedScale = 1.07f;

	const FName AttachmentTopButtonGroupOptics(TEXT("Optics"));
	const FName AttachmentTopButtonGroupSideRail(TEXT("SideRail"));
	const FName AttachmentTopButtonGroupUnderbarrel(TEXT("Underbarrel"));
	const FName AttachmentTopButtonGroupMuzzle(TEXT("Muzzle"));
	constexpr float AttachmentListRaisedTopY = 315.0f;
	constexpr float AttachmentListMinHeight = 360.0f;
	constexpr float AttachmentStatsBottomMargin = 38.0f;
	constexpr float AttachmentListStatsGap = 18.0f;

	struct FAttachmentTopButtonSpec
	{
		FName ButtonName;
		FName GroupName;
	};

	const FAttachmentTopButtonSpec AttachmentTopButtonSpecs[] =
	{
		{ TEXT("B_Optics"), AttachmentTopButtonGroupOptics },
		{ TEXT("B_Optics_R"), AttachmentTopButtonGroupOptics },
		{ TEXT("B_SideRail"), AttachmentTopButtonGroupSideRail },
		{ TEXT("B_SideRail_R"), AttachmentTopButtonGroupSideRail },
		{ TEXT("B_Underbarrel"), AttachmentTopButtonGroupUnderbarrel },
		{ TEXT("B_Underbarrel_R"), AttachmentTopButtonGroupUnderbarrel },
		{ TEXT("B_Muzzle"), AttachmentTopButtonGroupMuzzle },
		{ TEXT("B_Muzzle_R"), AttachmentTopButtonGroupMuzzle }
	};

	UWidget* FindFirstVisibleWidgetByName(UUserWidget* Widget, const FName* WidgetNames, const int32 WidgetNameCount)
	{
		if (!Widget || !WidgetNames || WidgetNameCount <= 0)
		{
			return nullptr;
		}

		for (int32 NameIndex = 0; NameIndex < WidgetNameCount; ++NameIndex)
		{
			UWidget* Candidate = Widget->GetWidgetFromName(WidgetNames[NameIndex]);
			if (IsValid(Candidate) && Candidate->IsVisible())
			{
				return Candidate;
			}
		}

		return nullptr;
	}

	UCanvasPanelSlot* FindCanvasSlotForWidgetOrAncestor(UWidget* Widget)
	{
		for (UWidget* Candidate = Widget; IsValid(Candidate); )
		{
			if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Candidate->Slot))
			{
				return CanvasSlot;
			}

			UPanelWidget* Parent = Candidate->GetParent();
			Candidate = Parent ? Cast<UWidget>(Parent) : nullptr;
		}

		return nullptr;
	}

	USizeBox* FindSizeBoxForWidgetOrAncestor(UWidget* Widget)
	{
		for (UWidget* Candidate = Widget; IsValid(Candidate); )
		{
			if (USizeBox* SizeBox = Cast<USizeBox>(Candidate))
			{
				return SizeBox;
			}

			UPanelWidget* Parent = Candidate->GetParent();
			Candidate = Parent ? Cast<UWidget>(Parent) : nullptr;
		}

		return nullptr;
	}

	float ResolveAttachmentLayoutHeight(const UUserWidget* Widget)
	{
		if (!Widget)
		{
			return 720.0f;
		}

		const float CachedHeight = Widget->GetCachedGeometry().GetLocalSize().Y;
		return CachedHeight > 100.0f ? CachedHeight : 720.0f;
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

	USoundBase* ResolveMainMenuButtonHoverSound()
	{
		static TWeakObjectPtr<USoundBase> CachedSound;
		if (CachedSound.IsValid())
		{
			return CachedSound.Get();
		}

		USoundBase* Sound = LoadObject<USoundBase>(nullptr, MainMenuButtonHoverSoundPath);
		CachedSound = Sound;
		return Sound;
	}

	FSlateSound MakeMainMenuButtonHoverSlateSound(UObject* ResourceObject)
	{
		FSlateSound Sound;
		Sound.SetResourceObject(ResourceObject);
		return Sound;
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

	for (TPair<TWeakObjectPtr<UButton>, FButtonStyle>& Pair : TrackedMainMenuButtonStyles)
	{
		UButton* Button = Pair.Key.Get();
		if (!IsValid(Button))
		{
			continue;
		}

		Button->SetStyle(Pair.Value);
	}

	for (TPair<TWeakObjectPtr<UButton>, FTrackedAttachmentTopButtonStyle>& Pair : TrackedAttachmentTopButtonStyles)
	{
		UButton* Button = Pair.Key.Get();
		if (!IsValid(Button))
		{
			continue;
		}

		Button->SetStyle(Pair.Value.NormalStyle);
		Button->SetRenderTransform(Pair.Value.NormalTransform);
		Button->SetRenderTransformPivot(Pair.Value.NormalPivot);
	}

	TrackedLabels.Reset();
	TrackedQuitOptionLabels.Reset();
	TrackedMainMenuButtonStyles.Reset();
	TrackedAttachmentTopButtonStyles.Reset();
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
	TSet<TWeakObjectPtr<UButton>> SeenMainMenuButtons;
	TSet<TWeakObjectPtr<UButton>> SeenAttachmentTopButtons;

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

				SeenMainMenuButtons.Add(Button);
				SetMainMenuButtonHoverSound(Button);

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
		ApplyAttachmentTopButtonPressedStyle(Widget, SeenAttachmentTopButtons);
		ApplyAttachmentPanelLayout(Widget);
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

	for (auto It = TrackedMainMenuButtonStyles.CreateIterator(); It; ++It)
	{
		UButton* Button = It.Key().Get();
		if (!IsValid(Button))
		{
			It.RemoveCurrent();
			continue;
		}

		if (!SeenMainMenuButtons.Contains(Button))
		{
			Button->SetStyle(It.Value());
			It.RemoveCurrent();
		}
	}

	for (auto It = TrackedAttachmentTopButtonStyles.CreateIterator(); It; ++It)
	{
		UButton* Button = It.Key().Get();
		if (!IsValid(Button))
		{
			It.RemoveCurrent();
			continue;
		}

		if (!SeenAttachmentTopButtons.Contains(Button))
		{
			Button->SetStyle(It.Value().NormalStyle);
			Button->SetRenderTransform(It.Value().NormalTransform);
			Button->SetRenderTransformPivot(It.Value().NormalPivot);
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

void UTMMainMenuHoverStyleSubsystem::ApplyAttachmentPanelLayout(UUserWidget* Widget)
{
	if (!IsAttachmentTopButtonWidget(Widget))
	{
		return;
	}

	static const FName AttachmentListNames[] =
	{
		TEXT("AttachmentsList"),
		TEXT("AttachmentList"),
		TEXT("AttachmentList_1"),
		TEXT("AttachmentSlots"),
		TEXT("List")
	};
	static const FName AttachmentStatsNames[] =
	{
		TEXT("StatBox"),
		TEXT("Stats")
	};

	UWidget* AttachmentList = FindFirstVisibleWidgetByName(
		Widget,
		AttachmentListNames,
		UE_ARRAY_COUNT(AttachmentListNames));
	UWidget* AttachmentStats = FindFirstVisibleWidgetByName(
		Widget,
		AttachmentStatsNames,
		UE_ARRAY_COUNT(AttachmentStatsNames));
	if (!AttachmentList || !AttachmentStats)
	{
		return;
	}

	const float LayoutHeight = ResolveAttachmentLayoutHeight(Widget);
	UCanvasPanelSlot* StatsCanvasSlot = FindCanvasSlotForWidgetOrAncestor(AttachmentStats);
	float StatsTopY = LayoutHeight - AttachmentStatsBottomMargin - 132.0f;
	if (StatsCanvasSlot)
	{
		FVector2D StatsSize = StatsCanvasSlot->GetSize();
		if (StatsSize.Y <= 1.0f)
		{
			StatsSize.Y = FMath::Max(AttachmentStats->GetDesiredSize().Y, 132.0f);
			StatsCanvasSlot->SetSize(StatsSize);
		}

		FVector2D StatsPosition = StatsCanvasSlot->GetPosition();
		StatsPosition.Y = FMath::Max(0.0f, LayoutHeight - AttachmentStatsBottomMargin - StatsSize.Y);
		StatsCanvasSlot->SetPosition(StatsPosition);
		StatsTopY = StatsPosition.Y;
	}

	const float ListTopY = FMath::Min(AttachmentListRaisedTopY, FMath::Max(0.0f, StatsTopY - AttachmentListMinHeight - AttachmentListStatsGap));
	const float ListHeight = FMath::Max(AttachmentListMinHeight, StatsTopY - ListTopY - AttachmentListStatsGap);
	if (UCanvasPanelSlot* ListCanvasSlot = FindCanvasSlotForWidgetOrAncestor(AttachmentList))
	{
		FVector2D ListPosition = ListCanvasSlot->GetPosition();
		FVector2D ListSize = ListCanvasSlot->GetSize();
		ListPosition.Y = ListTopY;
		ListSize.Y = FMath::Max(ListSize.Y, ListHeight);
		ListCanvasSlot->SetPosition(ListPosition);
		ListCanvasSlot->SetSize(ListSize);
	}

	if (USizeBox* ListSizeBox = FindSizeBoxForWidgetOrAncestor(AttachmentList))
	{
		ListSizeBox->SetHeightOverride(ListHeight);
	}
	else if (UScrollBox* ListScrollBox = Cast<UScrollBox>(AttachmentList))
	{
		ListScrollBox->SetRenderTransform(FWidgetTransform());
	}
}

void UTMMainMenuHoverStyleSubsystem::ApplyAttachmentTopButtonPressedStyle(
	UUserWidget* Widget,
	TSet<TWeakObjectPtr<UButton>>& SeenButtons)
{
	if (!IsAttachmentTopButtonWidget(Widget))
	{
		return;
	}

	for (const FAttachmentTopButtonSpec& Spec : AttachmentTopButtonSpecs)
	{
		UButton* Button = Cast<UButton>(Widget->GetWidgetFromName(Spec.ButtonName));
		if (!IsValid(Button) || !Button->IsVisible())
		{
			continue;
		}

		SeenButtons.Add(Button);
		if (SelectedAttachmentTopButtonGroup.IsNone() || Button->IsPressed())
		{
			SelectedAttachmentTopButtonGroup = Spec.GroupName;
		}

		SetAttachmentTopButtonPressed(Button, Spec.GroupName, SelectedAttachmentTopButtonGroup == Spec.GroupName);
	}
}

bool UTMMainMenuHoverStyleSubsystem::IsMainMenuWidget(const UUserWidget* Widget)
{
	const UClass* WidgetClass = Widget ? Widget->GetClass() : nullptr;
	return WidgetClass && WidgetClass->GetName().Contains(TEXT("W_MainMenu"), ESearchCase::IgnoreCase);
}

bool UTMMainMenuHoverStyleSubsystem::IsAttachmentTopButtonWidget(const UUserWidget* Widget)
{
	const UClass* WidgetClass = Widget ? Widget->GetClass() : nullptr;
	return WidgetClass && WidgetClass->GetName().Contains(TEXT("W_Attachments"), ESearchCase::IgnoreCase);
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

void UTMMainMenuHoverStyleSubsystem::SetAttachmentTopButtonPressed(
	UButton* Button,
	const FName GroupName,
	const bool bPressed)
{
	if (!IsValid(Button) || GroupName.IsNone())
	{
		return;
	}

	FTrackedAttachmentTopButtonStyle* Style = TrackedAttachmentTopButtonStyles.Find(Button);
	if (!Style)
	{
		FTrackedAttachmentTopButtonStyle NewStyle;
		NewStyle.NormalStyle = Button->GetStyle();
		NewStyle.NormalTransform = Button->GetRenderTransform();
		NewStyle.NormalPivot = Button->GetRenderTransformPivot();
		Style = &TrackedAttachmentTopButtonStyles.Add(Button, NewStyle);
	}

	FButtonStyle ButtonStyle = Style->NormalStyle;
	FWidgetTransform Transform = Style->NormalTransform;
	FVector2D Pivot = Style->NormalPivot;

	if (bPressed)
	{
		ButtonStyle
			.SetNormal(Style->NormalStyle.Pressed)
			.SetHovered(Style->NormalStyle.Pressed)
			.SetNormalForeground(Style->NormalStyle.PressedForeground)
			.SetHoveredForeground(Style->NormalStyle.PressedForeground);

		Transform.Scale = Style->NormalTransform.Scale * AttachmentTopButtonSelectedScale;
		Pivot = FVector2D(0.5f, 0.5f);
	}

	Button->SetStyle(ButtonStyle);
	Button->SetRenderTransformPivot(Pivot);
	Button->SetRenderTransform(Transform);
}

void UTMMainMenuHoverStyleSubsystem::SetMainMenuButtonHoverSound(UButton* Button)
{
	if (!IsValid(Button))
	{
		return;
	}

	USoundBase* HoverSound = ResolveMainMenuButtonHoverSound();
	if (!HoverSound)
	{
		return;
	}

	if (!TrackedMainMenuButtonStyles.Contains(Button))
	{
		TrackedMainMenuButtonStyles.Add(Button, Button->GetStyle());
	}

	FButtonStyle Style = Button->GetStyle();
	if (Style.HoveredSlateSound.GetResourceObject() == HoverSound)
	{
		return;
	}

	Style.SetHoveredSound(MakeMainMenuButtonHoverSlateSound(HoverSound));
	Button->SetStyle(Style);
}
