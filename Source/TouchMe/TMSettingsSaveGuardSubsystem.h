// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "TMSettingsSaveGuardSubsystem.generated.h"

UCLASS()
class TOUCHME_API UTMSettingsSaveGuardSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual TStatId GetStatId() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;

private:
	bool SanitizeMouseSensitivity(UObject* Object) const;
	void EnsureValidSettingsSave() const;

	double NextSettingsValidationTime = 0.0;
	int32 RemainingSettingsValidationPasses = 10;
};
