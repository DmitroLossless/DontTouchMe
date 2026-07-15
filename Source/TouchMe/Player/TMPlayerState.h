#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"

#include "TMPlayerState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTMSelectedCharacterSkinIdChanged, FName, NewSkinId, FName, PreviousSkinId);

UCLASS(Blueprintable)
class TOUCHME_API ATMPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	ATMPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "TouchMe|Skin")
	void SetSelectedCharacterSkinId(FName SkinId);

	UFUNCTION(BlueprintPure, Category = "TouchMe|Skin")
	FName GetSelectedCharacterSkinId() const { return SelectedCharacterSkinId; }

	void SetSelectedCharacterSkinIdFromServer(FName SkinId, bool bUpdatePawn);

	UPROPERTY(BlueprintAssignable, Category = "TouchMe|Skin")
	FTMSelectedCharacterSkinIdChanged OnSelectedCharacterSkinIdChanged;

protected:
	UPROPERTY(ReplicatedUsing = OnRep_SelectedCharacterSkinId, BlueprintReadOnly, Category = "TouchMe|Skin")
	FName SelectedCharacterSkinId = NAME_None;

private:
	UFUNCTION(Server, Reliable)
	void ServerSetSelectedCharacterSkinId(FName SkinId);

	UFUNCTION()
	void OnRep_SelectedCharacterSkinId(FName PreviousSkinId);

	void SetSelectedCharacterSkinIdInternal(FName SkinId, bool bUpdatePawn);
	void ApplySelectedCharacterSkinToPawn();
};
