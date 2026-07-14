// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TMDeepForestAmbientSubsystem.generated.h"

class UAudioComponent;
class USoundBase;

UCLASS()
class TOUCHME_API UTMDeepForestAmbientSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UTMDeepForestAmbientSubsystem();

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual bool IsTickable() const override;

private:
	static bool IsDeepForestWorld(const UWorld* World);
	USoundBase* ResolveDeepForestAmbientSound() const;

	void StartAmbient(UWorld* World);
	void StopAmbient(bool bImmediate);

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> AmbientComponent;

	UPROPERTY()
	TObjectPtr<USoundBase> DeepForestAmbientSound;

	float TimeUntilNextEnsure = 0.0f;
	bool bDeepForestWorld = false;
	bool bMissingSoundLogged = false;
	bool bCreateComponentFailedLogged = false;
};
