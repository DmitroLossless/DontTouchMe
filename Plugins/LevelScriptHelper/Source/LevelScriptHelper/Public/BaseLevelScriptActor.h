// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/LevelScriptActor.h"
#include "BaseLevelScriptActor.generated.h"

/**
 *
 */
UCLASS(ClassGroup = "Atom|LevelScriptActor", DisplayName = "BaseLevelScriptActor", BlueprintType, Blueprintable)
class LEVELSCRIPTHELPER_API ABaseLevelScriptActor : public ALevelScriptActor
{
	GENERATED_BODY()

public:
	ABaseLevelScriptActor(const FObjectInitializer& ObjectInitializer);
	~ABaseLevelScriptActor();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	UFUNCTION(Category = "Atom|LevelScriptHelper", BlueprintNativeEvent)
	void BeginActorInteracting_BP(AActor *WasInteracted, const ULevelSequence *LevelSequence);
	virtual void BeginActorInteracting_BP_Implementation(AActor *WasInteracted, const ULevelSequence *LevelSequence);

	UFUNCTION(Category = "Atom|LevelScriptHelper", BlueprintNativeEvent)
	void EndActorInteracting_BP(AActor *WasInteracted);
	virtual void EndActorInteracting_BP_Implementation(AActor *WasInteracted);

	UFUNCTION()
	virtual void BeginActorInteracting(AActor *WasInteracted, const ULevelSequence *LevelSequence);

	UFUNCTION(Category = "Atom|LevelScriptHelper", BlueprintNativeEvent)
	void BeginSceneInteracting_BP(TArray<AActor *> &AllInteracted, const ULevelSequence *LevelSequence);
	virtual void BeginSceneInteracting_BP_Implementation(TArray<AActor *> &AllInteracted, const ULevelSequence *LevelSequence);

	UFUNCTION()
	virtual void BeginSceneInteracting(TArray<AActor *> &AllInteracted, const ULevelSequence *LevelSequence);


	UFUNCTION()
	virtual void DoItemAction(AActor *Item);

	UFUNCTION(Category = "Atom|LevelScriptHelper", BlueprintNativeEvent)
	void DoItemAction_BP(AActor *Item);
	virtual void DoItemAction_BP_Implementation(AActor *Item);


	UFUNCTION(Category = "Atom|LevelScriptHelper", BlueprintNativeEvent)
	void UsingAnotherPawn_BP(APawn *User, AActor *Vehicle);
	virtual void UsingAnotherPawn_BP_Implementation(APawn *User, AActor *Vehicle) {}

};
