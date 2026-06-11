// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TMCharacter.generated.h"

UCLASS(Blueprintable)
class TOUCHME_API ATMCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ATMCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void OnRep_Controller() override;

protected:
	UPROPERTY(BlueprintReadOnly, Category = "TouchMe|Character")
	bool bIsLocalPlayerControlled = false;

	void UpdateLocalPlayerControlledFlag();
};
