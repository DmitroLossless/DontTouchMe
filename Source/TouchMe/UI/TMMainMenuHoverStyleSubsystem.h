// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Slate/WidgetTransform.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "TMMainMenuHoverStyleSubsystem.generated.h"

class UTextBlock;

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
		bool bHovered = false;
	};

	void ApplyMainMenuHoverStyle();
	static bool IsMainMenuWidget(const class UUserWidget* Widget);
	static bool IsMainMenuLargeLabel(const FString& Text);
	static UTextBlock* FindLargeLabelText(class UWidget* RootWidget);
	void SetLabelHovered(UTextBlock* TextBlock, bool bHovered);

	TMap<TWeakObjectPtr<UTextBlock>, FTrackedLabelStyle> TrackedLabels;
};
