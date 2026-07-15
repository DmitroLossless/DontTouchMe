#include "TMPlayerState.h"

#include "../Character/TMCharacter.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "Net/UnrealNetwork.h"

ATMPlayerState::ATMPlayerState()
{
	bReplicates = true;
}

void ATMPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATMPlayerState, SelectedCharacterSkinId);
}

void ATMPlayerState::SetSelectedCharacterSkinId(FName SkinId)
{
	if (HasAuthority())
	{
		SetSelectedCharacterSkinIdInternal(SkinId, true);
		return;
	}

	ServerSetSelectedCharacterSkinId(SkinId);
}

void ATMPlayerState::SetSelectedCharacterSkinIdFromServer(FName SkinId, bool bUpdatePawn)
{
	if (!HasAuthority())
	{
		return;
	}

	SetSelectedCharacterSkinIdInternal(SkinId, bUpdatePawn);
}

void ATMPlayerState::ServerSetSelectedCharacterSkinId_Implementation(FName SkinId)
{
	SetSelectedCharacterSkinIdInternal(SkinId, true);
}

void ATMPlayerState::OnRep_SelectedCharacterSkinId(FName PreviousSkinId)
{
	OnSelectedCharacterSkinIdChanged.Broadcast(SelectedCharacterSkinId, PreviousSkinId);
}

void ATMPlayerState::SetSelectedCharacterSkinIdInternal(FName SkinId, bool bUpdatePawn)
{
	if (SelectedCharacterSkinId == SkinId)
	{
		if (bUpdatePawn)
		{
			ApplySelectedCharacterSkinToPawn();
		}
		return;
	}

	const FName PreviousSkinId = SelectedCharacterSkinId;
	SelectedCharacterSkinId = SkinId;
	OnSelectedCharacterSkinIdChanged.Broadcast(SelectedCharacterSkinId, PreviousSkinId);

	if (bUpdatePawn)
	{
		ApplySelectedCharacterSkinToPawn();
	}
}

void ATMPlayerState::ApplySelectedCharacterSkinToPawn()
{
	AController* OwningController = Cast<AController>(GetOwner());
	if (!OwningController)
	{
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<AController> It(World); It; ++It)
			{
				AController* Controller = *It;
				if (Controller && Controller->PlayerState == this)
				{
					OwningController = Controller;
					break;
				}
			}
		}
	}

	if (!OwningController)
	{
		return;
	}

	ATMCharacter* Character = Cast<ATMCharacter>(OwningController->GetPawn());
	if (!Character)
	{
		return;
	}

	Character->ApplyCharacterSkinFromPlayerState();
}
