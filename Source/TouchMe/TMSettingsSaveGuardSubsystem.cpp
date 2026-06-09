// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMSettingsSaveGuardSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/SaveGame.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr TCHAR SettingsSlotName[] = TEXT("SaveSettingsSlot");
	constexpr TCHAR SettingsSaveClassPath[] =
		TEXT("/Game/MP_System_V3/Game/Blueprints/SaveFiles/S_SaveData.S_SaveData_C");
	constexpr int32 SettingsUserIndex = 0;
	constexpr double DefaultMouseSensitivity = 1.0;

	bool IsMouseSensitivityProperty(const FProperty* Property)
	{
		return Property && Property->GetName().StartsWith(TEXT("MouseSensitivity"));
	}
}

void UTMSettingsSaveGuardSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EnsureValidSettingsSave();
}

void UTMSettingsSaveGuardSubsystem::Tick(const float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	bool bRuntimeSensitivityFixed = false;
	for (FConstPlayerControllerIterator ControllerIt = World->GetPlayerControllerIterator(); ControllerIt; ++ControllerIt)
	{
		if (APlayerController* PlayerController = ControllerIt->Get())
		{
			bRuntimeSensitivityFixed |= SanitizeMouseSensitivity(PlayerController);
			bRuntimeSensitivityFixed |= SanitizeMouseSensitivity(PlayerController->GetPawn());
		}
	}

	const double CurrentTime = World->GetTimeSeconds();
	if (bRuntimeSensitivityFixed ||
		(RemainingSettingsValidationPasses > 0 && CurrentTime >= NextSettingsValidationTime))
	{
		EnsureValidSettingsSave();

		if (RemainingSettingsValidationPasses > 0)
		{
			--RemainingSettingsValidationPasses;
			NextSettingsValidationTime = CurrentTime + 0.5;
		}
	}
}

bool UTMSettingsSaveGuardSubsystem::IsTickable() const
{
	return !HasAnyFlags(RF_ClassDefaultObject | RF_BeginDestroyed) && GetWorld() != nullptr;
}

ETickableTickType UTMSettingsSaveGuardSubsystem::GetTickableTickType() const
{
	return HasAnyFlags(RF_ClassDefaultObject) ? ETickableTickType::Never : ETickableTickType::Conditional;
}

TStatId UTMSettingsSaveGuardSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTMSettingsSaveGuardSubsystem, STATGROUP_Tickables);
}

UWorld* UTMSettingsSaveGuardSubsystem::GetTickableGameObjectWorld() const
{
	return GetWorld();
}

bool UTMSettingsSaveGuardSubsystem::SanitizeMouseSensitivity(UObject* Object) const
{
	if (!Object)
	{
		return false;
	}

	for (TFieldIterator<FProperty> PropertyIt(Object->GetClass()); PropertyIt; ++PropertyIt)
	{
		FNumericProperty* SensitivityProperty = CastField<FNumericProperty>(*PropertyIt);
		if (!SensitivityProperty || !IsMouseSensitivityProperty(SensitivityProperty))
		{
			continue;
		}

		void* SensitivityData = SensitivityProperty->ContainerPtrToValuePtr<void>(Object);
		const double Sensitivity = SensitivityProperty->IsFloatingPoint()
			? SensitivityProperty->GetFloatingPointPropertyValue(SensitivityData)
			: static_cast<double>(SensitivityProperty->GetSignedIntPropertyValue(SensitivityData));

		if (FMath::IsFinite(Sensitivity) && Sensitivity > UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}

		if (SensitivityProperty->IsFloatingPoint())
		{
			SensitivityProperty->SetFloatingPointPropertyValue(SensitivityData, DefaultMouseSensitivity);
		}
		else
		{
			SensitivityProperty->SetIntPropertyValue(SensitivityData, static_cast<int64>(DefaultMouseSensitivity));
		}

		return true;
	}

	return false;
}

void UTMSettingsSaveGuardSubsystem::EnsureValidSettingsSave() const
{
	USaveGame* SettingsSave = nullptr;
	bool bNeedsSave = false;

	if (UGameplayStatics::DoesSaveGameExist(SettingsSlotName, SettingsUserIndex))
	{
		SettingsSave = UGameplayStatics::LoadGameFromSlot(SettingsSlotName, SettingsUserIndex);
	}

	if (!SettingsSave)
	{
		UClass* SettingsSaveClass = LoadClass<USaveGame>(nullptr, SettingsSaveClassPath);
		if (!SettingsSaveClass)
		{
			UE_LOG(LogTemp, Error, TEXT("Could not load settings save class: %s"), SettingsSaveClassPath);
			return;
		}

		SettingsSave = UGameplayStatics::CreateSaveGameObject(SettingsSaveClass);
		bNeedsSave = true;
	}

	FStructProperty* SettingsProperty = FindFProperty<FStructProperty>(SettingsSave->GetClass(), TEXT("Settings"));
	if (!SettingsProperty)
	{
		UE_LOG(LogTemp, Error, TEXT("Settings save object has no Settings struct property"));
		return;
	}

	void* SettingsData = SettingsProperty->ContainerPtrToValuePtr<void>(SettingsSave);
	for (TFieldIterator<FProperty> PropertyIt(SettingsProperty->Struct); PropertyIt; ++PropertyIt)
	{
		FNumericProperty* SensitivityProperty = CastField<FNumericProperty>(*PropertyIt);
		if (!SensitivityProperty || !IsMouseSensitivityProperty(SensitivityProperty))
		{
			continue;
		}

		void* SensitivityData = SensitivityProperty->ContainerPtrToValuePtr<void>(SettingsData);
		const double Sensitivity = SensitivityProperty->IsFloatingPoint()
			? SensitivityProperty->GetFloatingPointPropertyValue(SensitivityData)
			: static_cast<double>(SensitivityProperty->GetSignedIntPropertyValue(SensitivityData));

		if (!FMath::IsFinite(Sensitivity) || Sensitivity <= UE_KINDA_SMALL_NUMBER)
		{
			if (SensitivityProperty->IsFloatingPoint())
			{
				SensitivityProperty->SetFloatingPointPropertyValue(SensitivityData, DefaultMouseSensitivity);
			}
			else
			{
				SensitivityProperty->SetIntPropertyValue(SensitivityData, static_cast<int64>(DefaultMouseSensitivity));
			}

			bNeedsSave = true;
		}

		break;
	}

	if (bNeedsSave && !UGameplayStatics::SaveGameToSlot(SettingsSave, SettingsSlotName, SettingsUserIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("Could not write settings save slot: %s"), SettingsSlotName);
	}
}
