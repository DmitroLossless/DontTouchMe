// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMWeaponDisplayNameSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

#include "../Gun/Gun.h"

namespace
{
	constexpr float RefreshInterval = 0.25f;

	FString CleanWeaponClassName(FString ClassName)
	{
		ClassName.RemoveFromStart(TEXT("SKEL_"));
		ClassName.RemoveFromStart(TEXT("REINST_"));
		ClassName.RemoveFromEnd(TEXT("_C"));
		return ClassName;
	}

	FString StripBlueprintPrefix(FString Identifier)
	{
		Identifier = CleanWeaponClassName(Identifier.TrimStartAndEnd());
		Identifier.RemoveFromStart(TEXT("BP_"), ESearchCase::IgnoreCase);
		Identifier.RemoveFromStart(TEXT("B_"), ESearchCase::IgnoreCase);
		Identifier.RemoveFromStart(TEXT("BP "), ESearchCase::IgnoreCase);
		return Identifier;
	}

	FString MakeWeaponIdentifierKey(const FString& Identifier)
	{
		FString Key;
		for (const TCHAR Character : StripBlueprintPrefix(Identifier))
		{
			if (FChar::IsAlnum(Character))
			{
				Key.AppendChar(FChar::ToLower(Character));
			}
		}

		return Key;
	}

	FString GetClassDisplayNameForMatching(const UClass* Class)
	{
		if (!Class)
		{
			return FString();
		}

#if WITH_EDITORONLY_DATA
		const FString DisplayName = Class->GetDisplayNameText().ToString();
		if (!DisplayName.IsEmpty())
		{
			return DisplayName;
		}
#endif

#if WITH_METADATA
		const FString MetadataDisplayName = Class->GetMetaData(TEXT("DisplayName"));
		if (!MetadataDisplayName.IsEmpty())
		{
			return MetadataDisplayName;
		}
#endif

		return CleanWeaponClassName(Class->GetName());
	}

	bool DoesWeaponIdentifierMatch(const FString& Candidate, const UClass* Class)
	{
		if (!Class)
		{
			return false;
		}

		const FString TrimmedCandidate = Candidate.TrimStartAndEnd();
		const FString ClassName = Class->GetName();
		const FString CleanClassName = CleanWeaponClassName(ClassName);
		const FString StrippedClassName = StripBlueprintPrefix(ClassName);
		const FString ClassDisplayName = GetClassDisplayNameForMatching(Class);

		return TrimmedCandidate.Equals(ClassName, ESearchCase::IgnoreCase)
			|| TrimmedCandidate.Equals(CleanClassName, ESearchCase::IgnoreCase)
			|| TrimmedCandidate.Equals(StrippedClassName, ESearchCase::IgnoreCase)
			|| TrimmedCandidate.Equals(ClassDisplayName, ESearchCase::IgnoreCase)
			|| MakeWeaponIdentifierKey(TrimmedCandidate) == MakeWeaponIdentifierKey(ClassName)
			|| MakeWeaponIdentifierKey(TrimmedCandidate) == MakeWeaponIdentifierKey(ClassDisplayName);
	}

	FString ReadStringProperty(const UObject* Object, const FName PropertyName)
	{
		if (!Object)
		{
			return FString();
		}

		if (const FStrProperty* StringProperty = FindFProperty<FStrProperty>(Object->GetClass(), PropertyName))
		{
			return StringProperty->GetPropertyValue_InContainer(Object);
		}

		if (const FNameProperty* NameProperty = FindFProperty<FNameProperty>(Object->GetClass(), PropertyName))
		{
			return NameProperty->GetPropertyValue_InContainer(Object).ToString();
		}

		if (const FTextProperty* TextProperty = FindFProperty<FTextProperty>(Object->GetClass(), PropertyName))
		{
			return TextProperty->GetPropertyValue_InContainer(Object).ToString();
		}

		return FString();
	}

}

void UTMWeaponDisplayNameSubsystem::Deinitialize()
{
	TimeUntilNextRefresh = 0.0f;
	Super::Deinitialize();
}

void UTMWeaponDisplayNameSubsystem::Tick(const float DeltaTime)
{
	TimeUntilNextRefresh -= DeltaTime;
	if (TimeUntilNextRefresh > 0.0f)
	{
		return;
	}

	TimeUntilNextRefresh = RefreshInterval;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TObjectIterator<UUserWidget> WidgetIt; WidgetIt; ++WidgetIt)
	{
		UUserWidget* Widget = *WidgetIt;
		if (!IsValid(Widget) || Widget->GetWorld() != World)
		{
			continue;
		}

		const bool bIsWeaponLayer = IsWeaponMenuWidget(Widget);
		if (!bIsWeaponLayer && !IsLoadoutWidget(Widget))
		{
			continue;
		}

		if (bIsWeaponLayer)
		{
			if (UClass* WeaponClass = ResolveWeaponClass(Widget))
			{
				ApplyWeaponDisplayName(Widget, AGun::GetWeaponDisplayNameFromClass(WeaponClass));
			}
		}

		ApplyWeaponDisplayNamesFromExistingText(Widget);
	}
}

TStatId UTMWeaponDisplayNameSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTMWeaponDisplayNameSubsystem, STATGROUP_Tickables);
}

bool UTMWeaponDisplayNameSubsystem::IsTickable() const
{
	return !HasAnyFlags(RF_ClassDefaultObject);
}

UClass* UTMWeaponDisplayNameSubsystem::ResolveWeaponClass(const UUserWidget* Widget)
{
	if (!Widget)
	{
		return nullptr;
	}

	if (const FObjectPropertyBase* ActiveWeaponProperty =
		FindFProperty<FObjectPropertyBase>(Widget->GetClass(), TEXT("ActiveWeapon")))
	{
		if (UClass* WeaponClass = Cast<UClass>(ActiveWeaponProperty->GetObjectPropertyValue_InContainer(Widget)))
		{
			if (WeaponClass->IsChildOf(AGun::StaticClass()))
			{
				return WeaponClass;
			}
		}
	}

	static const FName CandidateProperties[] = {
		TEXT("WeaponID"),
		TEXT("WeaponIdentifier"),
		TEXT("WeaponName")
	};

	for (const FName CandidateProperty : CandidateProperties)
	{
		if (UClass* WeaponClass = ResolveWeaponClassFromCandidate(ReadStringProperty(Widget, CandidateProperty)))
		{
			return WeaponClass;
		}
	}

	return nullptr;
}

UClass* UTMWeaponDisplayNameSubsystem::ResolveWeaponClassFromCandidate(const FString& Candidate)
{
	const FString TrimmedCandidate = Candidate.TrimStartAndEnd();
	if (TrimmedCandidate.IsEmpty())
	{
		return nullptr;
	}

	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* Class = *ClassIt;
		if (!Class || !Class->IsChildOf(AGun::StaticClass()))
		{
			continue;
		}

		if (DoesWeaponIdentifierMatch(TrimmedCandidate, Class))
		{
			return Class;
		}
	}

	return nullptr;
}

void UTMWeaponDisplayNameSubsystem::ApplyWeaponDisplayName(UUserWidget* Widget, const FText& DisplayName)
{
	if (!Widget)
	{
		return;
	}

	const FString DisplayNameString = DisplayName.ToString();
	if (DisplayNameString.TrimStartAndEnd().IsEmpty())
	{
		return;
	}

	for (TFieldIterator<FObjectPropertyBase> PropertyIt(Widget->GetClass(), EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
	{
		FObjectPropertyBase* ObjectProperty = *PropertyIt;
		if (!ObjectProperty || !ObjectProperty->PropertyClass || !ObjectProperty->PropertyClass->IsChildOf(UTextBlock::StaticClass()))
		{
			continue;
		}

		const FString PropertyName = ObjectProperty->GetName();
		if (!PropertyName.Contains(TEXT("NameText")) && !PropertyName.Contains(TEXT("WeaponName")))
		{
			continue;
		}

		if (UTextBlock* TextBlock = Cast<UTextBlock>(ObjectProperty->GetObjectPropertyValue_InContainer(Widget)))
		{
			TextBlock->SetText(DisplayName);
		}
	}
}

void UTMWeaponDisplayNameSubsystem::ApplyWeaponDisplayNamesFromExistingText(UUserWidget* Widget)
{
	if (!Widget || !Widget->WidgetTree)
	{
		return;
	}

	Widget->WidgetTree->ForEachWidget([](UWidget* ChildWidget)
	{
		UTextBlock* TextBlock = Cast<UTextBlock>(ChildWidget);
		if (!TextBlock)
		{
			return;
		}

		UClass* WeaponClass = ResolveWeaponClassFromCandidate(TextBlock->GetText().ToString());
		if (!WeaponClass)
		{
			return;
		}

		const FText DisplayName = AGun::GetWeaponDisplayNameFromClass(WeaponClass);
		if (!DisplayName.ToString().TrimStartAndEnd().IsEmpty())
		{
			TextBlock->SetText(DisplayName);
		}
	});
}

bool UTMWeaponDisplayNameSubsystem::IsLoadoutWidget(const UUserWidget* Widget)
{
	if (!Widget)
	{
		return false;
	}

	const FString ClassName = Widget->GetClass()->GetName();
	return ClassName.Contains(TEXT("W_Loadout"));
}

bool UTMWeaponDisplayNameSubsystem::IsWeaponMenuWidget(const UUserWidget* Widget)
{
	if (!Widget)
	{
		return false;
	}

	const FString ClassName = Widget->GetClass()->GetName();
	return ClassName.Contains(TEXT("W_Weapon_Layer"));
}
