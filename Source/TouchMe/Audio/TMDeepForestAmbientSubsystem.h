// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "TMDeepForestAmbientSubsystem.generated.h"

class UAudioComponent;
class USoundBase;

UCLASS()
class TOUCHME_API UTMDeepForestAmbientSubsystem final : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	UTMDeepForestAmbientSubsystem();

	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

private:
	static bool IsDeepForestWorld(const UWorld* World);
	USoundBase* ResolveDeepForestAmbientSound() const;

	void ResetForWorld(UWorld* World);
	void StartAmbient(UWorld* World);
	void StopAmbient(bool bImmediate);

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> AmbientComponent;

	UPROPERTY()
	TObjectPtr<USoundBase> DeepForestAmbientSound;

	TWeakObjectPtr<UWorld> ActiveWorld;
	float TimeUntilNextScan = 0.0f;
	bool bMissingSoundLogged = false;
};
