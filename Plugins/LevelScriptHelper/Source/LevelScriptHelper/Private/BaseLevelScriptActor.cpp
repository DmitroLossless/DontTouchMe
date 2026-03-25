// Fill out your copyright notice in the Description page of Project Settings.


#include "BaseLevelScriptActor.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

ABaseLevelScriptActor::ABaseLevelScriptActor(const FObjectInitializer &ObjectInitializer) : Super(ObjectInitializer)
{

}

ABaseLevelScriptActor::~ABaseLevelScriptActor()
{

}

void ABaseLevelScriptActor::BeginPlay()
{

	Super::BeginPlay();



	// ...

}

void ABaseLevelScriptActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{

	Super::EndPlay(EndPlayReason);

	// ...
}

void ABaseLevelScriptActor::BeginActorInteracting_BP_Implementation(AActor *WasInteracted, const ULevelSequence *LevelSequence)
{

	//
}

void ABaseLevelScriptActor::EndActorInteracting_BP_Implementation(AActor *WasInteracted)
{

	//
}

void ABaseLevelScriptActor::BeginActorInteracting(AActor *WasInteracted, const ULevelSequence *LevelSequence)
{
	BeginActorInteracting_BP(WasInteracted, LevelSequence);
}

void ABaseLevelScriptActor::BeginSceneInteracting_BP_Implementation(TArray<AActor *> &AllInteracted, const ULevelSequence *LevelSequence)
{

	//
}

void ABaseLevelScriptActor::BeginSceneInteracting(TArray<AActor *> &AllInteracted, const ULevelSequence *LevelSequence)
{
	BeginSceneInteracting_BP(AllInteracted, LevelSequence);
}


void ABaseLevelScriptActor::DoItemAction_BP_Implementation(AActor *Item)
{

	//
}

void ABaseLevelScriptActor::DoItemAction(AActor *Item)
{
	DoItemAction_BP(Item);
}