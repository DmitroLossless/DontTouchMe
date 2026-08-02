// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMWeaponIconResolver.h"

#include "Blueprint/UserWidget.h"
#include "Components/ActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
	const TCHAR* WeaponTablePath = TEXT("/Game/MP_System_V3/Game/Blueprints/DataTables/DT_Weapons.DT_Weapons");

	FString CleanWeaponClassToken(FString Token)
	{
		Token.TrimStartAndEndInline();
		Token.RemoveFromStart(TEXT("Default__"));
		Token.RemoveFromStart(TEXT("SKEL_"));
		Token.RemoveFromStart(TEXT("REINST_"));
		Token.RemoveFromEnd(TEXT("_C"));
		Token.RemoveFromStart(TEXT("BP_"), ESearchCase::IgnoreCase);
		Token.RemoveFromStart(TEXT("B_"), ESearchCase::IgnoreCase);
		Token.RemoveFromStart(TEXT("BP "), ESearchCase::IgnoreCase);
		if (Token.Len() > 2
			&& Token.StartsWith(TEXT("BP"), ESearchCase::IgnoreCase)
			&& FChar::IsUpper(Token[2]))
		{
			Token.RightChopInline(2);
		}
		return Token;
	}

	void AddWeaponLookupToken(TArray<FString>& OutTokens, const FString& Token)
	{
		FString CleanToken = Token;
		CleanToken.TrimStartAndEndInline();
		if (CleanToken.IsEmpty())
		{
			return;
		}

		OutTokens.AddUnique(CleanToken);
		OutTokens.AddUnique(CleanWeaponClassToken(CleanToken));

		FString AssetLeaf;
		if (CleanToken.Split(TEXT("/"), nullptr, &AssetLeaf, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			OutTokens.AddUnique(AssetLeaf);
			OutTokens.AddUnique(CleanWeaponClassToken(AssetLeaf));

			FString AssetName;
			if (AssetLeaf.Split(TEXT("."), &AssetName, nullptr, ESearchCase::CaseSensitive, ESearchDir::FromStart))
			{
				OutTokens.AddUnique(AssetName);
				OutTokens.AddUnique(CleanWeaponClassToken(AssetName));
			}
		}
	}

	void AddTextPropertyToken(const UObject* Object, const FName PropertyName, TArray<FString>& OutTokens)
	{
		if (!Object)
		{
			return;
		}

		if (const FNameProperty* NameProperty = FindFProperty<FNameProperty>(Object->GetClass(), PropertyName))
		{
			AddWeaponLookupToken(OutTokens, NameProperty->GetPropertyValue_InContainer(Object).ToString());
			return;
		}

		if (const FStrProperty* StringProperty = FindFProperty<FStrProperty>(Object->GetClass(), PropertyName))
		{
			AddWeaponLookupToken(OutTokens, StringProperty->GetPropertyValue_InContainer(Object));
			return;
		}

		if (const FTextProperty* TextProperty = FindFProperty<FTextProperty>(Object->GetClass(), PropertyName))
		{
			AddWeaponLookupToken(OutTokens, TextProperty->GetPropertyValue_InContainer(Object).ToString());
		}
	}

	void AddObjectToken(const UObject* Object, TArray<FString>& OutTokens);

	void AddActorMeshTokens(const AActor* Actor, TArray<FString>& OutTokens)
	{
		if (!Actor)
		{
			return;
		}

		TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshComponents(Actor);
		for (const USkeletalMeshComponent* MeshComponent : SkeletalMeshComponents)
		{
			const USkeletalMesh* Mesh = MeshComponent ? MeshComponent->GetSkeletalMeshAsset() : nullptr;
			if (Mesh)
			{
				AddObjectToken(Mesh, OutTokens);
			}
		}

		TInlineComponentArray<UStaticMeshComponent*> StaticMeshComponents(Actor);
		for (const UStaticMeshComponent* MeshComponent : StaticMeshComponents)
		{
			const UStaticMesh* Mesh = MeshComponent ? MeshComponent->GetStaticMesh() : nullptr;
			if (Mesh)
			{
				AddObjectToken(Mesh, OutTokens);
			}
		}
	}

	void AddObjectToken(const UObject* Object, TArray<FString>& OutTokens)
	{
		if (!Object)
		{
			return;
		}

		AddWeaponLookupToken(OutTokens, Object->GetName());
		AddWeaponLookupToken(OutTokens, Object->GetPathName());

		if (const UClass* Class = Cast<UClass>(Object))
		{
			AddWeaponLookupToken(OutTokens, Class->GetName());
			AddWeaponLookupToken(OutTokens, Class->GetPathName());
			AddActorMeshTokens(Cast<AActor>(Class->GetDefaultObject(false)), OutTokens);
			return;
		}

		if (const AActor* Actor = Cast<AActor>(Object))
		{
			AddObjectToken(Actor->GetClass(), OutTokens);
			AddActorMeshTokens(Actor, OutTokens);
		}
	}

	void AddObjectPropertyToken(const UObject* Object, const FName PropertyName, TArray<FString>& OutTokens)
	{
		if (!Object)
		{
			return;
		}

		if (const FObjectPropertyBase* ObjectProperty =
			FindFProperty<FObjectPropertyBase>(Object->GetClass(), PropertyName))
		{
			AddObjectToken(ObjectProperty->GetObjectPropertyValue_InContainer(Object), OutTokens);
		}
	}

	void AddWidgetObjectLookupTokens(const UUserWidget* Widget, TArray<FString>& OutTokens)
	{
		if (!Widget)
		{
			return;
		}

		static const FName ObjectPropertyNames[] =
		{
			TEXT("ActiveWeapon"),
			TEXT("WeaponClass"),
			TEXT("WeaponActor"),
			TEXT("Weapon")
		};

		for (const FName PropertyName : ObjectPropertyNames)
		{
			AddObjectPropertyToken(Widget, PropertyName, OutTokens);
		}
	}

	void AddWidgetTextLookupTokens(const UUserWidget* Widget, TArray<FString>& OutTokens)
	{
		if (!Widget)
		{
			return;
		}

		static const FName TextPropertyNames[] =
		{
			TEXT("WeaponID"),
			TEXT("Weapon ID"),
			TEXT("WeaponIdentifier"),
			TEXT("Weapon Identifier"),
			TEXT("WeaponName"),
			TEXT("Weapon Name"),
			TEXT("Name"),
			TEXT("DisplayName")
		};

		for (const FName PropertyName : TextPropertyNames)
		{
			AddTextPropertyToken(Widget, PropertyName, OutTokens);
		}

		if (const UTextBlock* NameText = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("NameText"))))
		{
			AddWeaponLookupToken(OutTokens, NameText->GetText().ToString());
		}
	}

	TArray<FString> CollectWidgetWeaponLookupTokens(const UUserWidget* Widget, const FString& FallbackLookupToken)
	{
		TArray<FString> Tokens;
		AddWidgetObjectLookupTokens(Widget, Tokens);
		AddWidgetTextLookupTokens(Widget, Tokens);
		AddWeaponLookupToken(Tokens, FallbackLookupToken);
		return Tokens;
	}

	bool DataTableContainsRow(const UDataTable* Table, const FName RowName)
	{
		return Table && !RowName.IsNone() && Table->GetRowMap().Contains(RowName);
	}

	void CollectRowLookupTokensFromProperty(const FProperty* Property, const void* ValuePtr, TArray<FString>& OutTokens)
	{
		if (!Property || !ValuePtr)
		{
			return;
		}

		if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			AddWeaponLookupToken(OutTokens, NameProperty->GetPropertyValue(ValuePtr).ToString());
			return;
		}

		if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
		{
			AddWeaponLookupToken(OutTokens, StringProperty->GetPropertyValue(ValuePtr));
			return;
		}

		if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
		{
			AddWeaponLookupToken(OutTokens, TextProperty->GetPropertyValue(ValuePtr).ToString());
			return;
		}

		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
			for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
			{
				CollectRowLookupTokensFromProperty(ArrayProperty->Inner, ArrayHelper.GetRawPtr(Index), OutTokens);
			}
			return;
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (!StructProperty->Struct)
			{
				return;
			}

			for (TFieldIterator<FProperty> ChildPropertyIt(StructProperty->Struct); ChildPropertyIt; ++ChildPropertyIt)
			{
				const FProperty* ChildProperty = *ChildPropertyIt;
				const void* ChildValuePtr = ChildProperty->ContainerPtrToValuePtr<void>(ValuePtr);
				CollectRowLookupTokensFromProperty(ChildProperty, ChildValuePtr, OutTokens);
			}
		}
	}

	bool RowMatchesAnyLookupToken(const UScriptStruct* RowStruct, const uint8* RowData, const TArray<FString>& Tokens)
	{
		if (!RowStruct || !RowData)
		{
			return false;
		}

		TArray<FString> RowTokens;
		for (TFieldIterator<FProperty> PropertyIt(RowStruct); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(RowData);
			CollectRowLookupTokensFromProperty(Property, ValuePtr, RowTokens);
		}

		for (const FString& RowToken : RowTokens)
		{
			const FString NormalizedRowToken = TMWeaponIconResolver::NormalizeWeaponToken(RowToken);
			if (NormalizedRowToken.IsEmpty())
			{
				continue;
			}

			for (const FString& Token : Tokens)
			{
				if (NormalizedRowToken.Equals(TMWeaponIconResolver::NormalizeWeaponToken(Token), ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
		}

		return false;
	}

	FName ResolveWeaponRowName(const UDataTable* Table, const TArray<FString>& Tokens)
	{
		for (const FString& Token : Tokens)
		{
			const FName KnownRowName = TMWeaponIconResolver::ResolveKnownWeaponRowName(Token);
			if (DataTableContainsRow(Table, KnownRowName))
			{
				return KnownRowName;
			}
		}

		if (!Table)
		{
			return NAME_None;
		}

		for (const TPair<FName, uint8*>& RowPair : Table->GetRowMap())
		{
			const FString NormalizedRowName = TMWeaponIconResolver::NormalizeWeaponToken(RowPair.Key.ToString());
			for (const FString& Token : Tokens)
			{
				if (NormalizedRowName.Equals(TMWeaponIconResolver::NormalizeWeaponToken(Token), ESearchCase::IgnoreCase))
				{
					return RowPair.Key;
				}
			}
		}

		const UScriptStruct* RowStruct = Table->GetRowStruct();
		for (const TPair<FName, uint8*>& RowPair : Table->GetRowMap())
		{
			if (RowMatchesAnyLookupToken(RowStruct, RowPair.Value, Tokens))
			{
				return RowPair.Key;
			}
		}

		return NAME_None;
	}

	UObject* ResolveObjectRedirector(UObject* Object)
	{
		while (UObjectRedirector* Redirector = Cast<UObjectRedirector>(Object))
		{
			UObject* DestinationObject = Redirector->DestinationObject;
			if (!DestinationObject || DestinationObject == Object)
			{
				break;
			}

			Object = DestinationObject;
		}

		return Object;
	}

	bool IsWeaponMeshObject(const UObject* Object)
	{
		if (!Object || (!Object->IsA<USkeletalMesh>() && !Object->IsA<UStaticMesh>()))
		{
			return false;
		}

		const FString MeshName = Object->GetName();
		return !MeshName.EndsWith(TEXT("_NR"), ESearchCase::IgnoreCase)
			&& !MeshName.Contains(TEXT("NoRender"), ESearchCase::IgnoreCase)
			&& !MeshName.Contains(TEXT("No_Render"), ESearchCase::IgnoreCase);
	}

	bool IsCoreDataProperty(const FProperty* Property)
	{
		if (!Property)
		{
			return false;
		}

		const FString PropertyName = Property->GetName();
		const FString AuthoredName = Property->GetAuthoredName();
#if WITH_EDITOR
		const FString DisplayName = Property->GetDisplayNameText().ToString();
#else
		const FString DisplayName;
#endif

		const auto MatchesCoreDataName = [](const FString& Name)
		{
			return Name.Equals(TEXT("CoreData"), ESearchCase::IgnoreCase)
				|| Name.StartsWith(TEXT("CoreData_"), ESearchCase::IgnoreCase);
		};

		return MatchesCoreDataName(PropertyName)
			|| MatchesCoreDataName(AuthoredName)
			|| MatchesCoreDataName(DisplayName);
	}

	bool IsWeaponMeshProperty(const FProperty* Property)
	{
		if (!Property)
		{
			return false;
		}

		const FString PropertyName = Property->GetName();
		const FString AuthoredName = Property->GetAuthoredName();
#if WITH_EDITOR
		const FString DisplayName = Property->GetDisplayNameText().ToString();
#else
		const FString DisplayName;
#endif

		const auto MatchesMeshName = [](const FString& Name)
		{
			return Name.Equals(TEXT("Mesh"), ESearchCase::IgnoreCase)
				|| Name.StartsWith(TEXT("Mesh_"), ESearchCase::IgnoreCase)
				|| Name.Equals(TEXT("DT_Mesh"), ESearchCase::IgnoreCase)
				|| Name.StartsWith(TEXT("DT_Mesh_"), ESearchCase::IgnoreCase);
		};

		return MatchesMeshName(PropertyName)
			|| MatchesMeshName(AuthoredName)
			|| MatchesMeshName(DisplayName);
	}

	void AddWeaponMeshObject(UObject* Object, TArray<UObject*>& OutMeshes)
	{
		Object = ResolveObjectRedirector(Object);
		if (IsWeaponMeshObject(Object))
		{
			OutMeshes.AddUnique(Object);
		}
	}

	void CollectWeaponMeshObjectsFromValue(
		const FProperty* Property,
		const void* ValuePtr,
		TArray<UObject*>& OutMeshes)
	{
		if (!Property || !ValuePtr)
		{
			return;
		}

		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			AddWeaponMeshObject(ObjectProperty->GetObjectPropertyValue(ValuePtr), OutMeshes);
			return;
		}

		if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
		{
			const FSoftObjectPtr SoftObject = SoftObjectProperty->GetPropertyValue(ValuePtr);
			AddWeaponMeshObject(SoftObject.LoadSynchronous(), OutMeshes);
			return;
		}

		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
			for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
			{
				CollectWeaponMeshObjectsFromValue(ArrayProperty->Inner, ArrayHelper.GetRawPtr(Index), OutMeshes);
			}
			return;
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (!StructProperty->Struct)
			{
				return;
			}

			for (TFieldIterator<FProperty> ChildPropertyIt(StructProperty->Struct); ChildPropertyIt; ++ChildPropertyIt)
			{
				const FProperty* ChildProperty = *ChildPropertyIt;
				const void* ChildValuePtr = ChildProperty->ContainerPtrToValuePtr<void>(ValuePtr);
				CollectWeaponMeshObjectsFromValue(ChildProperty, ChildValuePtr, OutMeshes);
			}
		}
	}

	void CollectWeaponMeshesFromProperty(
		const FProperty* Property,
		const void* ValuePtr,
		const int32 CoreDataDepth,
		TArray<UObject*>& OutMeshes)
	{
		if (!Property || !ValuePtr)
		{
			return;
		}

		if (CoreDataDepth == 1 && IsWeaponMeshProperty(Property))
		{
			CollectWeaponMeshObjectsFromValue(Property, ValuePtr, OutMeshes);
			return;
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (!StructProperty->Struct)
			{
				return;
			}

			const int32 ChildCoreDataDepth = IsCoreDataProperty(Property) ? 1 : (CoreDataDepth > 0 ? CoreDataDepth + 1 : 0);
			for (TFieldIterator<FProperty> ChildPropertyIt(StructProperty->Struct); ChildPropertyIt; ++ChildPropertyIt)
			{
				const FProperty* ChildProperty = *ChildPropertyIt;
				const void* ChildValuePtr = ChildProperty->ContainerPtrToValuePtr<void>(ValuePtr);
				CollectWeaponMeshesFromProperty(ChildProperty, ChildValuePtr, ChildCoreDataDepth, OutMeshes);
			}
		}
	}

	int32 GetWeaponIconMeshScore(const UObject* Mesh)
	{
		if (!Mesh)
		{
			return MIN_int32;
		}

		int32 Score = Mesh->IsA<USkeletalMesh>() ? 100 : 50;
		const FString MeshName = Mesh->GetName();
		if (MeshName.Contains(TEXT("Mag"), ESearchCase::IgnoreCase)
			|| MeshName.Contains(TEXT("Sight"), ESearchCase::IgnoreCase)
			|| MeshName.Contains(TEXT("Laser"), ESearchCase::IgnoreCase)
			|| MeshName.Contains(TEXT("Grip"), ESearchCase::IgnoreCase)
			|| MeshName.Contains(TEXT("Silencer"), ESearchCase::IgnoreCase)
			|| MeshName.Contains(TEXT("Compensator"), ESearchCase::IgnoreCase))
		{
			Score -= 1000;
		}

		return Score;
	}

	void SortWeaponIconMeshes(TArray<UObject*>& Meshes)
	{
		Meshes.Sort([](const UObject& Left, const UObject& Right)
		{
			return GetWeaponIconMeshScore(&Left) > GetWeaponIconMeshScore(&Right);
		});
	}

	TArray<UObject*> CollectWeaponIconMeshesFromRow(const UDataTable* Table, const FName RowName)
	{
		TArray<UObject*> RowMeshes;
		if (!Table || !Table->GetRowStruct() || RowName.IsNone())
		{
			return RowMeshes;
		}

		uint8* RowData = Table->FindRowUnchecked(RowName);
		if (!RowData)
		{
			return RowMeshes;
		}

		for (TFieldIterator<FProperty> PropertyIt(Table->GetRowStruct()); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(RowData);
			CollectWeaponMeshesFromProperty(Property, ValuePtr, 0, RowMeshes);
		}

		SortWeaponIconMeshes(RowMeshes);
		return RowMeshes;
	}

	FString MakeWeaponIconObjectPath(const UObject* Mesh, const bool bSelected)
	{
		if (!Mesh)
		{
			return FString();
		}

		const FString IconName = FString::Printf(TEXT("T_%s_Icon%s"), *Mesh->GetName(), bSelected ? TEXT("_Active") : TEXT(""));
		return FString::Printf(TEXT("/Game/UI/Generated/Icons/%s.%s"), *IconName, *IconName);
	}

	UTexture2D* LoadIconForMesh(UObject* Mesh, const bool bSelected)
	{
		if (!Mesh)
		{
			return nullptr;
		}

		const FString MeshName = Mesh->GetName();
		const bool bUseSelectedIcon = bSelected && !MeshName.Equals(TEXT("Frag"), ESearchCase::IgnoreCase);
		if (bUseSelectedIcon)
		{
			const FString ActiveIconPath = MakeWeaponIconObjectPath(Mesh, true);
			if (UTexture2D* ActiveIconTexture = LoadObject<UTexture2D>(nullptr, *ActiveIconPath))
			{
				return ActiveIconTexture;
			}
		}

		const FString IconPath = MakeWeaponIconObjectPath(Mesh, false);
		return LoadObject<UTexture2D>(nullptr, *IconPath);
	}

	UTexture2D* LoadGeneratedIconByMeshName(const TCHAR* MeshName, const bool bSelected)
	{
		if (!MeshName || !*MeshName)
		{
			return nullptr;
		}

		const FString IconName = FString::Printf(TEXT("T_%s_Icon%s"), MeshName, bSelected ? TEXT("_Active") : TEXT(""));
		const FString IconPath = FString::Printf(TEXT("/Game/UI/Generated/Icons/%s.%s"), *IconName, *IconName);
		if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *IconPath))
		{
			return Texture;
		}

		if (bSelected)
		{
			return LoadGeneratedIconByMeshName(MeshName, false);
		}

		return nullptr;
	}

	const TCHAR* ResolveKnownGeneratedIconMeshName(const FString& Token)
	{
		const FString NormalizedToken = TMWeaponIconResolver::NormalizeWeaponToken(Token);
		if (NormalizedToken.IsEmpty())
		{
			return nullptr;
		}

		if (NormalizedToken.Equals(TEXT("Bayonet"), ESearchCase::IgnoreCase)
			|| NormalizedToken.Equals(TEXT("SKBayonet01"), ESearchCase::IgnoreCase))
		{
			return TEXT("SK_Bayonet_01");
		}

		if (NormalizedToken.Equals(TEXT("Cleaver"), ESearchCase::IgnoreCase)
			|| NormalizedToken.Equals(TEXT("SKCleaver01"), ESearchCase::IgnoreCase))
		{
			return TEXT("SK_Cleaver_01");
		}

		if (NormalizedToken.Equals(TEXT("Kunai"), ESearchCase::IgnoreCase)
			|| NormalizedToken.Equals(TEXT("SKKunai01"), ESearchCase::IgnoreCase))
		{
			return TEXT("SK_Kunai_01");
		}

		if (NormalizedToken.Equals(TEXT("Knife"), ESearchCase::IgnoreCase)
			|| NormalizedToken.Equals(TEXT("Knife1"), ESearchCase::IgnoreCase)
			|| NormalizedToken.Equals(TEXT("SKKnife1"), ESearchCase::IgnoreCase))
		{
			return TEXT("SK_Knife1");
		}

		if (NormalizedToken.Equals(TEXT("PipeWrench"), ESearchCase::IgnoreCase)
			|| NormalizedToken.Equals(TEXT("BPPipeWrench"), ESearchCase::IgnoreCase)
			|| NormalizedToken.Equals(TEXT("SMPipeWrench01"), ESearchCase::IgnoreCase))
		{
			return TEXT("SM_PipeWrench_01");
		}

		if (NormalizedToken.Equals(TEXT("VerticleTypeB"), ESearchCase::IgnoreCase)
			|| NormalizedToken.Equals(TEXT("VerticalTypeB"), ESearchCase::IgnoreCase)
			|| NormalizedToken.Equals(TEXT("SMVerticleTypeBGrip"), ESearchCase::IgnoreCase)
			|| NormalizedToken.Equals(TEXT("SMVerticalTypeBGrip"), ESearchCase::IgnoreCase))
		{
			return TEXT("SM_VerticleTypeB_Grip");
		}

		if (NormalizedToken.Equals(TEXT("VerticleTypeC"), ESearchCase::IgnoreCase)
			|| NormalizedToken.Equals(TEXT("VerticalTypeC"), ESearchCase::IgnoreCase)
			|| NormalizedToken.Equals(TEXT("SMVerticleTypeCGrip"), ESearchCase::IgnoreCase)
			|| NormalizedToken.Equals(TEXT("SMVerticalTypeCGrip"), ESearchCase::IgnoreCase))
		{
			return TEXT("SM_VerticleTypeC_Grip");
		}

		if (NormalizedToken.Equals(TEXT("FragRed"), ESearchCase::IgnoreCase)
			|| NormalizedToken.Equals(TEXT("BPFragRed"), ESearchCase::IgnoreCase)
			|| NormalizedToken.Equals(TEXT("SMGrenadeRed"), ESearchCase::IgnoreCase))
		{
			return TEXT("SM_Grenade_Red");
		}

		return nullptr;
	}

	UTexture2D* LoadKnownGeneratedIconForTokens(const TArray<FString>& Tokens, const bool bSelected)
	{
		for (const FString& Token : Tokens)
		{
			if (const TCHAR* MeshName = ResolveKnownGeneratedIconMeshName(Token))
			{
				if (UTexture2D* Texture = LoadGeneratedIconByMeshName(MeshName, bSelected))
				{
					return Texture;
				}
			}
		}

		return nullptr;
	}
}

FString TMWeaponIconResolver::NormalizeWeaponToken(FString Token)
{
	Token = CleanWeaponClassToken(Token);
	Token.ReplaceInline(TEXT(" "), TEXT(""));
	Token.ReplaceInline(TEXT("_"), TEXT(""));
	Token.ReplaceInline(TEXT("-"), TEXT(""));
	Token.ReplaceInline(TEXT("::"), TEXT(""));
	Token.ReplaceInline(TEXT("/"), TEXT(""));
	Token.ReplaceInline(TEXT("."), TEXT(""));
	return Token;
}

FName TMWeaponIconResolver::ResolveKnownWeaponRowName(const FString& Token)
{
	const FString NormalizedToken = NormalizeWeaponToken(Token);
	if (NormalizedToken.Equals(TEXT("Shotgun"), ESearchCase::IgnoreCase)) return FName(TEXT("Shotgun"));
	if (NormalizedToken.Equals(TEXT("Kriss"), ESearchCase::IgnoreCase)) return FName(TEXT("Kriss"));
	if (NormalizedToken.Equals(TEXT("Scar"), ESearchCase::IgnoreCase)) return FName(TEXT("Scar"));
	if (NormalizedToken.Equals(TEXT("TAR"), ESearchCase::IgnoreCase)) return FName(TEXT("TAR"));
	if (NormalizedToken.Equals(TEXT("ACWI"), ESearchCase::IgnoreCase)) return FName(TEXT("ACWI"));
	if (NormalizedToken.Equals(TEXT("DE"), ESearchCase::IgnoreCase)) return FName(TEXT("DE"));
	if (NormalizedToken.Equals(TEXT("M9"), ESearchCase::IgnoreCase)) return FName(TEXT("M9"));
	if (NormalizedToken.Equals(TEXT("SciFiSyringe"), ESearchCase::IgnoreCase)) return FName(TEXT("SciFi_Syringe"));
	if (NormalizedToken.Equals(TEXT("Knife"), ESearchCase::IgnoreCase)) return FName(TEXT("Knife"));
	if (NormalizedToken.Equals(TEXT("Knife1"), ESearchCase::IgnoreCase)) return FName(TEXT("Knife"));
	if (NormalizedToken.Equals(TEXT("SKKnife1"), ESearchCase::IgnoreCase)) return FName(TEXT("Knife"));
	if (NormalizedToken.Equals(TEXT("Bayonet"), ESearchCase::IgnoreCase)) return FName(TEXT("Bayonet"));
	if (NormalizedToken.Equals(TEXT("SKBayonet01"), ESearchCase::IgnoreCase)) return FName(TEXT("Bayonet"));
	if (NormalizedToken.Equals(TEXT("Cleaver"), ESearchCase::IgnoreCase)) return FName(TEXT("Cleaver"));
	if (NormalizedToken.Equals(TEXT("SKCleaver01"), ESearchCase::IgnoreCase)) return FName(TEXT("Cleaver"));
	if (NormalizedToken.Equals(TEXT("Kunai"), ESearchCase::IgnoreCase)) return FName(TEXT("Kunai"));
	if (NormalizedToken.Equals(TEXT("SKKunai01"), ESearchCase::IgnoreCase)) return FName(TEXT("Kunai"));
	if (NormalizedToken.Equals(TEXT("PipeWrench"), ESearchCase::IgnoreCase)) return FName(TEXT("PipeWrench"));
	if (NormalizedToken.Equals(TEXT("BPPipeWrench"), ESearchCase::IgnoreCase)) return FName(TEXT("PipeWrench"));
	if (NormalizedToken.Equals(TEXT("SMPipeWrench01"), ESearchCase::IgnoreCase)) return FName(TEXT("PipeWrench"));
	if (NormalizedToken.Equals(TEXT("Frag"), ESearchCase::IgnoreCase)) return FName(TEXT("Frag"));
	if (NormalizedToken.Equals(TEXT("FragRed"), ESearchCase::IgnoreCase)) return FName(TEXT("Frag_Red"));
	if (NormalizedToken.Equals(TEXT("BPFragRed"), ESearchCase::IgnoreCase)) return FName(TEXT("Frag_Red"));
	if (NormalizedToken.Equals(TEXT("SMGrenadeRed"), ESearchCase::IgnoreCase)) return FName(TEXT("Frag_Red"));
	if (NormalizedToken.Equals(TEXT("Sniper"), ESearchCase::IgnoreCase)) return FName(TEXT("Sniper"));
	return NAME_None;
}

FString TMWeaponIconResolver::GetWidgetWeaponLookupToken(const UUserWidget* Widget)
{
	const TArray<FString> Tokens = CollectWidgetWeaponLookupTokens(Widget, FString());
	if (UDataTable* WeaponTable = LoadObject<UDataTable>(nullptr, WeaponTablePath))
	{
		const FName RowName = ResolveWeaponRowName(WeaponTable, Tokens);
		if (!RowName.IsNone())
		{
			return RowName.ToString();
		}
	}

	for (const FString& Token : Tokens)
	{
		const FName KnownRowName = ResolveKnownWeaponRowName(Token);
		if (!KnownRowName.IsNone())
		{
			return KnownRowName.ToString();
		}
	}

	for (const FString& Token : Tokens)
	{
		if (!Token.TrimStartAndEnd().IsEmpty())
		{
			return Token;
		}
	}

	return FString();
}

bool TMWeaponIconResolver::ShouldCollapseWeaponRow(const FString& LookupToken)
{
	const FName KnownRowName = ResolveKnownWeaponRowName(LookupToken);
	if (!KnownRowName.IsNone())
	{
		return KnownRowName == FName(TEXT("Sniper"));
	}

	const FString NormalizedLookupToken = NormalizeWeaponToken(LookupToken);
	return NormalizedLookupToken.Equals(TEXT("Sniper"), ESearchCase::IgnoreCase);
}

bool TMWeaponIconResolver::ShouldCollapseWeaponRow(const UUserWidget* Widget)
{
	const TArray<FString> Tokens = CollectWidgetWeaponLookupTokens(Widget, FString());
	for (const FString& Token : Tokens)
	{
		if (ShouldCollapseWeaponRow(Token))
		{
			return true;
		}
	}

	return false;
}

UTexture2D* TMWeaponIconResolver::ResolveIconTexture(const UUserWidget* Widget, const bool bSelected)
{
	return ResolveIconTexture(Widget, FString(), bSelected);
}

UTexture2D* TMWeaponIconResolver::ResolveIconTexture(
	const UUserWidget* Widget,
	const FString& FallbackLookupToken,
	const bool bSelected)
{
	TArray<FString> Tokens = CollectWidgetWeaponLookupTokens(Widget, FallbackLookupToken);
	if (Tokens.IsEmpty())
	{
		return nullptr;
	}

	if (UTexture2D* KnownIconTexture = LoadKnownGeneratedIconForTokens(Tokens, bSelected))
	{
		return KnownIconTexture;
	}

	UDataTable* WeaponTable = LoadObject<UDataTable>(nullptr, WeaponTablePath);
	if (!WeaponTable || !WeaponTable->GetRowStruct())
	{
		return nullptr;
	}

	const FName RowName = ResolveWeaponRowName(WeaponTable, Tokens);
	if (RowName.IsNone())
	{
		return nullptr;
	}

	TArray<UObject*> RowMeshes = CollectWeaponIconMeshesFromRow(WeaponTable, RowName);
	for (UObject* Mesh : RowMeshes)
	{
		if (UTexture2D* IconTexture = LoadIconForMesh(Mesh, bSelected))
		{
			return IconTexture;
		}
	}

	return LoadKnownGeneratedIconForTokens(Tokens, bSelected);
}
