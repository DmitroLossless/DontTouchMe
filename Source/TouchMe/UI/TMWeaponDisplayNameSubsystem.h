// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "TMWeaponDisplayNameSubsystem.generated.h"

class AGun;
class UTextBlock;
class UUserWidget;

UCLASS()
class TOUCHME_API UTMWeaponDisplayNameSubsystem final : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

private:
	static UClass* ResolveWeaponClass(const UUserWidget* Widget);
	static UClass* ResolveWeaponClassFromCandidate(const FString& Candidate);
	static void ApplyWeaponDisplayName(UUserWidget* Widget, const FText& DisplayName);
	static void ApplyWeaponDisplayNamesFromExistingText(UUserWidget* Widget);
	static bool IsLoadoutWidget(const UUserWidget* Widget);
	static bool IsWeaponMenuWidget(const UUserWidget* Widget);

	float TimeUntilNextRefresh = 0.0f;
};
