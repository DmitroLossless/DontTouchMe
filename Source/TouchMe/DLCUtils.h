// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DLCUtils.generated.h"

UCLASS()
class TOUCHME_API UDLCUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "DLC")
	static bool MountPak(const FString& PakFilePath, int32 PakOrder = 0);

	UFUNCTION(BlueprintCallable, Category = "DLC", meta = (DisplayName = "Load Asset From Pak"))
	static UObject* LoadAssetFromPak(const FString& AssetPath, bool bLogResult = true);

	UFUNCTION(BlueprintCallable, Category = "DLC", meta = (DisplayName = "Cheat DLC Checker"))
	static bool CheatDLCChecker(const FString& AssetPath, bool bLogResult = true);
};
