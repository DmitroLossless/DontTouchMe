// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UTexture2D;
class UUserWidget;

namespace TMWeaponIconResolver
{
	FString NormalizeWeaponToken(FString Token);
	FName ResolveKnownWeaponRowName(const FString& Token);
	FString GetWidgetWeaponLookupToken(const UUserWidget* Widget);
	bool ShouldCollapseWeaponRow(const FString& LookupToken);
	bool ShouldCollapseWeaponRow(const UUserWidget* Widget);
	UTexture2D* ResolveIconTexture(const UUserWidget* Widget, bool bSelected);
	UTexture2D* ResolveIconTexture(const UUserWidget* Widget, const FString& FallbackLookupToken, bool bSelected);
}
