// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Slate/WidgetTransform.h"
#include "Styling/SlateTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "TMMainMenuHoverStyleSubsystem.generated.h"

class UButton;
class UTextBlock;
class UWidget;

UCLASS()
class TOUCHME_API UTMMainMenuHoverStyleSubsystem final : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

private:
	struct FTrackedLabelStyle
	{
		FSlateFontInfo NormalFont;
		FWidgetTransform NormalTransform;
		FVector2D NormalPivot = FVector2D::ZeroVector;
		FSlateColor NormalColor;
		bool bHovered = false;
	};

	struct FTrackedAttachmentTopButtonStyle
	{
		FButtonStyle NormalStyle;
		FWidgetTransform NormalTransform;
		FVector2D NormalPivot = FVector2D::ZeroVector;
	};

	void ApplyMainMenuHoverStyle();
	void ApplyQuitConfirmationStyle(class UUserWidget* Widget, TSet<TWeakObjectPtr<UTextBlock>>& SeenLabels);
	void ApplyAttachmentTopButtonPressedStyle(class UUserWidget* Widget, TSet<TWeakObjectPtr<UButton>>& SeenButtons);
	void ApplyAttachmentPanelLayout(class UUserWidget* Widget);
	void SetMainMenuButtonHoverSound(UButton* Button);
	void SetAttachmentTopButtonPressed(UButton* Button, FName GroupName, bool bPressed);
	static bool IsMainMenuWidget(const class UUserWidget* Widget);
	static bool IsAttachmentTopButtonWidget(const class UUserWidget* Widget);
	static bool IsMainMenuLargeLabel(const FString& Text);
	static UTextBlock* FindLargeLabelText(class UWidget* RootWidget);
	static UTextBlock* FindQuitOptionLabelText(class UWidget* RootWidget);
	void SetLabelHovered(UTextBlock* TextBlock, bool bHovered);
	void SetQuitOptionHovered(UTextBlock* TextBlock, bool bHovered);

	TMap<TWeakObjectPtr<UTextBlock>, FTrackedLabelStyle> TrackedLabels;
	TMap<TWeakObjectPtr<UTextBlock>, FTrackedLabelStyle> TrackedQuitOptionLabels;
	TMap<TWeakObjectPtr<UButton>, FButtonStyle> TrackedMainMenuButtonStyles;
	TMap<TWeakObjectPtr<UButton>, FTrackedAttachmentTopButtonStyle> TrackedAttachmentTopButtonStyles;
	FName SelectedAttachmentTopButtonGroup;
};
