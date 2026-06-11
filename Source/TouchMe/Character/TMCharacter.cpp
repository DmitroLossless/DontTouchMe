// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMCharacter.h"

ATMCharacter::ATMCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void ATMCharacter::BeginPlay()
{
	Super::BeginPlay();

	UpdateLocalPlayerControlledFlag();
}

void ATMCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateLocalPlayerControlledFlag();
}

void ATMCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	UpdateLocalPlayerControlledFlag();
}

void ATMCharacter::UnPossessed()
{
	Super::UnPossessed();

	UpdateLocalPlayerControlledFlag();
}

void ATMCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();

	UpdateLocalPlayerControlledFlag();
}

void ATMCharacter::UpdateLocalPlayerControlledFlag()
{
	bIsLocalPlayerControlled = IsPlayerControlled() && IsLocallyControlled();
}
