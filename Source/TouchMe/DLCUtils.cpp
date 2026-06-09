// Copyright Epic Games, Inc. All Rights Reserved.

#include "DLCUtils.h"

#include "HAL/PlatformFileManager.h"
#include "IPlatformFilePak.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/SoftObjectPath.h"

DEFINE_LOG_CATEGORY_STATIC(LogDLCUtils, Log, All);

bool UDLCUtils::MountPak(const FString& PakFilePath, int32 PakOrder)
{
	const FString FullPakPath = FPaths::ConvertRelativePathToFull(PakFilePath);
	if (PakOrder < 0)
	{
		UE_LOG(LogDLCUtils, Error, TEXT("Cannot mount pak '%s': PakOrder must be non-negative."), *FullPakPath);
		return false;
	}

	FPlatformFileManager& PlatformFileManager = FPlatformFileManager::Get();
	if (!PlatformFileManager.GetPlatformFile().FileExists(*FullPakPath))
	{
		UE_LOG(LogDLCUtils, Warning, TEXT("Cannot mount pak: file does not exist: %s"), *FullPakPath);
		return false;
	}

	FPakPlatformFile* PakPlatformFile = static_cast<FPakPlatformFile*>(
		PlatformFileManager.FindPlatformFile(FPakPlatformFile::GetTypeName()));

	if (!PakPlatformFile)
	{
		IPlatformFile* NewPakPlatformFile = PlatformFileManager.GetPlatformFile(FPakPlatformFile::GetTypeName());
		PakPlatformFile = static_cast<FPakPlatformFile*>(NewPakPlatformFile);

		if (!PakPlatformFile || !PakPlatformFile->Initialize(&PlatformFileManager.GetPlatformFile(), TEXT("")))
		{
			UE_LOG(LogDLCUtils, Error, TEXT("Cannot initialize the PakFile platform layer."));
			return false;
		}

		PlatformFileManager.SetPlatformFile(*PakPlatformFile);
	}

	const bool bMounted = PakPlatformFile->Mount(*FullPakPath, static_cast<uint32>(PakOrder));
	if (bMounted)
	{
		UE_LOG(LogDLCUtils, Log, TEXT("Mounted pak: %s"), *FullPakPath);
	}
	else
	{
		UE_LOG(LogDLCUtils, Error, TEXT("Failed to mount pak: %s"), *FullPakPath);
	}

	return bMounted;
}

UObject* UDLCUtils::LoadAssetFromPak(const FString& AssetPath, bool bLogResult)
{
	FString ObjectPath = FPackageName::ExportTextPathToObjectPath(AssetPath.TrimStartAndEnd());
	if (FPackageName::IsValidLongPackageName(ObjectPath))
	{
		ObjectPath = FString::Printf(
			TEXT("%s.%s"),
			*ObjectPath,
			*FPackageName::GetLongPackageAssetName(ObjectPath));
	}

	const FSoftObjectPath SoftObjectPath(ObjectPath);
	UObject* LoadedAsset = SoftObjectPath.ResolveObject();
	if (!LoadedAsset)
	{
		LoadedAsset = SoftObjectPath.TryLoad();
	}

	if (bLogResult)
	{
		if (LoadedAsset)
		{
			UE_LOG(LogDLCUtils, Log, TEXT("Loaded asset from pak: %s"), *ObjectPath);
		}
		else
		{
			UE_LOG(LogDLCUtils, Warning, TEXT("Failed to load asset from pak: %s"), *ObjectPath);
		}
	}

	return LoadedAsset;
}

bool UDLCUtils::CheatDLCChecker(const FString& AssetPath, bool bLogResult)
{
	const bool bDLCAssetAvailable = LoadAssetFromPak(AssetPath, false) != nullptr;
	if (bLogResult)
	{
		if (bDLCAssetAvailable)
		{
			UE_LOG(LogDLCUtils, Log, TEXT("Cheat DLC check for '%s': available"), *AssetPath);
		}
		else
		{
			UE_LOG(LogDLCUtils, Warning, TEXT("Cheat DLC check for '%s': not available"), *AssetPath);
		}
	}

	return bDLCAssetAvailable;
}
