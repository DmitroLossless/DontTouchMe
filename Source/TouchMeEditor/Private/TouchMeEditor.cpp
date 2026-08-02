#include "TouchMeEditor.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ContentBrowserModule.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Engine.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/World.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FeedbackContext.h"
#include "Misc/FileHelper.h"
#include "Misc/ObjectThumbnail.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "ObjectTools.h"
#include "PreviewScene.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "StaticMeshResources.h"
#include "Styling/AppStyle.h"
#include "ThumbnailRendering/SceneThumbnailInfo.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

#define LOCTEXT_NAMESPACE "TouchMeEditor"

namespace
{
	constexpr int32 TMIconWidth = 512;
	constexpr int32 TMIconHeight = 128;
	constexpr int32 TMThumbnailRenderSize = 1024;
	const TCHAR* TMIconOutputPath = TEXT("/Game/UI/Generated/Icons");
	const TCHAR* TMLoadoutWeaponDataTablePath = TEXT("/Game/MP_System_V3/Game/Blueprints/DataTables/DT_Weapons.DT_Weapons");
	bool bTMGenerateLoadoutWeaponMaterialOnly = false;
	bool bTMGenerateLoadoutWeaponActiveOnly = false;

	UObject* TMResolveObjectRedirector(UObject* Object)
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

	bool TMIsSupportedIconMeshObject(const UObject* Object)
	{
		if (!Object || (!Object->IsA<UStaticMesh>() && !Object->IsA<USkeletalMesh>()))
		{
			return false;
		}

		const FString MeshName = Object->GetName();
		return !MeshName.EndsWith(TEXT("_NR"), ESearchCase::IgnoreCase)
			&& !MeshName.Contains(TEXT("NoRender"), ESearchCase::IgnoreCase)
			&& !MeshName.Contains(TEXT("No_Render"), ESearchCase::IgnoreCase);
	}

	bool TMIsLoadoutCoreDataProperty(const FProperty* Property)
	{
		if (!Property)
		{
			return false;
		}

		const auto IsCoreDataPropertyName = [](const FString& Name)
		{
			return Name.Equals(TEXT("CoreData"), ESearchCase::IgnoreCase)
				|| Name.StartsWith(TEXT("CoreData_"), ESearchCase::IgnoreCase);
		};

		return IsCoreDataPropertyName(Property->GetName())
			|| IsCoreDataPropertyName(Property->GetAuthoredName())
			|| IsCoreDataPropertyName(Property->GetDisplayNameText().ToString());
	}

	bool TMIsLoadoutMeshProperty(const FProperty* Property)
	{
		if (!Property)
		{
			return false;
		}

		const auto IsMeshPropertyName = [](const FString& Name)
		{
			return Name.Equals(TEXT("Mesh"), ESearchCase::IgnoreCase)
				|| Name.StartsWith(TEXT("Mesh_"), ESearchCase::IgnoreCase)
				|| Name.Equals(TEXT("DT_Mesh"), ESearchCase::IgnoreCase)
				|| Name.StartsWith(TEXT("DT_Mesh_"), ESearchCase::IgnoreCase);
		};

		return IsMeshPropertyName(Property->GetName())
			|| IsMeshPropertyName(Property->GetAuthoredName())
			|| IsMeshPropertyName(Property->GetDisplayNameText().ToString());
	}

	void TMAddLoadoutWeaponMeshObject(UObject* Object, TArray<UObject*>& OutMeshes)
	{
		Object = TMResolveObjectRedirector(Object);
		if (TMIsSupportedIconMeshObject(Object))
		{
			OutMeshes.AddUnique(Object);
		}
	}

	void TMCollectLoadoutWeaponMeshesFromMeshProperty(
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
			TMAddLoadoutWeaponMeshObject(ObjectProperty->GetObjectPropertyValue(ValuePtr), OutMeshes);
			return;
		}

		if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
		{
			const FSoftObjectPtr SoftObject = SoftObjectProperty->GetPropertyValue(ValuePtr);
			TMAddLoadoutWeaponMeshObject(SoftObject.LoadSynchronous(), OutMeshes);
			return;
		}

		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
			for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
			{
				TMCollectLoadoutWeaponMeshesFromMeshProperty(
					ArrayProperty->Inner,
					ArrayHelper.GetRawPtr(Index),
					OutMeshes);
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
				TMCollectLoadoutWeaponMeshesFromMeshProperty(ChildProperty, ChildValuePtr, OutMeshes);
			}
		}
	}

	void TMCollectLoadoutWeaponMeshesFromProperty(
		const FProperty* Property,
		const void* ValuePtr,
		const int32 CoreDataDepth,
		TArray<UObject*>& OutMeshes)
	{
		if (!Property || !ValuePtr)
		{
			return;
		}

		if (CoreDataDepth == 1 && TMIsLoadoutMeshProperty(Property))
		{
			TMCollectLoadoutWeaponMeshesFromMeshProperty(Property, ValuePtr, OutMeshes);
			return;
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (!StructProperty->Struct)
			{
				return;
			}

			const int32 ChildCoreDataDepth = TMIsLoadoutCoreDataProperty(Property) ? 1 : (CoreDataDepth > 0 ? CoreDataDepth + 1 : 0);
			for (TFieldIterator<FProperty> ChildPropertyIt(StructProperty->Struct); ChildPropertyIt; ++ChildPropertyIt)
			{
				const FProperty* ChildProperty = *ChildPropertyIt;
				const void* ChildValuePtr = ChildProperty->ContainerPtrToValuePtr<void>(ValuePtr);
				TMCollectLoadoutWeaponMeshesFromProperty(ChildProperty, ChildValuePtr, ChildCoreDataDepth, OutMeshes);
			}
		}
	}

	TArray<FAssetData> TMCollectLoadoutWeaponMeshAssetsFromDataTable()
	{
		TArray<FAssetData> SourceAssets;
		UDataTable* WeaponTable = LoadObject<UDataTable>(nullptr, TMLoadoutWeaponDataTablePath);
		if (!WeaponTable || !WeaponTable->GetRowStruct())
		{
			UE_LOG(LogTemp, Error, TEXT("[TMIconGenerator] Failed to load weapon data table: %s."), TMLoadoutWeaponDataTablePath);
			return SourceAssets;
		}

		TSet<FString> AddedMeshPaths;
		for (const TPair<FName, uint8*>& RowPair : WeaponTable->GetRowMap())
		{
			TArray<UObject*> RowMeshes;
			for (TFieldIterator<FProperty> PropertyIt(WeaponTable->GetRowStruct()); PropertyIt; ++PropertyIt)
			{
				const FProperty* Property = *PropertyIt;
				const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(RowPair.Value);
				TMCollectLoadoutWeaponMeshesFromProperty(Property, ValuePtr, 0, RowMeshes);
			}

			if (RowMeshes.IsEmpty())
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[TMIconGenerator] Weapon data row %s has no supported CoreData.Mesh."),
					*RowPair.Key.ToString());
				continue;
			}

			UObject* Mesh = RowMeshes[0];
			const FString MeshPath = Mesh->GetPathName();
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMIconGenerator] Weapon data row %s CoreData.Mesh resolves to %s."),
				*RowPair.Key.ToString(),
				*MeshPath);

			if (!AddedMeshPaths.Contains(MeshPath))
			{
				AddedMeshPaths.Add(MeshPath);
				SourceAssets.Add(FAssetData(Mesh));
			}
		}

		return SourceAssets;
	}

	uint32 TMHashRaggedValue(uint32 Value)
	{
		Value ^= Value >> 16;
		Value *= 0x7FEB352Du;
		Value ^= Value >> 15;
		Value *= 0x846CA68Bu;
		Value ^= Value >> 16;
		return Value;
	}

	float TMHashToUnitFloat(const uint32 Value)
	{
		return static_cast<float>(TMHashRaggedValue(Value) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
	}

	float TMValueNoise1D(const float Position, const int32 Lane, const uint32 Seed)
	{
		const int32 Cell = FMath::FloorToInt(Position);
		const float Fraction = Position - static_cast<float>(Cell);
		const float SmoothFraction = Fraction * Fraction * (3.0f - 2.0f * Fraction);
		const uint32 A = Seed ^ (static_cast<uint32>(Cell) * 374761393u) ^ (static_cast<uint32>(Lane) * 668265263u);
		const uint32 B = Seed ^ (static_cast<uint32>(Cell + 1) * 374761393u) ^ (static_cast<uint32>(Lane) * 668265263u);
		return FMath::Lerp(TMHashToUnitFloat(A), TMHashToUnitFloat(B), SmoothFraction);
	}

	float TMSmoothMask(const float EdgeDistance, const float CutDistance, const float Softness)
	{
		return FMath::Clamp((EdgeDistance - CutDistance) / FMath::Max(Softness, 0.001f), 0.0f, 1.0f);
	}

	struct FTMSubjectBounds
	{
		int32 MinX = 0;
		int32 MinY = 0;
		int32 MaxX = -1;
		int32 MaxY = -1;

		bool IsValid() const
		{
			return MaxX >= MinX && MaxY >= MinY;
		}

		int32 Width() const
		{
			return IsValid() ? (MaxX - MinX + 1) : 0;
		}

		int32 Height() const
		{
			return IsValid() ? (MaxY - MinY + 1) : 0;
		}

		void Include(const int32 X, const int32 Y)
		{
			if (!IsValid())
			{
				MinX = MaxX = X;
				MinY = MaxY = Y;
				return;
			}

			MinX = FMath::Min(MinX, X);
			MinY = FMath::Min(MinY, Y);
			MaxX = FMath::Max(MaxX, X);
			MaxY = FMath::Max(MaxY, Y);
		}

		void Expand(const int32 Amount, const int32 Width, const int32 Height)
		{
			if (!IsValid())
			{
				return;
			}

			MinX = FMath::Clamp(MinX - Amount, 0, Width - 1);
			MinY = FMath::Clamp(MinY - Amount, 0, Height - 1);
			MaxX = FMath::Clamp(MaxX + Amount, 0, Width - 1);
			MaxY = FMath::Clamp(MaxY + Amount, 0, Height - 1);
		}
	};

	float TMGetPixelMaxChannel(const TArray<uint8>& PixelData, const int32 Width, const int32 Height, const int32 X, const int32 Y)
	{
		const int32 ClampedX = FMath::Clamp(X, 0, Width - 1);
		const int32 ClampedY = FMath::Clamp(Y, 0, Height - 1);
		const int32 PixelIndex = ((ClampedY * Width) + ClampedX) * 4;
		return static_cast<float>(FMath::Max3(PixelData[PixelIndex], PixelData[PixelIndex + 1], PixelData[PixelIndex + 2])) / 255.0f;
	}

	float TMGetPixelAlpha(const TArray<uint8>& PixelData, const int32 Width, const int32 Height, const int32 X, const int32 Y)
	{
		const int32 ClampedX = FMath::Clamp(X, 0, Width - 1);
		const int32 ClampedY = FMath::Clamp(Y, 0, Height - 1);
		return static_cast<float>(PixelData[((ClampedY * Width) + ClampedX) * 4 + 3]) / 255.0f;
	}

	FTMSubjectBounds TMFindMaskBounds(const TArray<uint8>& Mask, const int32 Width, const int32 Height)
	{
		FTMSubjectBounds Bounds;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				if (Mask[(Y * Width) + X] > 0)
				{
					Bounds.Include(X, Y);
				}
			}
		}
		return Bounds;
	}

	TArray<uint8> TMDilateMask(const TArray<uint8>& SourceMask, const int32 Width, const int32 Height, const int32 Radius)
	{
		TArray<uint8> DilatedMask;
		DilatedMask.Init(0, Width * Height);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				uint8 BestAlpha = 0;
				for (int32 OffsetY = -Radius; OffsetY <= Radius; ++OffsetY)
				{
					const int32 SourceY = FMath::Clamp(Y + OffsetY, 0, Height - 1);
					for (int32 OffsetX = -Radius; OffsetX <= Radius; ++OffsetX)
					{
						const int32 SourceX = FMath::Clamp(X + OffsetX, 0, Width - 1);
						BestAlpha = FMath::Max(BestAlpha, SourceMask[(SourceY * Width) + SourceX]);
					}
				}
				DilatedMask[(Y * Width) + X] = BestAlpha;
			}
		}
		return DilatedMask;
	}

	TArray<uint8> TMBuildSubjectMaskFromLuminance(const TArray<uint8>& PixelData, const int32 Width, const int32 Height)
	{
		const int32 PixelCount = Width * Height;
		TArray<uint8> SeedMask;
		SeedMask.Init(0, PixelCount);

		TArray<int32> RowMin;
		TArray<int32> RowMax;
		TArray<int32> RowCount;
		RowMin.Init(Width, Height);
		RowMax.Init(-1, Height);
		RowCount.Init(0, Height);

		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 PixelIndex = ((Y * Width) + X) * 4;
				const float Alpha = static_cast<float>(PixelData[PixelIndex + 3]) / 255.0f;
				const float Blue = static_cast<float>(PixelData[PixelIndex]) / 255.0f;
				const float Green = static_cast<float>(PixelData[PixelIndex + 1]) / 255.0f;
				const float Red = static_cast<float>(PixelData[PixelIndex + 2]) / 255.0f;
				const float Luminance = (Red * 0.2126f) + (Green * 0.7152f) + (Blue * 0.0722f);
				const float MaxChannel = FMath::Max3(Red, Green, Blue);

				if (Alpha > 0.02f && (MaxChannel > 0.085f || Luminance > 0.055f))
				{
					SeedMask[(Y * Width) + X] = 255;
					RowMin[Y] = FMath::Min(RowMin[Y], X);
					RowMax[Y] = FMath::Max(RowMax[Y], X);
					++RowCount[Y];
				}
			}
		}

		TArray<uint8> FilledMask;
		FilledMask.Init(0, Width * Height);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			if (RowCount[Y] >= 2 && RowMax[Y] >= RowMin[Y])
			{
				const int32 Padding = 3;
				const int32 MinX = FMath::Clamp(RowMin[Y] - Padding, 0, Width - 1);
				const int32 MaxX = FMath::Clamp(RowMax[Y] + Padding, 0, Width - 1);
				for (int32 X = MinX; X <= MaxX; ++X)
				{
					FilledMask[(Y * Width) + X] = 255;
				}
			}
		}

		for (int32 Index = 0; Index < SeedMask.Num(); ++Index)
		{
			FilledMask[Index] = FMath::Max(FilledMask[Index], SeedMask[Index]);
		}

		TArray<uint8> DilatedMask = TMDilateMask(FilledMask, Width, Height, 4);
		FTMSubjectBounds Bounds = TMFindMaskBounds(DilatedMask, Width, Height);
		if (!Bounds.IsValid() || Bounds.Width() < 12 || Bounds.Height() < 12)
		{
			DilatedMask.Init(255, Width * Height);
			return DilatedMask;
		}

		Bounds.Expand(8, Width, Height);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				if (X < Bounds.MinX || X > Bounds.MaxX || Y < Bounds.MinY || Y > Bounds.MaxY)
				{
					DilatedMask[(Y * Width) + X] = 0;
				}
			}
		}

		return DilatedMask;
	}

	TArray<uint8> TMBuildSubjectMaskFromDarkThumbnail(const TArray<uint8>& PixelData, const int32 Width, const int32 Height)
	{
		const int32 PixelCount = Width * Height;
		int32 TransparentPixelCount = 0;
		int32 VisiblePixelCount = 0;
		for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
		{
			const uint8 Alpha = PixelData[(PixelIndex * 4) + 3];
			if (Alpha <= 8)
			{
				++TransparentPixelCount;
			}
			else
			{
				++VisiblePixelCount;
			}
		}

		if (VisiblePixelCount > 0 && TransparentPixelCount > PixelCount / 16)
		{
			TArray<uint8> AlphaMask;
			AlphaMask.Init(0, PixelCount);
			for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
			{
				const uint8 Alpha = PixelData[(PixelIndex * 4) + 3];
				if (Alpha > 8)
				{
					AlphaMask[PixelIndex] = Alpha;
				}
			}
			return AlphaMask;
		}

		TArray<uint8> SeedMask;
		SeedMask.Init(0, PixelCount);

		TArray<int32> RowMin;
		TArray<int32> RowMax;
		TArray<int32> RowCount;
		RowMin.Init(Width, Height);
		RowMax.Init(-1, Height);
		RowCount.Init(0, Height);

		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 PixelIndex = ((Y * Width) + X) * 4;
				const float Alpha = static_cast<float>(PixelData[PixelIndex + 3]) / 255.0f;
				const float Blue = static_cast<float>(PixelData[PixelIndex]) / 255.0f;
				const float Green = static_cast<float>(PixelData[PixelIndex + 1]) / 255.0f;
				const float Red = static_cast<float>(PixelData[PixelIndex + 2]) / 255.0f;
				const float Luminance = (Red * 0.2126f) + (Green * 0.7152f) + (Blue * 0.0722f);
				const float MaxChannel = FMath::Max3(Red, Green, Blue);

				if (Alpha > 0.02f && (MaxChannel > 0.085f || Luminance > 0.055f))
				{
					SeedMask[(Y * Width) + X] = 255;
					RowMin[Y] = FMath::Min(RowMin[Y], X);
					RowMax[Y] = FMath::Max(RowMax[Y], X);
					++RowCount[Y];
				}
			}
		}

		TArray<uint8> FilledMask;
		FilledMask.Init(0, Width * Height);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			if (RowCount[Y] >= 2 && RowMax[Y] >= RowMin[Y])
			{
				const int32 Padding = 3;
				const int32 MinX = FMath::Clamp(RowMin[Y] - Padding, 0, Width - 1);
				const int32 MaxX = FMath::Clamp(RowMax[Y] + Padding, 0, Width - 1);
				for (int32 X = MinX; X <= MaxX; ++X)
				{
					FilledMask[(Y * Width) + X] = 255;
				}
			}
		}

		for (int32 Index = 0; Index < SeedMask.Num(); ++Index)
		{
			FilledMask[Index] = FMath::Max(FilledMask[Index], SeedMask[Index]);
		}

		TArray<uint8> DilatedMask = TMDilateMask(FilledMask, Width, Height, 4);
		FTMSubjectBounds Bounds = TMFindMaskBounds(DilatedMask, Width, Height);
		if (!Bounds.IsValid() || Bounds.Width() < 12 || Bounds.Height() < 12)
		{
			DilatedMask.Init(255, Width * Height);
			return DilatedMask;
		}

		Bounds.Expand(8, Width, Height);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				if (X < Bounds.MinX || X > Bounds.MaxX || Y < Bounds.MinY || Y > Bounds.MaxY)
				{
					DilatedMask[(Y * Width) + X] = 0;
				}
			}
		}

		return DilatedMask;
	}

	void TMApplyRaggedButtonAlpha(TArray<uint8>& PixelData, const int32 Width, const int32 Height, const uint32 Seed)
	{
		if (Width <= 0 || Height <= 0 || PixelData.Num() != Width * Height * 4)
		{
			return;
		}

		constexpr float Softness = 4.0f;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const float TopCut =
					3.0f
					+ TMValueNoise1D(static_cast<float>(X) * 0.030f, 0, Seed) * 15.0f
					+ TMValueNoise1D(static_cast<float>(X) * 0.115f, 1, Seed) * 5.0f;
				const float BottomCut =
					3.0f
					+ TMValueNoise1D(static_cast<float>(X) * 0.027f, 2, Seed) * 15.0f
					+ TMValueNoise1D(static_cast<float>(X) * 0.105f, 3, Seed) * 5.0f;
				const float LeftCut =
					2.0f
					+ TMValueNoise1D(static_cast<float>(Y) * 0.090f, 4, Seed) * 11.0f
					+ TMValueNoise1D(static_cast<float>(Y) * 0.310f, 5, Seed) * 3.0f;
				const float RightCut =
					2.0f
					+ TMValueNoise1D(static_cast<float>(Y) * 0.083f, 6, Seed) * 11.0f
					+ TMValueNoise1D(static_cast<float>(Y) * 0.290f, 7, Seed) * 3.0f;

				float AlphaMask = 1.0f;
				AlphaMask = FMath::Min(AlphaMask, TMSmoothMask(static_cast<float>(Y), TopCut, Softness));
				AlphaMask = FMath::Min(AlphaMask, TMSmoothMask(static_cast<float>(Height - 1 - Y), BottomCut, Softness));
				AlphaMask = FMath::Min(AlphaMask, TMSmoothMask(static_cast<float>(X), LeftCut, Softness));
				AlphaMask = FMath::Min(AlphaMask, TMSmoothMask(static_cast<float>(Width - 1 - X), RightCut, Softness));

				const int32 AlphaIndex = ((Y * Width) + X) * 4 + 3;
				PixelData[AlphaIndex] = static_cast<uint8>(FMath::Clamp(
					FMath::RoundToInt(static_cast<float>(PixelData[AlphaIndex]) * AlphaMask),
					0,
					255));
			}
		}
	}

	float TMGetPixelLuminance(const TArray<uint8>& PixelData, const int32 Width, const int32 Height, const int32 X, const int32 Y)
	{
		const int32 ClampedX = FMath::Clamp(X, 0, Width - 1);
		const int32 ClampedY = FMath::Clamp(Y, 0, Height - 1);
		const int32 PixelIndex = ((ClampedY * Width) + ClampedX) * 4;
		const float Alpha = static_cast<float>(PixelData[PixelIndex + 3]) / 255.0f;
		const float Blue = static_cast<float>(PixelData[PixelIndex]) / 255.0f;
		const float Green = static_cast<float>(PixelData[PixelIndex + 1]) / 255.0f;
		const float Red = static_cast<float>(PixelData[PixelIndex + 2]) / 255.0f;
		return Alpha * ((Red * 0.2126f) + (Green * 0.7152f) + (Blue * 0.0722f));
	}

	void TMFitSubjectToIconCanvas(
		const TArray<uint8>& SourcePixels,
		const TArray<uint8>& SourceMask,
		const int32 SourceWidth,
		const int32 SourceHeight,
		TArray<uint8>& OutPixels)
	{
		OutPixels.Init(0, TMIconWidth * TMIconHeight * 4);
		if (SourceWidth <= 0
			|| SourceHeight <= 0
			|| SourcePixels.Num() != SourceWidth * SourceHeight * 4
			|| SourceMask.Num() != SourceWidth * SourceHeight)
		{
			return;
		}

		FTMSubjectBounds Bounds = TMFindMaskBounds(SourceMask, SourceWidth, SourceHeight);
		if (!Bounds.IsValid())
		{
			return;
		}

		Bounds.Expand(8, SourceWidth, SourceHeight);

		constexpr float TargetWidthRatio = 0.90f;
		constexpr float TargetHeightRatio = 0.90f;
		const float TargetWidth = static_cast<float>(TMIconWidth) * TargetWidthRatio;
		const float TargetHeight = static_cast<float>(TMIconHeight) * TargetHeightRatio;
		const float Scale = FMath::Min(
			TargetWidth / static_cast<float>(FMath::Max(Bounds.Width(), 1)),
			TargetHeight / static_cast<float>(FMath::Max(Bounds.Height(), 1)));

		const int32 DrawWidth = FMath::Clamp(FMath::RoundToInt(static_cast<float>(Bounds.Width()) * Scale), 1, TMIconWidth);
		const int32 DrawHeight = FMath::Clamp(FMath::RoundToInt(static_cast<float>(Bounds.Height()) * Scale), 1, TMIconHeight);
		const int32 DestMinX = (TMIconWidth - DrawWidth) / 2;
		const int32 DestMinY = (TMIconHeight - DrawHeight) / 2;

		for (int32 DestY = 0; DestY < DrawHeight; ++DestY)
		{
			for (int32 DestX = 0; DestX < DrawWidth; ++DestX)
			{
				const float SourceXFloat = static_cast<float>(Bounds.MinX)
					+ ((static_cast<float>(DestX) + 0.5f) / FMath::Max(Scale, 0.001f));
				const float SourceYFloat = static_cast<float>(Bounds.MinY)
					+ ((static_cast<float>(DestY) + 0.5f) / FMath::Max(Scale, 0.001f));
				const int32 SourceX = FMath::Clamp(FMath::FloorToInt(SourceXFloat), 0, SourceWidth - 1);
				const int32 SourceY = FMath::Clamp(FMath::FloorToInt(SourceYFloat), 0, SourceHeight - 1);
				const int32 SourceIndex = ((SourceY * SourceWidth) + SourceX) * 4;
				const int32 DestIndex = (((DestMinY + DestY) * TMIconWidth) + (DestMinX + DestX)) * 4;
				const uint8 Alpha = SourceMask[(SourceY * SourceWidth) + SourceX];
				if (Alpha <= 0)
				{
					continue;
				}

				OutPixels[DestIndex] = SourcePixels[SourceIndex];
				OutPixels[DestIndex + 1] = SourcePixels[SourceIndex + 1];
				OutPixels[DestIndex + 2] = SourcePixels[SourceIndex + 2];
				OutPixels[DestIndex + 3] = Alpha;
			}
		}
	}

	bool TMBuildButtonIconPixels(const TArray<uint8>& SourcePixels, const int32 SourceWidth, const int32 SourceHeight, TArray<uint8>& OutPixels)
	{
		if (SourceWidth <= 0 || SourceHeight <= 0 || SourcePixels.Num() != SourceWidth * SourceHeight * 4)
		{
			return false;
		}

		const TArray<uint8> SubjectMask = TMBuildSubjectMaskFromDarkThumbnail(SourcePixels, SourceWidth, SourceHeight);
		TMFitSubjectToIconCanvas(SourcePixels, SubjectMask, SourceWidth, SourceHeight, OutPixels);
		return OutPixels.Num() == TMIconWidth * TMIconHeight * 4;
	}

	bool TMBuildSubjectMaskFromAlpha(const TArray<uint8>& SourcePixels, const int32 SourceWidth, const int32 SourceHeight, TArray<uint8>& OutMask)
	{
		OutMask.Reset();
		if (SourceWidth <= 0 || SourceHeight <= 0 || SourcePixels.Num() != SourceWidth * SourceHeight * 4)
		{
			return false;
		}

		OutMask.Init(0, SourceWidth * SourceHeight);
		int32 VisiblePixelCount = 0;
		for (int32 PixelIndex = 0; PixelIndex < SourceWidth * SourceHeight; ++PixelIndex)
		{
			const uint8 Alpha = SourcePixels[(PixelIndex * 4) + 3];
			OutMask[PixelIndex] = Alpha;
			if (Alpha > 8)
			{
				++VisiblePixelCount;
			}
		}

		return VisiblePixelCount > 0 && VisiblePixelCount < (SourceWidth * SourceHeight * 95) / 100;
	}

	bool TMBuildButtonIconPixelsFromAlpha(const TArray<uint8>& SourcePixels, const int32 SourceWidth, const int32 SourceHeight, TArray<uint8>& OutPixels)
	{
		if (SourceWidth <= 0 || SourceHeight <= 0 || SourcePixels.Num() != SourceWidth * SourceHeight * 4)
		{
			return false;
		}

		TArray<uint8> SubjectMask;
		if (!TMBuildSubjectMaskFromAlpha(SourcePixels, SourceWidth, SourceHeight, SubjectMask))
		{
			return false;
		}

		TMFitSubjectToIconCanvas(SourcePixels, SubjectMask, SourceWidth, SourceHeight, OutPixels);
		return OutPixels.Num() == TMIconWidth * TMIconHeight * 4;
	}

	bool TMBuildTransformedButtonIconPixels(
		const TArray<uint8>& SourcePixels,
		const int32 SourceWidth,
		const int32 SourceHeight,
		const float RotationDegrees,
		const float TargetWidthRatio,
		const float TargetHeightRatio,
		const float ScaleMultiplier,
		TArray<uint8>& OutPixels)
	{
		OutPixels.Init(0, TMIconWidth * TMIconHeight * 4);
		if (SourceWidth <= 0 || SourceHeight <= 0 || SourcePixels.Num() != SourceWidth * SourceHeight * 4)
		{
			return false;
		}

		TArray<uint8> SubjectMask;
		if (!TMBuildSubjectMaskFromAlpha(SourcePixels, SourceWidth, SourceHeight, SubjectMask))
		{
			SubjectMask = TMBuildSubjectMaskFromLuminance(SourcePixels, SourceWidth, SourceHeight);
		}

		FTMSubjectBounds Bounds = TMFindMaskBounds(SubjectMask, SourceWidth, SourceHeight);
		if (!Bounds.IsValid())
		{
			return false;
		}

		Bounds.Expand(2, SourceWidth, SourceHeight);

		const FVector2D SourceCenter(
			(static_cast<float>(Bounds.MinX) + static_cast<float>(Bounds.MaxX)) * 0.5f,
			(static_cast<float>(Bounds.MinY) + static_cast<float>(Bounds.MaxY)) * 0.5f);
		const float RotationRadians = FMath::DegreesToRadians(RotationDegrees);
		const float CosRotation = FMath::Cos(RotationRadians);
		const float SinRotation = FMath::Sin(RotationRadians);
		bool bHasRotatedBounds = false;
		float MinRotatedX = 0.0f;
		float MinRotatedY = 0.0f;
		float MaxRotatedX = 0.0f;
		float MaxRotatedY = 0.0f;
		for (int32 SourceY = Bounds.MinY; SourceY <= Bounds.MaxY; ++SourceY)
		{
			for (int32 SourceX = Bounds.MinX; SourceX <= Bounds.MaxX; ++SourceX)
			{
				if (SubjectMask[(SourceY * SourceWidth) + SourceX] <= 16)
				{
					continue;
				}

				const float SubjectLocalX = static_cast<float>(SourceX) + 0.5f - SourceCenter.X;
				const float SubjectLocalY = static_cast<float>(SourceY) + 0.5f - SourceCenter.Y;
				const float RotatedX = (SubjectLocalX * CosRotation) - (SubjectLocalY * SinRotation);
				const float RotatedY = (SubjectLocalX * SinRotation) + (SubjectLocalY * CosRotation);
				if (!bHasRotatedBounds)
				{
					MinRotatedX = MaxRotatedX = RotatedX;
					MinRotatedY = MaxRotatedY = RotatedY;
					bHasRotatedBounds = true;
					continue;
				}

				MinRotatedX = FMath::Min(MinRotatedX, RotatedX);
				MinRotatedY = FMath::Min(MinRotatedY, RotatedY);
				MaxRotatedX = FMath::Max(MaxRotatedX, RotatedX);
				MaxRotatedY = FMath::Max(MaxRotatedY, RotatedY);
			}
		}

		if (!bHasRotatedBounds)
		{
			return false;
		}

		const float RotatedWidth = FMath::Max(MaxRotatedX - MinRotatedX + 1.0f, 1.0f);
		const float RotatedHeight = FMath::Max(MaxRotatedY - MinRotatedY + 1.0f, 1.0f);
		const float RotatedCenterX = (MinRotatedX + MaxRotatedX) * 0.5f;
		const float RotatedCenterY = (MinRotatedY + MaxRotatedY) * 0.5f;
		const float Scale = FMath::Min(
			(static_cast<float>(TMIconWidth) * TargetWidthRatio) / FMath::Max(RotatedWidth, 1.0f),
			(static_cast<float>(TMIconHeight) * TargetHeightRatio) / FMath::Max(RotatedHeight, 1.0f))
			* FMath::Max(ScaleMultiplier, 0.001f);
		if (Scale <= 0.0f)
		{
			return false;
		}

		const FVector2D DestCenter(static_cast<float>(TMIconWidth) * 0.5f, static_cast<float>(TMIconHeight) * 0.5f);

		for (int32 DestY = 0; DestY < TMIconHeight; ++DestY)
		{
			for (int32 DestX = 0; DestX < TMIconWidth; ++DestX)
			{
				const float LocalX = (static_cast<float>(DestX) + 0.5f - DestCenter.X) / Scale;
				const float LocalY = (static_cast<float>(DestY) + 0.5f - DestCenter.Y) / Scale;
				const float RotatedX = LocalX + RotatedCenterX;
				const float RotatedY = LocalY + RotatedCenterY;
				const float SourceXFloat = SourceCenter.X + (RotatedX * CosRotation) + (RotatedY * SinRotation);
				const float SourceYFloat = SourceCenter.Y - (RotatedX * SinRotation) + (RotatedY * CosRotation);
				const int32 SourceX = FMath::Clamp(FMath::FloorToInt(SourceXFloat), 0, SourceWidth - 1);
				const int32 SourceY = FMath::Clamp(FMath::FloorToInt(SourceYFloat), 0, SourceHeight - 1);
				const uint8 Alpha = SubjectMask[(SourceY * SourceWidth) + SourceX];
				if (Alpha <= 0)
				{
					continue;
				}

				const int32 SourceIndex = ((SourceY * SourceWidth) + SourceX) * 4;
				const int32 DestIndex = ((DestY * TMIconWidth) + DestX) * 4;
				OutPixels[DestIndex] = SourcePixels[SourceIndex];
				OutPixels[DestIndex + 1] = SourcePixels[SourceIndex + 1];
				OutPixels[DestIndex + 2] = SourcePixels[SourceIndex + 2];
				OutPixels[DestIndex + 3] = Alpha;
			}
		}

		TArray<uint8> OutMask;
		OutMask.Init(0, TMIconWidth * TMIconHeight);
		for (int32 PixelIndex = 0; PixelIndex < TMIconWidth * TMIconHeight; ++PixelIndex)
		{
			OutMask[PixelIndex] = OutPixels[(PixelIndex * 4) + 3];
		}

		return TMFindMaskBounds(OutMask, TMIconWidth, TMIconHeight).IsValid()
			&& OutPixels.Num() == TMIconWidth * TMIconHeight * 4;
	}

	bool TMIconHasReadableColor(const TArray<uint8>& PixelData, const int32 Width, const int32 Height)
	{
		if (Width <= 0 || Height <= 0 || PixelData.Num() != Width * Height * 4)
		{
			return false;
		}

		int32 VisiblePixelCount = 0;
		int32 LitPixelCount = 0;
		double LuminanceSum = 0.0;
		for (int32 PixelIndex = 0; PixelIndex < Width * Height; ++PixelIndex)
		{
			const int32 DataIndex = PixelIndex * 4;
			const uint8 Alpha = PixelData[DataIndex + 3];
			if (Alpha <= 8)
			{
				continue;
			}

			++VisiblePixelCount;
			const uint8 Blue = PixelData[DataIndex];
			const uint8 Green = PixelData[DataIndex + 1];
			const uint8 Red = PixelData[DataIndex + 2];
			const uint8 MaxChannel = FMath::Max3(Red, Green, Blue);
			if (MaxChannel > 32)
			{
				++LitPixelCount;
			}
			LuminanceSum += (static_cast<double>(Red) * 0.2126)
				+ (static_cast<double>(Green) * 0.7152)
				+ (static_cast<double>(Blue) * 0.0722);
		}

		if (VisiblePixelCount <= 0)
		{
			return false;
		}

		const double AverageLuminance = LuminanceSum / static_cast<double>(VisiblePixelCount);
		return AverageLuminance > 12.0 || LitPixelCount > VisiblePixelCount / 20;
	}

	bool TMRealMaterialIconHasUsableDetail(const TArray<uint8>& PixelData, const int32 Width, const int32 Height)
	{
		if (Width <= 0 || Height <= 0 || PixelData.Num() != Width * Height * 4)
		{
			return false;
		}

		int32 VisiblePixelCount = 0;
		int32 LitPixelCount = 0;
		double LuminanceSum = 0.0;
		for (int32 PixelIndex = 0; PixelIndex < Width * Height; ++PixelIndex)
		{
			const int32 DataIndex = PixelIndex * 4;
			if (PixelData[DataIndex + 3] <= 8)
			{
				continue;
			}

			++VisiblePixelCount;
			const uint8 Blue = PixelData[DataIndex];
			const uint8 Green = PixelData[DataIndex + 1];
			const uint8 Red = PixelData[DataIndex + 2];
			const uint8 MaxChannel = FMath::Max3(Red, Green, Blue);
			if (MaxChannel > 48)
			{
				++LitPixelCount;
			}
			LuminanceSum += (static_cast<double>(Red) * 0.2126)
				+ (static_cast<double>(Green) * 0.7152)
				+ (static_cast<double>(Blue) * 0.0722);
		}

		if (VisiblePixelCount <= 0)
		{
			return false;
		}

		const double AverageLuminance = LuminanceSum / static_cast<double>(VisiblePixelCount);
		return AverageLuminance > 20.0 || LitPixelCount > VisiblePixelCount / 15;
	}

	void TMApplyReadableTint(TArray<uint8>& PixelData, const int32 Width, const int32 Height)
	{
		if (Width <= 0 || Height <= 0 || PixelData.Num() != Width * Height * 4)
		{
			return;
		}

		TArray<uint8> AlphaMask;
		AlphaMask.Init(0, Width * Height);
		for (int32 PixelIndex = 0; PixelIndex < Width * Height; ++PixelIndex)
		{
			AlphaMask[PixelIndex] = PixelData[(PixelIndex * 4) + 3];
		}

		FTMSubjectBounds Bounds = TMFindMaskBounds(AlphaMask, Width, Height);
		if (!Bounds.IsValid())
		{
			return;
		}

		const float BoundsWidth = static_cast<float>(FMath::Max(Bounds.Width(), 1));
		const float BoundsHeight = static_cast<float>(FMath::Max(Bounds.Height(), 1));
		for (int32 Y = Bounds.MinY; Y <= Bounds.MaxY; ++Y)
		{
			for (int32 X = Bounds.MinX; X <= Bounds.MaxX; ++X)
			{
				const int32 PixelIndex = ((Y * Width) + X) * 4;
				const uint8 Alpha = PixelData[PixelIndex + 3];
				if (Alpha <= 8)
				{
					continue;
				}

				const float LocalX = static_cast<float>(X - Bounds.MinX) / BoundsWidth;
				const float LocalY = static_cast<float>(Y - Bounds.MinY) / BoundsHeight;
				const float TopLight = FMath::Clamp(1.0f - LocalY, 0.0f, 1.0f);
				const float SideLight = FMath::Clamp(1.0f - FMath::Abs(LocalX - 0.35f) / 0.65f, 0.0f, 1.0f);
				const float Noise = TMHashToUnitFloat(
					(static_cast<uint32>(X) * 1103515245u)
					^ (static_cast<uint32>(Y) * 12345u));
				const float Shade = FMath::Clamp(0.44f + TopLight * 0.34f + SideLight * 0.16f + Noise * 0.05f, 0.38f, 0.92f);

				PixelData[PixelIndex] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(178.0f * Shade), 0, 255));
				PixelData[PixelIndex + 1] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(188.0f * Shade), 0, 255));
				PixelData[PixelIndex + 2] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(198.0f * Shade), 0, 255));
			}
		}
	}

	void TMNormalizeSceneCaptureIconExposure(TArray<uint8>& PixelData, const int32 Width, const int32 Height)
	{
		if (Width <= 0 || Height <= 0 || PixelData.Num() != Width * Height * 4)
		{
			return;
		}

		int32 VisiblePixelCount = 0;
		double LuminanceSum = 0.0;
		for (int32 PixelIndex = 0; PixelIndex < Width * Height; ++PixelIndex)
		{
			const int32 DataIndex = PixelIndex * 4;
			if (PixelData[DataIndex + 3] <= 8)
			{
				continue;
			}

			++VisiblePixelCount;
			LuminanceSum += (static_cast<double>(PixelData[DataIndex + 2]) * 0.2126)
				+ (static_cast<double>(PixelData[DataIndex + 1]) * 0.7152)
				+ (static_cast<double>(PixelData[DataIndex]) * 0.0722);
		}

		if (VisiblePixelCount <= 0)
		{
			return;
		}

		constexpr double TargetAverageLuminance = 30.0;
		const double AverageLuminance = LuminanceSum / static_cast<double>(VisiblePixelCount);
		const float Gain = static_cast<float>(FMath::Clamp(TargetAverageLuminance / FMath::Max(AverageLuminance, 1.0), 1.0, 2.75));
		if (Gain <= 1.01f)
		{
			return;
		}

		for (int32 PixelIndex = 0; PixelIndex < Width * Height; ++PixelIndex)
		{
			const int32 DataIndex = PixelIndex * 4;
			if (PixelData[DataIndex + 3] <= 8)
			{
				continue;
			}

			PixelData[DataIndex] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(static_cast<float>(PixelData[DataIndex]) * Gain), 0, 255));
			PixelData[DataIndex + 1] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(static_cast<float>(PixelData[DataIndex + 1]) * Gain), 0, 255));
			PixelData[DataIndex + 2] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(static_cast<float>(PixelData[DataIndex + 2]) * Gain), 0, 255));
		}
	}

	float TMTriangleEdge(const FVector2D& A, const FVector2D& B, const FVector2D& Point)
	{
		return ((Point.X - A.X) * (B.Y - A.Y)) - ((Point.Y - A.Y) * (B.X - A.X));
	}

	void TMRasterizeProjectedTriangle(
		TArray<uint8>& Mask,
		const int32 Width,
		const int32 Height,
		const FVector2D& A,
		const FVector2D& B,
		const FVector2D& C)
	{
		const float Area = TMTriangleEdge(A, B, C);
		if (FMath::IsNearlyZero(Area, 0.01f))
		{
			return;
		}

		const float Sign = Area >= 0.0f ? 1.0f : -1.0f;
		const int32 MinX = FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.X, B.X, C.X)), 0, Width - 1);
		const int32 MaxX = FMath::Clamp(FMath::CeilToInt(FMath::Max3(A.X, B.X, C.X)), 0, Width - 1);
		const int32 MinY = FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.Y, B.Y, C.Y)), 0, Height - 1);
		const int32 MaxY = FMath::Clamp(FMath::CeilToInt(FMath::Max3(A.Y, B.Y, C.Y)), 0, Height - 1);

		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32 X = MinX; X <= MaxX; ++X)
			{
				const FVector2D Sample(static_cast<float>(X) + 0.5f, static_cast<float>(Y) + 0.5f);
				const float Edge0 = TMTriangleEdge(B, C, Sample) * Sign;
				const float Edge1 = TMTriangleEdge(C, A, Sample) * Sign;
				const float Edge2 = TMTriangleEdge(A, B, Sample) * Sign;
				if (Edge0 >= -0.001f && Edge1 >= -0.001f && Edge2 >= -0.001f)
				{
					Mask[(Y * Width) + X] = 255;
				}
			}
		}
	}

	void TMRasterizeLitProjectedTriangle(
		TArray<uint8>& Pixels,
		TArray<float>& DepthBuffer,
		const int32 Width,
		const int32 Height,
		const FVector2D& A,
		const FVector2D& B,
		const FVector2D& C,
		const float DepthA,
		const float DepthB,
		const float DepthC,
		const FColor& Color)
	{
		const float Area = TMTriangleEdge(A, B, C);
		if (FMath::IsNearlyZero(Area, 0.01f))
		{
			return;
		}

		const float Sign = Area >= 0.0f ? 1.0f : -1.0f;
		const float AbsArea = FMath::Abs(Area);
		const int32 MinX = FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.X, B.X, C.X)), 0, Width - 1);
		const int32 MaxX = FMath::Clamp(FMath::CeilToInt(FMath::Max3(A.X, B.X, C.X)), 0, Width - 1);
		const int32 MinY = FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.Y, B.Y, C.Y)), 0, Height - 1);
		const int32 MaxY = FMath::Clamp(FMath::CeilToInt(FMath::Max3(A.Y, B.Y, C.Y)), 0, Height - 1);

		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32 X = MinX; X <= MaxX; ++X)
			{
				const FVector2D Sample(static_cast<float>(X) + 0.5f, static_cast<float>(Y) + 0.5f);
				const float Edge0 = TMTriangleEdge(B, C, Sample) * Sign;
				const float Edge1 = TMTriangleEdge(C, A, Sample) * Sign;
				const float Edge2 = TMTriangleEdge(A, B, Sample) * Sign;
				if (Edge0 < -0.001f || Edge1 < -0.001f || Edge2 < -0.001f)
				{
					continue;
				}

				const float WeightA = Edge0 / AbsArea;
				const float WeightB = Edge1 / AbsArea;
				const float WeightC = Edge2 / AbsArea;
				const float Depth = (DepthA * WeightA) + (DepthB * WeightB) + (DepthC * WeightC);
				const int32 PixelIndex = (Y * Width) + X;
				if (Depth < DepthBuffer[PixelIndex])
				{
					continue;
				}

				DepthBuffer[PixelIndex] = Depth;
				const int32 DataIndex = PixelIndex * 4;
				Pixels[DataIndex] = Color.B;
				Pixels[DataIndex + 1] = Color.G;
				Pixels[DataIndex + 2] = Color.R;
				Pixels[DataIndex + 3] = Color.A;
			}
		}
	}

	const TCHAR* TMGetStaticProjectedDiffuseTexturePath(const UStaticMesh* StaticMesh, const TCHAR*& OutWeaponName);
	bool TMReadTextureSourceBgra8(UTexture2D* Texture, TArray<uint8>& OutPixels, int32& OutWidth, int32& OutHeight);
	void TMRasterizeTexturedProjectedTriangle(
		TArray<uint8>& Pixels,
		TArray<float>& DepthBuffer,
		int32 Width,
		int32 Height,
		const FVector2D& A,
		const FVector2D& B,
		const FVector2D& C,
		float DepthA,
		float DepthB,
		float DepthC,
		const FVector2f& UVA,
		const FVector2f& UVB,
		const FVector2f& UVC,
		const TArray<uint8>& DiffusePixels,
		int32 DiffuseWidth,
		int32 DiffuseHeight,
		float Light,
		float Facing,
		bool bRepairTransparentDiffuseSamples);

	bool TMBuildStaticMeshProjectedIconPixels(UStaticMesh* StaticMesh, TArray<uint8>& OutPixels)
	{
		OutPixels.Reset();
		if (!StaticMesh || !StaticMesh->GetRenderData() || StaticMesh->GetRenderData()->LODResources.IsEmpty())
		{
			return false;
		}

		const FStaticMeshLODResources& LODResources = StaticMesh->GetRenderData()->LODResources[0];
		const FPositionVertexBuffer& PositionBuffer = LODResources.VertexBuffers.PositionVertexBuffer;
		const FStaticMeshVertexBuffer& StaticMeshVertexBuffer = LODResources.VertexBuffers.StaticMeshVertexBuffer;
		const int32 VertexCount = static_cast<int32>(PositionBuffer.GetNumVertices());
		if (VertexCount <= 0)
		{
			return false;
		}

		const TCHAR* DiffuseWeaponName = nullptr;
		const TCHAR* DiffuseTexturePath = TMGetStaticProjectedDiffuseTexturePath(StaticMesh, DiffuseWeaponName);
		TArray<uint8> DiffusePixels;
		int32 DiffuseWidth = 0;
		int32 DiffuseHeight = 0;
		if (DiffuseTexturePath && StaticMeshVertexBuffer.GetNumTexCoords() > 0)
		{
			UTexture2D* DiffuseTexture = LoadObject<UTexture2D>(nullptr, DiffuseTexturePath);
			if (DiffuseTexture
				&& TMReadTextureSourceBgra8(DiffuseTexture, DiffusePixels, DiffuseWidth, DiffuseHeight))
			{
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMIconGenerator] Using %s diffuse material texture %s for static geometry projection."),
					DiffuseWeaponName ? DiffuseWeaponName : TEXT("static mesh"),
					*DiffuseTexture->GetPathName());
			}
		}

		const FBoxSphereBounds MeshBounds = StaticMesh->GetBounds();
		const FVector Center = MeshBounds.Origin;
		const FRotationMatrix ViewRotationMatrix(FRotator(-18.0f, -38.0f, 0.0f));
		const FVector ViewForward = ViewRotationMatrix.GetScaledAxis(EAxis::X);
		const FVector ViewRight = ViewRotationMatrix.GetScaledAxis(EAxis::Y);
		const FVector ViewUp = ViewRotationMatrix.GetScaledAxis(EAxis::Z);
		const FVector LightDirection = (ViewForward * 0.35f + ViewRight * -0.25f + ViewUp * 0.85f).GetSafeNormal();

		TArray<FVector> CenteredVertices;
		CenteredVertices.SetNum(VertexCount);
		TArray<FVector2D> ProjectedVertices;
		ProjectedVertices.SetNum(VertexCount);
		TArray<float> ProjectedDepths;
		ProjectedDepths.SetNum(VertexCount);
		FBox2D ProjectedBounds(ForceInit);
		for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
		{
			const FVector3f VertexPosition = PositionBuffer.VertexPosition(VertexIndex);
			const FVector LocalPosition(VertexPosition);
			const FVector Offset = LocalPosition - Center;
			CenteredVertices[VertexIndex] = Offset;
			const FVector2D ProjectedPosition(
				FVector::DotProduct(Offset, ViewRight),
				-FVector::DotProduct(Offset, ViewUp));
			ProjectedVertices[VertexIndex] = ProjectedPosition;
			ProjectedDepths[VertexIndex] = static_cast<float>(FVector::DotProduct(Offset, ViewForward));
			ProjectedBounds += ProjectedPosition;
		}

		if (!ProjectedBounds.bIsValid)
		{
			return false;
		}

		const FVector2D ProjectedSize = ProjectedBounds.GetSize();
		if (ProjectedSize.X <= UE_SMALL_NUMBER || ProjectedSize.Y <= UE_SMALL_NUMBER)
		{
			return false;
		}

		const float Scale = FMath::Min(
			(static_cast<float>(TMIconWidth) * 0.90f) / ProjectedSize.X,
			(static_cast<float>(TMIconHeight) * 0.90f) / ProjectedSize.Y);
		const FVector2D ProjectedCenter = ProjectedBounds.GetCenter();
		const FVector2D IconCenter(static_cast<float>(TMIconWidth) * 0.5f, static_cast<float>(TMIconHeight) * 0.5f);

		for (FVector2D& ProjectedVertex : ProjectedVertices)
		{
			ProjectedVertex = ((ProjectedVertex - ProjectedCenter) * Scale) + IconCenter;
		}

		OutPixels.Init(0, TMIconWidth * TMIconHeight * 4);
		TArray<float> DepthBuffer;
		DepthBuffer.Init(-FLT_MAX, TMIconWidth * TMIconHeight);
		const FIndexArrayView Indices = LODResources.IndexBuffer.GetArrayView();
		for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
		{
			const int32 IndexA = static_cast<int32>(Indices[Index]);
			const int32 IndexB = static_cast<int32>(Indices[Index + 1]);
			const int32 IndexC = static_cast<int32>(Indices[Index + 2]);
			if (!ProjectedVertices.IsValidIndex(IndexA)
				|| !ProjectedVertices.IsValidIndex(IndexB)
				|| !ProjectedVertices.IsValidIndex(IndexC))
			{
				continue;
			}

			const FVector TriangleNormal = FVector::CrossProduct(
				CenteredVertices[IndexB] - CenteredVertices[IndexA],
				CenteredVertices[IndexC] - CenteredVertices[IndexA]).GetSafeNormal();
			const float Light = FMath::Abs(FVector::DotProduct(TriangleNormal, LightDirection));
			const float Facing = FMath::Abs(FVector::DotProduct(TriangleNormal, ViewForward));
			if (!DiffusePixels.IsEmpty())
			{
				TMRasterizeTexturedProjectedTriangle(
					OutPixels,
					DepthBuffer,
					TMIconWidth,
					TMIconHeight,
					ProjectedVertices[IndexA],
					ProjectedVertices[IndexB],
					ProjectedVertices[IndexC],
					ProjectedDepths[IndexA],
					ProjectedDepths[IndexB],
					ProjectedDepths[IndexC],
					StaticMeshVertexBuffer.GetVertexUV(IndexA, 0),
					StaticMeshVertexBuffer.GetVertexUV(IndexB, 0),
					StaticMeshVertexBuffer.GetVertexUV(IndexC, 0),
					DiffusePixels,
					DiffuseWidth,
					DiffuseHeight,
					Light,
					Facing,
					false);
				continue;
			}

			const float Shade = FMath::Clamp(0.24f + Light * 0.32f + Facing * 0.10f, 0.22f, 0.68f);
			const FColor TriangleColor(
				static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(145.0f * Shade), 0, 255)),
				static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(149.0f * Shade), 0, 255)),
				static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(156.0f * Shade), 0, 255)),
				255);

			TMRasterizeLitProjectedTriangle(
				OutPixels,
				DepthBuffer,
				TMIconWidth,
				TMIconHeight,
				ProjectedVertices[IndexA],
				ProjectedVertices[IndexB],
				ProjectedVertices[IndexC],
				ProjectedDepths[IndexA],
				ProjectedDepths[IndexB],
				ProjectedDepths[IndexC],
				TriangleColor);
		}

		TArray<uint8> Mask;
		Mask.Init(0, TMIconWidth * TMIconHeight);
		for (int32 PixelIndex = 0; PixelIndex < TMIconWidth * TMIconHeight; ++PixelIndex)
		{
			Mask[PixelIndex] = OutPixels[(PixelIndex * 4) + 3];
		}
		if (!TMFindMaskBounds(Mask, TMIconWidth, TMIconHeight).IsValid())
		{
			return false;
		}

		return TMIconHasReadableColor(OutPixels, TMIconWidth, TMIconHeight);
	}

	bool TMReadTextureSourceBgra8(UTexture2D* Texture, TArray<uint8>& OutPixels, int32& OutWidth, int32& OutHeight);
	const TCHAR* TMGetMeleeLoadoutDiffuseTexturePath(const UStaticMesh* StaticMesh, const TCHAR*& OutWeaponName);
	bool TMDownsampleBgraPixels(
		const TArray<uint8>& SourcePixels,
		int32 SourceWidth,
		int32 SourceHeight,
		TArray<uint8>& OutPixels,
		int32 OutWidth,
		int32 OutHeight);
	void TMRasterizeTexturedProjectedTriangle(
		TArray<uint8>& Pixels,
		TArray<float>& DepthBuffer,
		int32 Width,
		int32 Height,
		const FVector2D& A,
		const FVector2D& B,
		const FVector2D& C,
		float DepthA,
		float DepthB,
		float DepthC,
		const FVector2f& UVA,
		const FVector2f& UVB,
		const FVector2f& UVC,
		const TArray<uint8>& DiffusePixels,
		int32 DiffuseWidth,
		int32 DiffuseHeight,
		float Light,
		float Facing,
		bool bRepairTransparentDiffuseSamples = false);

	bool TMBuildMeleeStaticMeshProjectedIconPixels(UStaticMesh* StaticMesh, TArray<uint8>& OutPixels)
	{
		OutPixels.Reset();
		if (!StaticMesh || !StaticMesh->GetRenderData() || StaticMesh->GetRenderData()->LODResources.IsEmpty())
		{
			return false;
		}

		const FStaticMeshLODResources& LODResources = StaticMesh->GetRenderData()->LODResources[0];
		const FPositionVertexBuffer& PositionBuffer = LODResources.VertexBuffers.PositionVertexBuffer;
		const FStaticMeshVertexBuffer& StaticMeshVertexBuffer = LODResources.VertexBuffers.StaticMeshVertexBuffer;
		const int32 VertexCount = static_cast<int32>(PositionBuffer.GetNumVertices());
		if (VertexCount <= 0 || StaticMeshVertexBuffer.GetNumTexCoords() <= 0)
		{
			return false;
		}

		const TCHAR* WeaponName = nullptr;
		const TCHAR* DiffuseTexturePath = TMGetMeleeLoadoutDiffuseTexturePath(StaticMesh, WeaponName);
		if (!DiffuseTexturePath)
		{
			return false;
		}

		TArray<uint8> DiffusePixels;
		int32 DiffuseWidth = 0;
		int32 DiffuseHeight = 0;
		UTexture2D* DiffuseTexture = LoadObject<UTexture2D>(nullptr, DiffuseTexturePath);
		if (!DiffuseTexture || !TMReadTextureSourceBgra8(DiffuseTexture, DiffusePixels, DiffuseWidth, DiffuseHeight))
		{
			return false;
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMIconGenerator] Using %s diffuse material texture %s for melee loadout projection."),
			WeaponName ? WeaponName : TEXT("melee weapon"),
			*DiffuseTexture->GetPathName());

		const FBoxSphereBounds MeshBounds = StaticMesh->GetBounds();
		const FVector Center = MeshBounds.Origin;
		const FRotationMatrix ViewRotationMatrix(FRotator(-18.0f, -38.0f, 0.0f));
		const FVector ViewForward = ViewRotationMatrix.GetScaledAxis(EAxis::X);
		const FVector ViewRight = ViewRotationMatrix.GetScaledAxis(EAxis::Y);
		const FVector ViewUp = ViewRotationMatrix.GetScaledAxis(EAxis::Z);
		const FVector LightDirection = (ViewForward * 0.35f + ViewRight * -0.25f + ViewUp * 0.85f).GetSafeNormal();

		TArray<FVector> CenteredVertices;
		CenteredVertices.SetNum(VertexCount);
		TArray<FVector2D> ProjectedVertices;
		ProjectedVertices.SetNum(VertexCount);
		TArray<float> ProjectedDepths;
		ProjectedDepths.SetNum(VertexCount);
		FBox2D ProjectedBounds(ForceInit);
		for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
		{
			const FVector3f VertexPosition = PositionBuffer.VertexPosition(VertexIndex);
			const FVector LocalPosition(VertexPosition);
			const FVector Offset = LocalPosition - Center;
			CenteredVertices[VertexIndex] = Offset;
			const FVector2D ProjectedPosition(
				FVector::DotProduct(Offset, ViewRight),
				-FVector::DotProduct(Offset, ViewUp));
			ProjectedVertices[VertexIndex] = ProjectedPosition;
			ProjectedDepths[VertexIndex] = static_cast<float>(FVector::DotProduct(Offset, ViewForward));
			ProjectedBounds += ProjectedPosition;
		}

		if (!ProjectedBounds.bIsValid)
		{
			return false;
		}

		const FVector2D ProjectedSize = ProjectedBounds.GetSize();
		if (ProjectedSize.X <= UE_SMALL_NUMBER || ProjectedSize.Y <= UE_SMALL_NUMBER)
		{
			return false;
		}

		constexpr int32 RenderScale = 8;
		constexpr int32 RenderWidth = TMIconWidth * RenderScale;
		constexpr int32 RenderHeight = TMIconHeight * RenderScale;
		const float Scale = FMath::Min(
			(static_cast<float>(RenderWidth) * 0.90f) / ProjectedSize.X,
			(static_cast<float>(RenderHeight) * 0.90f) / ProjectedSize.Y);
		const FVector2D ProjectedCenter = ProjectedBounds.GetCenter();
		const FVector2D IconCenter(static_cast<float>(RenderWidth) * 0.5f, static_cast<float>(RenderHeight) * 0.5f);

		for (FVector2D& ProjectedVertex : ProjectedVertices)
		{
			ProjectedVertex = ((ProjectedVertex - ProjectedCenter) * Scale) + IconCenter;
		}

		TArray<uint8> RenderPixels;
		RenderPixels.Init(0, RenderWidth * RenderHeight * 4);
		TArray<float> DepthBuffer;
		DepthBuffer.Init(-FLT_MAX, RenderWidth * RenderHeight);
		const FIndexArrayView Indices = LODResources.IndexBuffer.GetArrayView();
		for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
		{
			const int32 IndexA = static_cast<int32>(Indices[Index]);
			const int32 IndexB = static_cast<int32>(Indices[Index + 1]);
			const int32 IndexC = static_cast<int32>(Indices[Index + 2]);
			if (!ProjectedVertices.IsValidIndex(IndexA)
				|| !ProjectedVertices.IsValidIndex(IndexB)
				|| !ProjectedVertices.IsValidIndex(IndexC))
			{
				continue;
			}

			const FVector TriangleNormal = FVector::CrossProduct(
				CenteredVertices[IndexB] - CenteredVertices[IndexA],
				CenteredVertices[IndexC] - CenteredVertices[IndexA]).GetSafeNormal();
			const float Light = FMath::Abs(FVector::DotProduct(TriangleNormal, LightDirection));
			const float Facing = FMath::Abs(FVector::DotProduct(TriangleNormal, ViewForward));
			TMRasterizeTexturedProjectedTriangle(
				RenderPixels,
				DepthBuffer,
				RenderWidth,
				RenderHeight,
				ProjectedVertices[IndexA],
				ProjectedVertices[IndexB],
				ProjectedVertices[IndexC],
				ProjectedDepths[IndexA],
				ProjectedDepths[IndexB],
				ProjectedDepths[IndexC],
				StaticMeshVertexBuffer.GetVertexUV(IndexA, 0),
				StaticMeshVertexBuffer.GetVertexUV(IndexB, 0),
				StaticMeshVertexBuffer.GetVertexUV(IndexC, 0),
				DiffusePixels,
				DiffuseWidth,
				DiffuseHeight,
				Light,
				Facing);
		}

		TArray<uint8> Mask;
		Mask.Init(0, RenderWidth * RenderHeight);
		for (int32 PixelIndex = 0; PixelIndex < RenderWidth * RenderHeight; ++PixelIndex)
		{
			Mask[PixelIndex] = RenderPixels[(PixelIndex * 4) + 3];
		}
		if (!TMFindMaskBounds(Mask, RenderWidth, RenderHeight).IsValid())
		{
			return false;
		}

		if (!TMDownsampleBgraPixels(RenderPixels, RenderWidth, RenderHeight, OutPixels, TMIconWidth, TMIconHeight))
		{
			return false;
		}

		return TMIconHasReadableColor(OutPixels, TMIconWidth, TMIconHeight);
	}

	bool TMReadTextureSourceBgra8(UTexture2D* Texture, TArray<uint8>& OutPixels, int32& OutWidth, int32& OutHeight);
	bool TMIsWeaponDataTableMeshSourceWithVisualOverride(const FAssetData& SourceAsset);
	bool TMIsFragDataTableMeshSource(const FAssetData& SourceAsset);

	bool TMIsMeleeLoadoutMeshSource(const FAssetData& SourceAsset)
	{
		const FString ObjectPath = SourceAsset.GetObjectPathString();
		return ObjectPath.Equals(TEXT("/Game/MeleeWeapons/Meshes/SK_Kunai_01.SK_Kunai_01"), ESearchCase::IgnoreCase)
			|| ObjectPath.Equals(TEXT("/Game/MeleeWeapons/Meshes/SK_Bayonet_01.SK_Bayonet_01"), ESearchCase::IgnoreCase)
			|| ObjectPath.Equals(TEXT("/Game/MeleeWeapons/Meshes/SK_Cleaver_01.SK_Cleaver_01"), ESearchCase::IgnoreCase)
			|| ObjectPath.Equals(TEXT("/Game/MeleeWeapons/Meshes/SM_PipeWrench_01.SM_PipeWrench_01"), ESearchCase::IgnoreCase);
	}

	bool TMIsSkeletalVisualMeshPath(const USkeletalMesh* SkeletalMesh, const TCHAR* MeshPath)
	{
		return SkeletalMesh
			&& MeshPath
			&& SkeletalMesh->GetPathName().Equals(
				MeshPath,
				ESearchCase::IgnoreCase);
	}

	struct FTMProjectedDiffuseTextureData
	{
		TArray<uint8> Pixels;
		int32 Width = 0;
		int32 Height = 0;

		bool IsValid() const
		{
			return !Pixels.IsEmpty() && Width > 0 && Height > 0;
		}
	};

	const TCHAR* TMGetSkeletalVisualDiffuseTexturePath(
		const USkeletalMesh* SkeletalMesh,
		const TCHAR*& OutWeaponName,
		const int32 MaterialIndex = 0)
	{
		OutWeaponName = nullptr;
		if (TMIsSkeletalVisualMeshPath(SkeletalMesh, TEXT("/Game/Modular_AR_Pack/Mesh/T21/SKM_T21.SKM_T21")))
		{
			OutWeaponName = TEXT("TAR");
			return TEXT("/Game/Modular_AR_Pack/Texture/T21/T_T21_Diffuse.T_T21_Diffuse");
		}

		if (TMIsSkeletalVisualMeshPath(SkeletalMesh, TEXT("/Game/Modular_AR_Pack/Mesh/SCAL/SKM_SCAL_Complete.SKM_SCAL_Complete")))
		{
			if (MaterialIndex == 1)
			{
				OutWeaponName = TEXT("Scar stock");
				return TEXT("/Game/Modular_AR_Pack/Texture/Stocks/T_Stocks_Diffuse.T_Stocks_Diffuse");
			}

			OutWeaponName = TEXT("Scar");
			return TEXT("/Game/Modular_AR_Pack/Texture/SCAL/T_SCAL_Diffuse.T_SCAL_Diffuse");
		}

		if (TMIsSkeletalVisualMeshPath(SkeletalMesh, TEXT("/Game/Weapons/Mesh/Shotgun/SK_M890_Shotgun_Yaxis.SK_M890_Shotgun_Yaxis"))
			|| TMIsSkeletalVisualMeshPath(SkeletalMesh, TEXT("/Game/Weapons/Mesh/Shotgun/SK_M890_Shotgun_Xaxis.SK_M890_Shotgun_Xaxis")))
		{
			OutWeaponName = TEXT("M890");
			return TEXT("/Game/Weapons/Textures/T_M890_Shotgun_BaseColor.T_M890_Shotgun_BaseColor");
		}

		if (TMIsSkeletalVisualMeshPath(SkeletalMesh, TEXT("/Game/Weapons/Mesh/SMG/SK_V014_SMG_Yaxis.SK_V014_SMG_Yaxis"))
			|| TMIsSkeletalVisualMeshPath(SkeletalMesh, TEXT("/Game/Weapons/Mesh/SMG/SK_V014_SMG_Xaxis.SK_V014_SMG_Xaxis")))
		{
			OutWeaponName = TEXT("V014");
			return TEXT("/Game/Weapons/Textures/T_V014_SMG_BaseColor.T_V014_SMG_BaseColor");
		}

		if (TMIsSkeletalVisualMeshPath(SkeletalMesh, TEXT("/Game/Weapons/Mesh/Assault/SK_ACWI_Assault_Yaxis.SK_ACWI_Assault_Yaxis"))
			|| TMIsSkeletalVisualMeshPath(SkeletalMesh, TEXT("/Game/Weapons/Mesh/Assault/SK_ACWI_Assault_Xaxis.SK_ACWI_Assault_Xaxis")))
		{
			OutWeaponName = TEXT("ACWI");
			return TEXT("/Game/Weapons/Textures/T_ACWI_BaseColor.T_ACWI_BaseColor");
		}

		if (TMIsSkeletalVisualMeshPath(SkeletalMesh, TEXT("/Game/Weapons/Mesh/GrenadesAndMine/SK_Frag_Grenade.SK_Frag_Grenade")))
		{
			OutWeaponName = TEXT("Frag");
			return TEXT("/Game/Weapons/Textures/T_Frag_Grenade_BaseColor.T_Frag_Grenade_BaseColor");
		}

		return nullptr;
	}

	bool TMShouldRepairSkeletalVisualDiffuseProjection(const USkeletalMesh* SkeletalMesh)
	{
		return TMIsSkeletalVisualMeshPath(SkeletalMesh, TEXT("/Game/Modular_AR_Pack/Mesh/T21/SKM_T21.SKM_T21"))
			|| TMIsSkeletalVisualMeshPath(SkeletalMesh, TEXT("/Game/Modular_AR_Pack/Mesh/SCAL/SKM_SCAL_Complete.SKM_SCAL_Complete"));
	}

	const TCHAR* TMGetMeleeLoadoutDiffuseTexturePath(const UStaticMesh* StaticMesh, const TCHAR*& OutWeaponName)
	{
		OutWeaponName = nullptr;
		if (!StaticMesh)
		{
			return nullptr;
		}

		const FString MeshPath = StaticMesh->GetPathName();
		if (MeshPath.Equals(TEXT("/Game/MeleeWeapons/Meshes/SM_Kunai_01.SM_Kunai_01"), ESearchCase::IgnoreCase))
		{
			OutWeaponName = TEXT("Kunai");
			return TEXT("/Game/MeleeWeapons/Textures/T_Kunai_BC.T_Kunai_BC");
		}

		if (MeshPath.Equals(TEXT("/Game/MeleeWeapons/Meshes/SM_Bayonet_01.SM_Bayonet_01"), ESearchCase::IgnoreCase))
		{
			OutWeaponName = TEXT("Bayonet");
			return TEXT("/Game/MeleeWeapons/Textures/T_Bayonet_BC.T_Bayonet_BC");
		}

		if (MeshPath.Equals(TEXT("/Game/MeleeWeapons/Meshes/SM_Cleaver_01.SM_Cleaver_01"), ESearchCase::IgnoreCase))
		{
			OutWeaponName = TEXT("Cleaver");
			return TEXT("/Game/MeleeWeapons/Textures/T_Cleaver_BC.T_Cleaver_BC");
		}

		if (MeshPath.Equals(TEXT("/Game/MeleeWeapons/Meshes/SM_PipeWrench_01.SM_PipeWrench_01"), ESearchCase::IgnoreCase))
		{
			OutWeaponName = TEXT("PipeWrench");
			return TEXT("/Game/MeleeWeapons/Textures/T_PipeWrench_BC.T_PipeWrench_BC");
		}

		return nullptr;
	}

	const TCHAR* TMGetStaticProjectedDiffuseTexturePath(const UStaticMesh* StaticMesh, const TCHAR*& OutWeaponName)
	{
		OutWeaponName = nullptr;
		if (!StaticMesh)
		{
			return nullptr;
		}

		const FString MeshPath = StaticMesh->GetPathName();
		if (MeshPath.Equals(TEXT("/Game/UrbanMilChar/Mesh/SM/SM_Grenade_Red.SM_Grenade_Red"), ESearchCase::IgnoreCase))
		{
			OutWeaponName = TEXT("Red grenade");
			return TEXT("/Game/UrbanMilChar/Textures/Weapon/Granade/T_Grenade_Red_BaseColor.T_Grenade_Red_BaseColor");
		}

		if (MeshPath.Equals(TEXT("/Game/UrbanMilChar/Mesh/SM/SM_Grenade_Green.SM_Grenade_Green"), ESearchCase::IgnoreCase))
		{
			OutWeaponName = TEXT("Green grenade");
			return TEXT("/Game/UrbanMilChar/Textures/Weapon/Granade/T_Grenade_BC.T_Grenade_BC");
		}

		if (MeshPath.Equals(TEXT("/Game/UrbanMilChar/Mesh/SM/SM_Grenade_Blue.SM_Grenade_Blue"), ESearchCase::IgnoreCase))
		{
			OutWeaponName = TEXT("Blue grenade");
			return TEXT("/Game/UrbanMilChar/Textures/Weapon/Granade/T_Grenade_Blue_BC.T_Grenade_Blue_BC");
		}

		return nullptr;
	}

	bool TMDownsampleBgraPixels(
		const TArray<uint8>& SourcePixels,
		const int32 SourceWidth,
		const int32 SourceHeight,
		TArray<uint8>& OutPixels,
		const int32 OutWidth,
		const int32 OutHeight)
	{
		OutPixels.Reset();
		if (SourceWidth <= 0
			|| SourceHeight <= 0
			|| OutWidth <= 0
			|| OutHeight <= 0
			|| SourcePixels.Num() != SourceWidth * SourceHeight * 4)
		{
			return false;
		}

		OutPixels.Init(0, OutWidth * OutHeight * 4);
		for (int32 OutY = 0; OutY < OutHeight; ++OutY)
		{
			const int32 SourceY0 = FMath::FloorToInt(static_cast<float>(OutY) * static_cast<float>(SourceHeight) / static_cast<float>(OutHeight));
			const int32 SourceY1 = FMath::Max(
				SourceY0 + 1,
				FMath::CeilToInt(static_cast<float>(OutY + 1) * static_cast<float>(SourceHeight) / static_cast<float>(OutHeight)));
			for (int32 OutX = 0; OutX < OutWidth; ++OutX)
			{
				const int32 SourceX0 = FMath::FloorToInt(static_cast<float>(OutX) * static_cast<float>(SourceWidth) / static_cast<float>(OutWidth));
				const int32 SourceX1 = FMath::Max(
					SourceX0 + 1,
					FMath::CeilToInt(static_cast<float>(OutX + 1) * static_cast<float>(SourceWidth) / static_cast<float>(OutWidth)));

				double AlphaSum = 0.0;
				double BlueSum = 0.0;
				double GreenSum = 0.0;
				double RedSum = 0.0;
				int32 SampleCount = 0;
				for (int32 SourceY = SourceY0; SourceY < SourceY1; ++SourceY)
				{
					if (SourceY < 0 || SourceY >= SourceHeight)
					{
						continue;
					}

					for (int32 SourceX = SourceX0; SourceX < SourceX1; ++SourceX)
					{
						if (SourceX < 0 || SourceX >= SourceWidth)
						{
							continue;
						}

						const int32 SourceIndex = ((SourceY * SourceWidth) + SourceX) * 4;
						const double Alpha = static_cast<double>(SourcePixels[SourceIndex + 3]) / 255.0;
						AlphaSum += Alpha;
						BlueSum += static_cast<double>(SourcePixels[SourceIndex]) * Alpha;
						GreenSum += static_cast<double>(SourcePixels[SourceIndex + 1]) * Alpha;
						RedSum += static_cast<double>(SourcePixels[SourceIndex + 2]) * Alpha;
						++SampleCount;
					}
				}

				if (SampleCount <= 0 || AlphaSum <= UE_SMALL_NUMBER)
				{
					continue;
				}

				const double AverageAlpha = AlphaSum / static_cast<double>(SampleCount);
				const int32 OutIndex = ((OutY * OutWidth) + OutX) * 4;
				OutPixels[OutIndex] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(BlueSum / AlphaSum), 0, 255));
				OutPixels[OutIndex + 1] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(GreenSum / AlphaSum), 0, 255));
				OutPixels[OutIndex + 2] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(RedSum / AlphaSum), 0, 255));
				OutPixels[OutIndex + 3] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(AverageAlpha * 255.0), 0, 255));
			}
		}

		return OutPixels.Num() == OutWidth * OutHeight * 4;
	}

	FColor TMSampleBgraTextureWrapped(
		const TArray<uint8>& TexturePixels,
		const int32 TextureWidth,
		const int32 TextureHeight,
		const FVector2f& UV)
	{
		if (TextureWidth <= 0 || TextureHeight <= 0 || TexturePixels.Num() != TextureWidth * TextureHeight * 4)
		{
			return FColor(64, 66, 68, 255);
		}

		const float U = FMath::Clamp(UV.X, 0.0f, 1.0f);
		const float V = FMath::Clamp(1.0f - UV.Y, 0.0f, 1.0f);
		const float XFloat = U * static_cast<float>(TextureWidth - 1);
		const float YFloat = V * static_cast<float>(TextureHeight - 1);
		const int32 X0 = FMath::Clamp(FMath::FloorToInt(XFloat), 0, TextureWidth - 1);
		const int32 Y0 = FMath::Clamp(FMath::FloorToInt(YFloat), 0, TextureHeight - 1);
		const int32 X1 = FMath::Min(X0 + 1, TextureWidth - 1);
		const int32 Y1 = FMath::Min(Y0 + 1, TextureHeight - 1);
		const float BlendX = XFloat - static_cast<float>(X0);
		const float BlendY = YFloat - static_cast<float>(Y0);
		const auto SampleChannel = [&TexturePixels, TextureWidth](const int32 X, const int32 Y, const int32 Channel) -> float
		{
			return static_cast<float>(TexturePixels[((Y * TextureWidth) + X) * 4 + Channel]);
		};

		uint8 Channels[4] = { 0, 0, 0, 255 };
		for (int32 Channel = 0; Channel < 4; ++Channel)
		{
			const float Top = FMath::Lerp(
				SampleChannel(X0, Y0, Channel),
				SampleChannel(X1, Y0, Channel),
				BlendX);
			const float Bottom = FMath::Lerp(
				SampleChannel(X0, Y1, Channel),
				SampleChannel(X1, Y1, Channel),
				BlendX);
			Channels[Channel] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(FMath::Lerp(Top, Bottom, BlendY)), 0, 255));
		}

		return FColor(Channels[2], Channels[1], Channels[0], Channels[3]);
	}

	FColor TMShadeMaterialColor(const FColor& BaseColor, const float Light, const float Facing)
	{
		const float Shade = FMath::Clamp(0.48f + Light * 0.46f + Facing * 0.18f, 0.42f, 1.0f);
		const FColor Color(
			static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(static_cast<float>(BaseColor.R) * Shade + 8.0f), 0, 255)),
			static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(static_cast<float>(BaseColor.G) * Shade + 8.0f), 0, 255)),
			static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(static_cast<float>(BaseColor.B) * Shade + 8.0f), 0, 255)),
			255);
		return Color;
	}

	FColor TMGetBgraTexturePixel(
		const TArray<uint8>& TexturePixels,
		const int32 TextureWidth,
		const int32 TextureHeight,
		const int32 X,
		const int32 Y)
	{
		if (TextureWidth <= 0 || TextureHeight <= 0 || TexturePixels.Num() != TextureWidth * TextureHeight * 4)
		{
			return FColor(64, 66, 68, 255);
		}

		const int32 ClampedX = FMath::Clamp(X, 0, TextureWidth - 1);
		const int32 ClampedY = FMath::Clamp(Y, 0, TextureHeight - 1);
		const int32 DataIndex = ((ClampedY * TextureWidth) + ClampedX) * 4;
		return FColor(
			TexturePixels[DataIndex + 2],
			TexturePixels[DataIndex + 1],
			TexturePixels[DataIndex],
			TexturePixels[DataIndex + 3]);
	}

	FColor TMFindNearestOpaqueDiffuseColor(
		const TArray<uint8>& TexturePixels,
		const int32 TextureWidth,
		const int32 TextureHeight,
		const FVector2f& UV)
	{
		const float U = FMath::Clamp(UV.X, 0.0f, 1.0f);
		const float V = FMath::Clamp(1.0f - UV.Y, 0.0f, 1.0f);
		const int32 CenterX = FMath::Clamp(FMath::RoundToInt(U * static_cast<float>(TextureWidth - 1)), 0, TextureWidth - 1);
		const int32 CenterY = FMath::Clamp(FMath::RoundToInt(V * static_cast<float>(TextureHeight - 1)), 0, TextureHeight - 1);

		for (int32 Radius = 1; Radius <= 10; ++Radius)
		{
			for (int32 OffsetY = -Radius; OffsetY <= Radius; ++OffsetY)
			{
				for (int32 OffsetX = -Radius; OffsetX <= Radius; ++OffsetX)
				{
					if (FMath::Abs(OffsetX) != Radius && FMath::Abs(OffsetY) != Radius)
					{
						continue;
					}

					const FColor Candidate = TMGetBgraTexturePixel(
						TexturePixels,
						TextureWidth,
						TextureHeight,
						CenterX + OffsetX,
						CenterY + OffsetY);
					if (Candidate.A > 16)
					{
						return Candidate;
					}
				}
			}
		}

		return FColor(70, 72, 76, 255);
	}

	FColor TMRepairProjectedDiffuseSample(
		const TArray<uint8>& TexturePixels,
		const int32 TextureWidth,
		const int32 TextureHeight,
		const FVector2f& UV,
		const bool bRepairTransparentSamples)
	{
		FColor SampledColor = TMSampleBgraTextureWrapped(TexturePixels, TextureWidth, TextureHeight, UV);
		if (bRepairTransparentSamples && SampledColor.A <= 16)
		{
			SampledColor = TMFindNearestOpaqueDiffuseColor(TexturePixels, TextureWidth, TextureHeight, UV);
		}

		SampledColor.A = 255;
		return SampledColor;
	}

	void TMRasterizeTexturedProjectedTriangle(
		TArray<uint8>& Pixels,
		TArray<float>& DepthBuffer,
		const int32 Width,
		const int32 Height,
		const FVector2D& A,
		const FVector2D& B,
		const FVector2D& C,
		const float DepthA,
		const float DepthB,
		const float DepthC,
		const FVector2f& UVA,
		const FVector2f& UVB,
		const FVector2f& UVC,
		const TArray<uint8>& DiffusePixels,
		const int32 DiffuseWidth,
		const int32 DiffuseHeight,
		const float Light,
		const float Facing,
		const bool bRepairTransparentDiffuseSamples)
	{
		const float Area = TMTriangleEdge(A, B, C);
		if (FMath::IsNearlyZero(Area, 0.01f))
		{
			return;
		}

		const float Sign = Area >= 0.0f ? 1.0f : -1.0f;
		const float AbsArea = FMath::Abs(Area);
		const int32 MinX = FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.X, B.X, C.X)), 0, Width - 1);
		const int32 MaxX = FMath::Clamp(FMath::CeilToInt(FMath::Max3(A.X, B.X, C.X)), 0, Width - 1);
		const int32 MinY = FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.Y, B.Y, C.Y)), 0, Height - 1);
		const int32 MaxY = FMath::Clamp(FMath::CeilToInt(FMath::Max3(A.Y, B.Y, C.Y)), 0, Height - 1);

		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32 X = MinX; X <= MaxX; ++X)
			{
				const FVector2D Sample(static_cast<float>(X) + 0.5f, static_cast<float>(Y) + 0.5f);
				const float Edge0 = TMTriangleEdge(B, C, Sample) * Sign;
				const float Edge1 = TMTriangleEdge(C, A, Sample) * Sign;
				const float Edge2 = TMTriangleEdge(A, B, Sample) * Sign;
				if (Edge0 < -0.001f || Edge1 < -0.001f || Edge2 < -0.001f)
				{
					continue;
				}

				const float WeightA = Edge0 / AbsArea;
				const float WeightB = Edge1 / AbsArea;
				const float WeightC = Edge2 / AbsArea;
				const float Depth = (DepthA * WeightA) + (DepthB * WeightB) + (DepthC * WeightC);
				const int32 PixelIndex = (Y * Width) + X;
				if (Depth < DepthBuffer[PixelIndex])
				{
					continue;
				}

				DepthBuffer[PixelIndex] = Depth;
				const FVector2f UV(
					(UVA.X * WeightA) + (UVB.X * WeightB) + (UVC.X * WeightC),
					(UVA.Y * WeightA) + (UVB.Y * WeightB) + (UVC.Y * WeightC));
				const FColor Color = TMShadeMaterialColor(
					TMRepairProjectedDiffuseSample(
						DiffusePixels,
						DiffuseWidth,
						DiffuseHeight,
						UV,
						bRepairTransparentDiffuseSamples),
					Light,
					Facing);

				const int32 DataIndex = PixelIndex * 4;
				Pixels[DataIndex] = Color.B;
				Pixels[DataIndex + 1] = Color.G;
				Pixels[DataIndex + 2] = Color.R;
				Pixels[DataIndex + 3] = Color.A;
			}
		}
	}

	bool TMBuildSkeletalMeshProjectedIconPixels(USkeletalMesh* SkeletalMesh, TArray<uint8>& OutPixels)
	{
		OutPixels.Reset();
		if (!SkeletalMesh)
		{
			return false;
		}

		const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
		if (!RenderData || RenderData->LODRenderData.IsEmpty())
		{
			return false;
		}

		const FSkeletalMeshLODRenderData& LODRenderData = RenderData->LODRenderData[0];
		const FPositionVertexBuffer& PositionBuffer = LODRenderData.StaticVertexBuffers.PositionVertexBuffer;
		const int32 VertexCount = static_cast<int32>(PositionBuffer.GetNumVertices());
		if (VertexCount <= 0)
		{
			return false;
		}

		TArray<uint32> Indices;
		LODRenderData.MultiSizeIndexContainer.GetIndexBuffer(Indices);
		if (Indices.Num() < 3)
		{
			return false;
		}

		const TCHAR* DiffuseWeaponName = nullptr;
		const TCHAR* DiffuseTexturePath = TMGetSkeletalVisualDiffuseTexturePath(SkeletalMesh, DiffuseWeaponName);
		TArray<uint8> DiffusePixels;
		int32 DiffuseWidth = 0;
		int32 DiffuseHeight = 0;
		const bool bRepairTransparentDiffuseSamples = TMShouldRepairSkeletalVisualDiffuseProjection(SkeletalMesh);
		if (DiffuseTexturePath)
		{
			UTexture2D* DiffuseTexture = LoadObject<UTexture2D>(
				nullptr,
				DiffuseTexturePath);
			if (DiffuseTexture
				&& TMReadTextureSourceBgra8(DiffuseTexture, DiffusePixels, DiffuseWidth, DiffuseHeight))
			{
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMIconGenerator] Using %s diffuse material texture %s for skeletal geometry projection."),
					DiffuseWeaponName ? DiffuseWeaponName : TEXT("weapon"),
					*DiffuseTexture->GetPathName());
			}
		}
		TMap<int32, FTMProjectedDiffuseTextureData> DiffuseTextureByMaterialIndex;
		const auto LoadDiffuseTextureForMaterialIndex =
			[&DiffuseTextureByMaterialIndex, SkeletalMesh](const int32 MaterialIndex) -> const FTMProjectedDiffuseTextureData*
			{
				if (const FTMProjectedDiffuseTextureData* ExistingTextureData = DiffuseTextureByMaterialIndex.Find(MaterialIndex))
				{
					return ExistingTextureData->IsValid() ? ExistingTextureData : nullptr;
				}

				const TCHAR* MaterialWeaponName = nullptr;
				const TCHAR* MaterialDiffuseTexturePath = TMGetSkeletalVisualDiffuseTexturePath(
					SkeletalMesh,
					MaterialWeaponName,
					MaterialIndex);
				FTMProjectedDiffuseTextureData TextureData;
				if (MaterialDiffuseTexturePath)
				{
					UTexture2D* MaterialDiffuseTexture = LoadObject<UTexture2D>(nullptr, MaterialDiffuseTexturePath);
					if (MaterialDiffuseTexture
						&& TMReadTextureSourceBgra8(
							MaterialDiffuseTexture,
							TextureData.Pixels,
							TextureData.Width,
							TextureData.Height))
					{
						UE_LOG(
							LogTemp,
							Display,
							TEXT("[TMIconGenerator] Using %s material slot %d diffuse texture %s for skeletal geometry projection."),
							MaterialWeaponName ? MaterialWeaponName : TEXT("weapon"),
							MaterialIndex,
							*MaterialDiffuseTexture->GetPathName());
					}
				}

				DiffuseTextureByMaterialIndex.Add(MaterialIndex, MoveTemp(TextureData));
				const FTMProjectedDiffuseTextureData* AddedTextureData = DiffuseTextureByMaterialIndex.Find(MaterialIndex);
				return AddedTextureData && AddedTextureData->IsValid() ? AddedTextureData : nullptr;
			};
		const auto FindSectionMaterialIndex =
			[&LODRenderData](const int32 IndexBufferPosition) -> int32
			{
				for (const FSkelMeshRenderSection& RenderSection : LODRenderData.RenderSections)
				{
					const int32 SectionBaseIndex = static_cast<int32>(RenderSection.BaseIndex);
					const int32 SectionIndexCount = static_cast<int32>(RenderSection.NumTriangles) * 3;
					if (IndexBufferPosition >= SectionBaseIndex && IndexBufferPosition < SectionBaseIndex + SectionIndexCount)
					{
						return RenderSection.MaterialIndex;
					}
				}

				return 0;
			};

		const FBoxSphereBounds MeshBounds = SkeletalMesh->GetBounds();
		const FVector Center = MeshBounds.Origin;
		const FRotationMatrix ViewRotationMatrix(FRotator(-18.0f, -38.0f, 0.0f));
		const FVector ViewForward = ViewRotationMatrix.GetScaledAxis(EAxis::X);
		const FVector ViewRight = ViewRotationMatrix.GetScaledAxis(EAxis::Y);
		const FVector ViewUp = ViewRotationMatrix.GetScaledAxis(EAxis::Z);
		const FVector LightDirection = (ViewForward * 0.35f + ViewRight * -0.25f + ViewUp * 0.85f).GetSafeNormal();

		TArray<FVector> CenteredVertices;
		CenteredVertices.SetNum(VertexCount);
		TArray<FVector2D> ProjectedVertices;
		ProjectedVertices.SetNum(VertexCount);
		TArray<float> ProjectedDepths;
		ProjectedDepths.SetNum(VertexCount);
		FBox2D ProjectedBounds(ForceInit);
		for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
		{
			const FVector3f VertexPosition = PositionBuffer.VertexPosition(VertexIndex);
			const FVector LocalPosition(VertexPosition);
			const FVector Offset = LocalPosition - Center;
			CenteredVertices[VertexIndex] = Offset;
			const FVector2D ProjectedPosition(
				FVector::DotProduct(Offset, ViewRight),
				-FVector::DotProduct(Offset, ViewUp));
			ProjectedVertices[VertexIndex] = ProjectedPosition;
			ProjectedDepths[VertexIndex] = static_cast<float>(FVector::DotProduct(Offset, ViewForward));
			ProjectedBounds += ProjectedPosition;
		}

		if (!ProjectedBounds.bIsValid)
		{
			return false;
		}

		const FVector2D ProjectedSize = ProjectedBounds.GetSize();
		if (ProjectedSize.X <= UE_SMALL_NUMBER || ProjectedSize.Y <= UE_SMALL_NUMBER)
		{
			return false;
		}

		const float Scale = FMath::Min(
			(static_cast<float>(TMIconWidth) * 0.90f) / ProjectedSize.X,
			(static_cast<float>(TMIconHeight) * 0.90f) / ProjectedSize.Y);
		const FVector2D ProjectedCenter = ProjectedBounds.GetCenter();
		const FVector2D IconCenter(static_cast<float>(TMIconWidth) * 0.5f, static_cast<float>(TMIconHeight) * 0.5f);

		for (FVector2D& ProjectedVertex : ProjectedVertices)
		{
			ProjectedVertex = ((ProjectedVertex - ProjectedCenter) * Scale) + IconCenter;
		}

		OutPixels.Init(0, TMIconWidth * TMIconHeight * 4);
		TArray<float> DepthBuffer;
		DepthBuffer.Init(-FLT_MAX, TMIconWidth * TMIconHeight);
		for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
		{
			const int32 IndexA = static_cast<int32>(Indices[Index]);
			const int32 IndexB = static_cast<int32>(Indices[Index + 1]);
			const int32 IndexC = static_cast<int32>(Indices[Index + 2]);
			if (!ProjectedVertices.IsValidIndex(IndexA)
				|| !ProjectedVertices.IsValidIndex(IndexB)
				|| !ProjectedVertices.IsValidIndex(IndexC))
			{
				continue;
			}

			const FVector TriangleNormal = FVector::CrossProduct(
				CenteredVertices[IndexB] - CenteredVertices[IndexA],
				CenteredVertices[IndexC] - CenteredVertices[IndexA]).GetSafeNormal();
			const float Light = FMath::Abs(FVector::DotProduct(TriangleNormal, LightDirection));
			const float Facing = FMath::Abs(FVector::DotProduct(TriangleNormal, ViewForward));
			if (!DiffusePixels.IsEmpty())
			{
				const int32 MaterialIndex = FindSectionMaterialIndex(Index);
				const FTMProjectedDiffuseTextureData* MaterialDiffuseTextureData = MaterialIndex == 0
					? nullptr
					: LoadDiffuseTextureForMaterialIndex(MaterialIndex);
				const TArray<uint8>& TriangleDiffusePixels = MaterialDiffuseTextureData
					? MaterialDiffuseTextureData->Pixels
					: DiffusePixels;
				const int32 TriangleDiffuseWidth = MaterialDiffuseTextureData
					? MaterialDiffuseTextureData->Width
					: DiffuseWidth;
				const int32 TriangleDiffuseHeight = MaterialDiffuseTextureData
					? MaterialDiffuseTextureData->Height
					: DiffuseHeight;
				TMRasterizeTexturedProjectedTriangle(
					OutPixels,
					DepthBuffer,
					TMIconWidth,
					TMIconHeight,
					ProjectedVertices[IndexA],
					ProjectedVertices[IndexB],
					ProjectedVertices[IndexC],
					ProjectedDepths[IndexA],
					ProjectedDepths[IndexB],
					ProjectedDepths[IndexC],
					LODRenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(IndexA, 0),
					LODRenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(IndexB, 0),
					LODRenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(IndexC, 0),
					TriangleDiffusePixels,
					TriangleDiffuseWidth,
					TriangleDiffuseHeight,
					Light,
					Facing,
					bRepairTransparentDiffuseSamples);
				continue;
			}

			const float Shade = FMath::Clamp(0.24f + Light * 0.32f + Facing * 0.10f, 0.22f, 0.68f);
			const FColor TriangleColor(
				static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(145.0f * Shade), 0, 255)),
				static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(149.0f * Shade), 0, 255)),
				static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(156.0f * Shade), 0, 255)),
				255);

			TMRasterizeLitProjectedTriangle(
				OutPixels,
				DepthBuffer,
				TMIconWidth,
				TMIconHeight,
				ProjectedVertices[IndexA],
				ProjectedVertices[IndexB],
				ProjectedVertices[IndexC],
				ProjectedDepths[IndexA],
				ProjectedDepths[IndexB],
				ProjectedDepths[IndexC],
				TriangleColor);
		}

		TArray<uint8> Mask;
		Mask.Init(0, TMIconWidth * TMIconHeight);
		for (int32 PixelIndex = 0; PixelIndex < TMIconWidth * TMIconHeight; ++PixelIndex)
		{
			Mask[PixelIndex] = OutPixels[(PixelIndex * 4) + 3];
		}
		if (!TMFindMaskBounds(Mask, TMIconWidth, TMIconHeight).IsValid())
		{
			return false;
		}

		return TMIconHasReadableColor(OutPixels, TMIconWidth, TMIconHeight);
	}

	void TMCleanLoadoutWeaponIconAlpha(TArray<uint8>& PixelData, const int32 Width, const int32 Height)
	{
		if (Width <= 0 || Height <= 0 || PixelData.Num() != Width * Height * 4)
		{
			return;
		}

		const int32 PixelCount = Width * Height;
		TArray<uint8> Seen;
		Seen.Init(0, PixelCount);
		TArray<uint8> KeepMask;
		KeepMask.Init(0, PixelCount);
		TArray<int32> Stack;
		TArray<int32> ComponentPixels;
		const int32 MinComponentPixels = FMath::Max(24, PixelCount / 4096);

		for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
		{
			if (Seen[PixelIndex] != 0 || PixelData[(PixelIndex * 4) + 3] <= 16)
			{
				continue;
			}

			Stack.Reset();
			ComponentPixels.Reset();
			Stack.Add(PixelIndex);
			Seen[PixelIndex] = 1;
			while (!Stack.IsEmpty())
			{
				const int32 CurrentIndex = Stack.Pop(EAllowShrinking::No);
				ComponentPixels.Add(CurrentIndex);

				const int32 X = CurrentIndex % Width;
				const int32 Y = CurrentIndex / Width;
				for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
				{
					for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
					{
						if (OffsetX == 0 && OffsetY == 0)
						{
							continue;
						}

						const int32 NeighborX = X + OffsetX;
						const int32 NeighborY = Y + OffsetY;
						if (NeighborX < 0 || NeighborX >= Width || NeighborY < 0 || NeighborY >= Height)
						{
							continue;
						}

						const int32 NeighborIndex = (NeighborY * Width) + NeighborX;
						if (Seen[NeighborIndex] == 0 && PixelData[(NeighborIndex * 4) + 3] > 16)
						{
							Seen[NeighborIndex] = 1;
							Stack.Add(NeighborIndex);
						}
					}
				}
			}

			if (ComponentPixels.Num() >= MinComponentPixels)
			{
				for (const int32 ComponentPixelIndex : ComponentPixels)
				{
					KeepMask[ComponentPixelIndex] = 1;
				}
			}
		}

		for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
		{
			const int32 DataIndex = PixelIndex * 4;
			if (KeepMask[PixelIndex] == 0 || PixelData[DataIndex + 3] <= 16)
			{
				PixelData[DataIndex] = 0;
				PixelData[DataIndex + 1] = 0;
				PixelData[DataIndex + 2] = 0;
				PixelData[DataIndex + 3] = 0;
			}
		}
	}

	void TMApplyLoadoutWeaponMaterialIconTone(
		TArray<uint8>& PixelData,
		const int32 Width,
		const int32 Height,
		const bool bActiveVariant)
	{
		if (Width <= 0 || Height <= 0 || PixelData.Num() != Width * Height * 4)
		{
			return;
		}

		const float Scale = bActiveVariant ? 1.22f : 0.56f;
		const float Lift = bActiveVariant ? 10.0f : 0.0f;
		for (int32 PixelIndex = 0; PixelIndex < Width * Height; ++PixelIndex)
		{
			const int32 DataIndex = PixelIndex * 4;
			if (PixelData[DataIndex + 3] <= 16)
			{
				continue;
			}

			PixelData[DataIndex] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(static_cast<float>(PixelData[DataIndex]) * Scale + Lift), 0, 255));
			PixelData[DataIndex + 1] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(static_cast<float>(PixelData[DataIndex + 1]) * Scale + Lift), 0, 255));
			PixelData[DataIndex + 2] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(static_cast<float>(PixelData[DataIndex + 2]) * Scale + Lift), 0, 255));
		}
	}

	void TMSmoothMeleeLoadoutIconMaterial(TArray<uint8>& PixelData, const int32 Width, const int32 Height)
	{
		if (Width <= 0 || Height <= 0 || PixelData.Num() != Width * Height * 4)
		{
			return;
		}

		TArray<uint8> SourcePixels = PixelData;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 DataIndex = ((Y * Width) + X) * 4;
				if (SourcePixels[DataIndex + 3] <= 24)
				{
					continue;
				}

				int32 BlueSum = 0;
				int32 GreenSum = 0;
				int32 RedSum = 0;
				int32 WeightSum = 0;
				for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
				{
					for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
					{
						const int32 NeighborX = X + OffsetX;
						const int32 NeighborY = Y + OffsetY;
						if (NeighborX < 0 || NeighborX >= Width || NeighborY < 0 || NeighborY >= Height)
						{
							continue;
						}

						const int32 NeighborIndex = ((NeighborY * Width) + NeighborX) * 4;
						if (SourcePixels[NeighborIndex + 3] <= 24)
						{
							continue;
						}

						const int32 Weight = (OffsetX == 0 && OffsetY == 0) ? 4 : 1;
						BlueSum += static_cast<int32>(SourcePixels[NeighborIndex]) * Weight;
						GreenSum += static_cast<int32>(SourcePixels[NeighborIndex + 1]) * Weight;
						RedSum += static_cast<int32>(SourcePixels[NeighborIndex + 2]) * Weight;
						WeightSum += Weight;
					}
				}

				if (WeightSum <= 0)
				{
					continue;
				}

				const float OriginalWeight = 0.58f;
				const float SmoothWeight = 1.0f - OriginalWeight;
				PixelData[DataIndex] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(
					static_cast<float>(SourcePixels[DataIndex]) * OriginalWeight
					+ static_cast<float>(BlueSum) / static_cast<float>(WeightSum) * SmoothWeight), 0, 255));
				PixelData[DataIndex + 1] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(
					static_cast<float>(SourcePixels[DataIndex + 1]) * OriginalWeight
					+ static_cast<float>(GreenSum) / static_cast<float>(WeightSum) * SmoothWeight), 0, 255));
				PixelData[DataIndex + 2] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(
					static_cast<float>(SourcePixels[DataIndex + 2]) * OriginalWeight
					+ static_cast<float>(RedSum) / static_cast<float>(WeightSum) * SmoothWeight), 0, 255));
			}
		}
	}

	void TMLeftAlignIconForeground(TArray<uint8>& PixelData, const int32 Width, const int32 Height)
	{
		if (Width <= 0 || Height <= 0 || PixelData.Num() != Width * Height * 4)
		{
			return;
		}

		int32 MinX = Width;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 DataIndex = ((Y * Width) + X) * 4;
				if (PixelData[DataIndex + 3] > 0)
				{
					MinX = FMath::Min(MinX, X);
				}
			}
		}

		if (MinX <= 0 || MinX >= Width)
		{
			return;
		}

		TArray<uint8> ShiftedPixels;
		ShiftedPixels.Init(0, PixelData.Num());
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = MinX; X < Width; ++X)
			{
				const int32 SourceIndex = ((Y * Width) + X) * 4;
				if (PixelData[SourceIndex + 3] == 0)
				{
					continue;
				}

				const int32 TargetX = X - MinX;
				const int32 TargetIndex = ((Y * Width) + TargetX) * 4;
				ShiftedPixels[TargetIndex] = PixelData[SourceIndex];
				ShiftedPixels[TargetIndex + 1] = PixelData[SourceIndex + 1];
				ShiftedPixels[TargetIndex + 2] = PixelData[SourceIndex + 2];
				ShiftedPixels[TargetIndex + 3] = PixelData[SourceIndex + 3];
			}
		}

		PixelData = MoveTemp(ShiftedPixels);
	}

	void TMShiftIconForegroundRight(TArray<uint8>& PixelData, const int32 Width, const int32 Height, const int32 OffsetX)
	{
		if (OffsetX <= 0 || Width <= 0 || Height <= 0 || PixelData.Num() != Width * Height * 4)
		{
			return;
		}

		TArray<uint8> ShiftedPixels;
		ShiftedPixels.Init(0, PixelData.Num());
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 SourceIndex = ((Y * Width) + X) * 4;
				if (PixelData[SourceIndex + 3] == 0)
				{
					continue;
				}

				const int32 TargetX = X + OffsetX;
				if (TargetX >= Width)
				{
					continue;
				}

				const int32 TargetIndex = ((Y * Width) + TargetX) * 4;
				ShiftedPixels[TargetIndex] = PixelData[SourceIndex];
				ShiftedPixels[TargetIndex + 1] = PixelData[SourceIndex + 1];
				ShiftedPixels[TargetIndex + 2] = PixelData[SourceIndex + 2];
				ShiftedPixels[TargetIndex + 3] = PixelData[SourceIndex + 3];
			}
		}

		PixelData = MoveTemp(ShiftedPixels);
	}

	void TMOrientMeleeLoadoutIconHorizontal(TArray<uint8>& PixelData)
	{
		if (PixelData.Num() != TMIconWidth * TMIconHeight * 4)
		{
			return;
		}

		TArray<uint8> AlphaMask;
		AlphaMask.Init(0, TMIconWidth * TMIconHeight);
		for (int32 PixelIndex = 0; PixelIndex < TMIconWidth * TMIconHeight; ++PixelIndex)
		{
			const uint8 Alpha = PixelData[(PixelIndex * 4) + 3];
			AlphaMask[PixelIndex] = Alpha > 16 ? Alpha : 0;
		}

		const FTMSubjectBounds Bounds = TMFindMaskBounds(AlphaMask, TMIconWidth, TMIconHeight);
		if (Bounds.IsValid() && Bounds.Height() > FMath::RoundToInt(static_cast<float>(Bounds.Width()) * 1.15f))
		{
			TArray<uint8> RotatedPixels;
			if (TMBuildTransformedButtonIconPixels(
				PixelData,
				TMIconWidth,
				TMIconHeight,
				90.0f,
				0.90f,
				0.88f,
				1.0f,
				RotatedPixels))
			{
				PixelData = MoveTemp(RotatedPixels);
			}
		}
	}

	void TMApplyLoadoutWeaponRowIconAlignment(const FAssetData& AssetData, TArray<uint8>& PixelData)
	{
		TMLeftAlignIconForeground(PixelData, TMIconWidth, TMIconHeight);

		const FString ObjectPath = AssetData.GetObjectPathString();
		if (ObjectPath.Contains(TEXT("SMG_Scar"), ESearchCase::IgnoreCase))
		{
			TMShiftIconForegroundRight(PixelData, TMIconWidth, TMIconHeight, 8);
		}
		if (ObjectPath.Contains(TEXT("SMG_TAR"), ESearchCase::IgnoreCase))
		{
			TMShiftIconForegroundRight(PixelData, TMIconWidth, TMIconHeight, 72);
		}
		if (ObjectPath.Contains(TEXT("SMG_ACWI"), ESearchCase::IgnoreCase))
		{
			TMShiftIconForegroundRight(PixelData, TMIconWidth, TMIconHeight, 3);
		}
	}

	void TMRemoveScarLoadoutIconCaptureArtifacts(const FAssetData& AssetData, TArray<uint8>& PixelData)
	{
		if (!AssetData.GetObjectPathString().Contains(TEXT("SMG_Scar"), ESearchCase::IgnoreCase)
			|| PixelData.Num() != TMIconWidth * TMIconHeight * 4)
		{
			return;
		}

		// Scar now uses material projection before scene capture, so coordinate cleanup is intentionally disabled.
	}

	bool TMFitIconForegroundToCanvas(
		TArray<uint8>& PixelData,
		const int32 Width,
		const int32 Height,
		const float TargetWidthRatio,
		const float TargetHeightRatio)
	{
		if (Width <= 0 || Height <= 0 || PixelData.Num() != Width * Height * 4)
		{
			return false;
		}

		TArray<uint8> AlphaMask;
		AlphaMask.Init(0, Width * Height);
		for (int32 PixelIndex = 0; PixelIndex < Width * Height; ++PixelIndex)
		{
			const uint8 Alpha = PixelData[(PixelIndex * 4) + 3];
			AlphaMask[PixelIndex] = Alpha > 16 ? Alpha : 0;
		}

		const FTMSubjectBounds Bounds = TMFindMaskBounds(AlphaMask, Width, Height);
		if (!Bounds.IsValid())
		{
			return false;
		}

		const float TargetWidth = static_cast<float>(Width) * FMath::Clamp(TargetWidthRatio, 0.01f, 1.0f);
		const float TargetHeight = static_cast<float>(Height) * FMath::Clamp(TargetHeightRatio, 0.01f, 1.0f);
		const int32 DrawWidth = FMath::Clamp(
			FMath::RoundToInt(TargetWidth),
			1,
			Width);
		const int32 DrawHeight = FMath::Clamp(
			FMath::RoundToInt(TargetHeight),
			1,
			Height);
		const float ScaleX = static_cast<float>(DrawWidth) / static_cast<float>(FMath::Max(Bounds.Width(), 1));
		const float ScaleY = static_cast<float>(DrawHeight) / static_cast<float>(FMath::Max(Bounds.Height(), 1));
		const int32 DestMinX = (Width - DrawWidth) / 2;
		const int32 DestMinY = (Height - DrawHeight) / 2;

		TArray<uint8> FittedPixels;
		FittedPixels.Init(0, PixelData.Num());
		for (int32 DestY = 0; DestY < DrawHeight; ++DestY)
		{
			for (int32 DestX = 0; DestX < DrawWidth; ++DestX)
			{
				const float SourceXFloat = static_cast<float>(Bounds.MinX)
					+ ((static_cast<float>(DestX) + 0.5f) / FMath::Max(ScaleX, 0.001f));
				const float SourceYFloat = static_cast<float>(Bounds.MinY)
					+ ((static_cast<float>(DestY) + 0.5f) / FMath::Max(ScaleY, 0.001f));
				const float ClampedSourceX = FMath::Clamp(SourceXFloat, 0.0f, static_cast<float>(Width - 1));
				const float ClampedSourceY = FMath::Clamp(SourceYFloat, 0.0f, static_cast<float>(Height - 1));
				const int32 SourceX0 = FMath::Clamp(FMath::FloorToInt(ClampedSourceX), 0, Width - 1);
				const int32 SourceY0 = FMath::Clamp(FMath::FloorToInt(ClampedSourceY), 0, Height - 1);
				const int32 SourceX1 = FMath::Min(SourceX0 + 1, Width - 1);
				const int32 SourceY1 = FMath::Min(SourceY0 + 1, Height - 1);
				const float BlendX = ClampedSourceX - static_cast<float>(SourceX0);
				const float BlendY = ClampedSourceY - static_cast<float>(SourceY0);
				const auto SampleChannel = [&PixelData, Width](const int32 X, const int32 Y, const int32 Channel) -> float
				{
					return static_cast<float>(PixelData[((Y * Width) + X) * 4 + Channel]);
				};

				uint8 SampledChannels[4] = { 0, 0, 0, 0 };
				for (int32 Channel = 0; Channel < 4; ++Channel)
				{
					const float Top = FMath::Lerp(
						SampleChannel(SourceX0, SourceY0, Channel),
						SampleChannel(SourceX1, SourceY0, Channel),
						BlendX);
					const float Bottom = FMath::Lerp(
						SampleChannel(SourceX0, SourceY1, Channel),
						SampleChannel(SourceX1, SourceY1, Channel),
						BlendX);
					SampledChannels[Channel] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(FMath::Lerp(Top, Bottom, BlendY)), 0, 255));
				}
				if (SampledChannels[3] <= 0)
				{
					continue;
				}

				const int32 DestIndex = (((DestMinY + DestY) * Width) + (DestMinX + DestX)) * 4;
				FittedPixels[DestIndex] = SampledChannels[0];
				FittedPixels[DestIndex + 1] = SampledChannels[1];
				FittedPixels[DestIndex + 2] = SampledChannels[2];
				FittedPixels[DestIndex + 3] = SampledChannels[3];
			}
		}

		PixelData = MoveTemp(FittedPixels);
		return true;
	}

	bool TMFindLoadoutWeaponPostIconFit(
		const FAssetData& AssetData,
		float& OutTargetWidthRatio,
		float& OutTargetHeightRatio)
	{
		const FString ObjectPath = AssetData.GetObjectPathString();
		if (ObjectPath.Contains(TEXT("SMG_TAR"), ESearchCase::IgnoreCase))
		{
			OutTargetWidthRatio = 0.68f;
			OutTargetHeightRatio = 0.98f;
			return true;
		}

		if (ObjectPath.Contains(TEXT("SMG_Scar"), ESearchCase::IgnoreCase))
		{
			OutTargetWidthRatio = 0.98f;
			OutTargetHeightRatio = 0.98f;
			return true;
		}

		if (TMIsFragDataTableMeshSource(AssetData))
		{
			OutTargetWidthRatio = 0.22f;
			OutTargetHeightRatio = 0.98f;
			return true;
		}

		return false;
	}

	void TMShowNotification(const FText& Message, const SNotificationItem::ECompletionState State)
	{
		if (IsRunningCommandlet())
		{
			UE_LOG(LogTemp, Display, TEXT("[TMIconGenerator] %s"), *Message.ToString());
			return;
		}

		FNotificationInfo Info(Message);
		Info.bUseSuccessFailIcons = true;
		Info.ExpireDuration = State == SNotificationItem::CS_Fail ? 8.0f : 4.0f;

		TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification.IsValid())
		{
			Notification->SetCompletionState(State);
		}
	}

	UThumbnailInfo* TMGetMeshThumbnailInfo(UObject* SourceObject)
	{
		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(SourceObject))
		{
			return StaticMesh->GetThumbnailInfo();
		}

		if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(SourceObject))
		{
			return SkeletalMesh->GetThumbnailInfo();
		}

		return nullptr;
	}

	void TMSetMeshThumbnailInfo(UObject* SourceObject, UThumbnailInfo* ThumbnailInfo)
	{
		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(SourceObject))
		{
			StaticMesh->SetThumbnailInfo(ThumbnailInfo);
			return;
		}

		if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(SourceObject))
		{
			SkeletalMesh->SetThumbnailInfo(ThumbnailInfo);
		}
	}

	struct FTMScopedThumbnailAngleOverride
	{
		explicit FTMScopedThumbnailAngleOverride(UObject* InSourceObject)
			: SourceObject(InSourceObject)
			, OriginalThumbnailInfo(TMGetMeshThumbnailInfo(InSourceObject))
		{
			if (!SourceObject)
			{
				return;
			}

			USceneThumbnailInfo* TemporaryThumbnailInfo = NewObject<USceneThumbnailInfo>(SourceObject, NAME_None, RF_Transient);
			if (!TemporaryThumbnailInfo)
			{
				return;
			}

			TemporaryThumbnailInfo->OrbitPitch = -18.0f;
			TemporaryThumbnailInfo->OrbitYaw = -38.0f;
			TemporaryThumbnailInfo->OrbitZoom = -12.0f;
			TMSetMeshThumbnailInfo(SourceObject, TemporaryThumbnailInfo);
			ActiveThumbnailInfo = TemporaryThumbnailInfo;
		}

		~FTMScopedThumbnailAngleOverride()
		{
			if (SourceObject)
			{
				TMSetMeshThumbnailInfo(SourceObject, OriginalThumbnailInfo);
			}
		}

		UObject* SourceObject = nullptr;
		UThumbnailInfo* OriginalThumbnailInfo = nullptr;
		TWeakObjectPtr<USceneThumbnailInfo> ActiveThumbnailInfo;
	};

	UMaterialInterface* TMGetReadableIconMaterial()
	{
		static TWeakObjectPtr<UMaterialInstanceDynamic> CachedReadableMaterial;
		if (CachedReadableMaterial.IsValid())
		{
			return CachedReadableMaterial.Get();
		}

		UMaterialInterface* BaseMaterial = GEngine ? GEngine->EmissiveMeshMaterial : nullptr;
		if (!BaseMaterial)
		{
			BaseMaterial = LoadObject<UMaterialInterface>(
				nullptr,
				TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
		}
		if (!BaseMaterial)
		{
			BaseMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
		}

		UMaterialInstanceDynamic* ReadableMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, GetTransientPackage());
		if (ReadableMaterial)
		{
			ReadableMaterial->SetVectorParameterValue(TEXT("EmissiveColor"), FLinearColor(0.86f, 0.88f, 0.90f, 1.0f));
			ReadableMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.86f, 0.88f, 0.90f, 1.0f));
			ReadableMaterial->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.86f, 0.88f, 0.90f, 1.0f));
			ReadableMaterial->AddToRoot();
			CachedReadableMaterial = ReadableMaterial;
			return ReadableMaterial;
		}

		return BaseMaterial;
	}

	struct FTMScopedMeshMaterialOverride
	{
		FTMScopedMeshMaterialOverride(UObject* InSourceObject, UMaterialInterface* InOverrideMaterial)
			: SourceObject(InSourceObject)
			, OverrideMaterial(InOverrideMaterial)
			, SourcePackage(InSourceObject ? InSourceObject->GetOutermost() : nullptr)
			, bSourcePackageWasDirty(SourcePackage ? SourcePackage->IsDirty() : false)
		{
			if (!SourceObject || !OverrideMaterial)
			{
				return;
			}

			if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(SourceObject))
			{
				const int32 MaterialCount = StaticMesh->GetStaticMaterials().Num();
				OriginalMaterials.Reserve(MaterialCount);
				for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
				{
					OriginalMaterials.Add(StaticMesh->GetMaterial(MaterialIndex));
					StaticMesh->SetMaterial(MaterialIndex, OverrideMaterial);
				}
				bAppliedToStaticMesh = MaterialCount > 0;
				return;
			}

			if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(SourceObject))
			{
				OriginalSkeletalMaterials = SkeletalMesh->GetMaterials();
				TArray<FSkeletalMaterial> OverrideMaterials = OriginalSkeletalMaterials;
				for (FSkeletalMaterial& Material : OverrideMaterials)
				{
					Material.MaterialInterface = OverrideMaterial;
				}
				SkeletalMesh->SetMaterials(OverrideMaterials);
				bAppliedToSkeletalMesh = !OverrideMaterials.IsEmpty();
			}
		}

		~FTMScopedMeshMaterialOverride()
		{
			if (UStaticMesh* StaticMesh = bAppliedToStaticMesh ? Cast<UStaticMesh>(SourceObject) : nullptr)
			{
				for (int32 MaterialIndex = 0; MaterialIndex < OriginalMaterials.Num(); ++MaterialIndex)
				{
					StaticMesh->SetMaterial(MaterialIndex, OriginalMaterials[MaterialIndex]);
				}
			}
			else if (USkeletalMesh* SkeletalMesh = bAppliedToSkeletalMesh ? Cast<USkeletalMesh>(SourceObject) : nullptr)
			{
				SkeletalMesh->SetMaterials(OriginalSkeletalMaterials);
			}

			if (SourcePackage && !bSourcePackageWasDirty)
			{
				SourcePackage->SetDirtyFlag(false);
			}
		}

		UObject* SourceObject = nullptr;
		UMaterialInterface* OverrideMaterial = nullptr;
		UPackage* SourcePackage = nullptr;
		bool bSourcePackageWasDirty = false;
		bool bAppliedToStaticMesh = false;
		bool bAppliedToSkeletalMesh = false;
		TArray<UMaterialInterface*> OriginalMaterials;
		TArray<FSkeletalMaterial> OriginalSkeletalMaterials;
	};

	bool TMRenderMeshThumbnailPixels(
		UObject* SourceObject,
		TArray<uint8>& OutPixels,
		int32& OutWidth,
		int32& OutHeight,
		const bool bUseReadableMaterial)
	{
		OutPixels.Reset();
		OutWidth = 0;
		OutHeight = 0;
		if (!SourceObject)
		{
			return false;
		}

		FObjectThumbnail Thumbnail;
		{
			FTMScopedMeshMaterialOverride MaterialOverride(
				SourceObject,
				bUseReadableMaterial ? TMGetReadableIconMaterial() : nullptr);
			FTMScopedThumbnailAngleOverride ThumbnailAngleOverride(SourceObject);
			ThumbnailTools::RenderThumbnail(
				SourceObject,
				TMThumbnailRenderSize,
				TMThumbnailRenderSize,
				ThumbnailTools::EThumbnailTextureFlushMode::AlwaysFlush,
				nullptr,
				&Thumbnail);
		}

		if (Thumbnail.IsEmpty() || !Thumbnail.HasValidImageData())
		{
			return false;
		}

		OutPixels = Thumbnail.GetUncompressedImageData();
		OutWidth = Thumbnail.GetImageWidth();
		OutHeight = Thumbnail.GetImageHeight();
		return OutWidth > 0 && OutHeight > 0 && OutPixels.Num() == OutWidth * OutHeight * 4;
	}

	bool TMRenderStaticMeshSceneCapturePixels(
		UStaticMesh* StaticMesh,
		TArray<uint8>& OutPixels,
		int32& OutWidth,
		int32& OutHeight)
	{
		OutPixels.Reset();
		OutWidth = 0;
		OutHeight = 0;
		if (!StaticMesh)
		{
			return false;
		}

		FPreviewScene PreviewScene(FPreviewScene::ConstructionValues()
			.SetCreateDefaultLighting(true)
			.SetLightRotation(FRotator(-48.0f, -42.0f, 0.0f))
			.SetLightBrightness(8.0f)
			.SetSkyBrightness(0.8f)
			.AllowAudioPlayback(false)
			.SetCreatePhysicsScene(false)
			.ShouldSimulatePhysics(false)
			.SetTransactional(false)
			.SetEditor(true));
		UWorld* PreviewWorld = PreviewScene.GetWorld();
		if (!PreviewWorld)
		{
			return false;
		}

		const FBoxSphereBounds MeshBounds = StaticMesh->GetBounds();
		UStaticMeshComponent* MeshComponent = NewObject<UStaticMeshComponent>(GetTransientPackage(), TEXT("TMIconPreviewMesh"));
		if (!MeshComponent)
		{
			return false;
		}

		FRotator MeshRotation = FRotator::ZeroRotator;
		const FString StaticMeshName = StaticMesh->GetName();

		MeshComponent->SetStaticMesh(StaticMesh);
		MeshComponent->SetForcedLodModel(1);
		MeshComponent->SetMobility(EComponentMobility::Movable);
		MeshComponent->SetVisibility(true);
		MeshComponent->SetHiddenInGame(false);
		MeshComponent->SetCastShadow(false);
		PreviewScene.AddComponent(
			MeshComponent,
			FTransform(MeshRotation, -MeshRotation.RotateVector(MeshBounds.Origin)));

		UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), TEXT("TMIconPreviewRenderTarget"));
		if (!RenderTarget)
		{
			return false;
		}

		RenderTarget->ClearColor = FLinearColor::Transparent;
		RenderTarget->InitCustomFormat(TMThumbnailRenderSize, TMThumbnailRenderSize, PF_B8G8R8A8, true);
		RenderTarget->UpdateResourceImmediate(true);

		USceneCaptureComponent2D* CaptureComponent = NewObject<USceneCaptureComponent2D>(
			GetTransientPackage(),
			TEXT("TMIconPreviewCapture"));
		if (!CaptureComponent)
		{
			return false;
		}

		constexpr float FieldOfViewDegrees = 26.0f;
		const bool bForeGrip = StaticMeshName.Equals(TEXT("ForeGrip"), ESearchCase::IgnoreCase);
		const FRotator ViewRotation = bForeGrip
			? FRotator(-6.0f, -92.0f, 0.0f)
			: FRotator(-18.0f, -38.0f, 0.0f);
		const FVector ViewForward = ViewRotation.Vector();
		const float ViewDistance = FMath::Max(
			MeshBounds.SphereRadius / FMath::Tan(FMath::DegreesToRadians(FieldOfViewDegrees * 0.5f)) * (bForeGrip ? 1.45f : 1.30f),
			20.0f);

		CaptureComponent->TextureTarget = RenderTarget;
		CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		CaptureComponent->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
		CaptureComponent->FOVAngle = FieldOfViewDegrees;
		CaptureComponent->bCaptureEveryFrame = false;
		CaptureComponent->bCaptureOnMovement = false;
		CaptureComponent->ShowFlags.SetLighting(true);
		CaptureComponent->ShowFlags.SetMaterials(true);
		CaptureComponent->ShowFlags.SetPostProcessing(true);
		CaptureComponent->ShowFlags.SetAntiAliasing(false);
		CaptureComponent->PostProcessSettings.bOverride_AutoExposureMethod = true;
		CaptureComponent->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
		CaptureComponent->PostProcessSettings.bOverride_AutoExposureBias = true;
		CaptureComponent->PostProcessSettings.AutoExposureBias = bForeGrip ? 1.6f : 1.0f;
		CaptureComponent->PostProcessSettings.bOverride_BloomIntensity = true;
		CaptureComponent->PostProcessSettings.BloomIntensity = 0.0f;
		CaptureComponent->PostProcessBlendWeight = 1.0f;
		PreviewScene.AddComponent(
			CaptureComponent,
			FTransform(ViewRotation, -ViewForward * ViewDistance));

		PreviewWorld->UpdateWorldComponents(true, false);
		MeshComponent->MarkRenderStateDirty();
		CaptureComponent->MarkRenderStateDirty();
		PreviewWorld->Tick(LEVELTICK_All, 0.0f);
		CaptureComponent->CaptureScene();
		FlushRenderingCommands();

		FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
		if (!RenderTargetResource)
		{
			return false;
		}

		TArray<FColor> SurfacePixels;
		FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
		ReadFlags.SetLinearToGamma(true);
		if (!RenderTargetResource->ReadPixels(SurfacePixels, ReadFlags)
			|| SurfacePixels.Num() != TMThumbnailRenderSize * TMThumbnailRenderSize)
		{
			return false;
		}

		OutWidth = TMThumbnailRenderSize;
		OutHeight = TMThumbnailRenderSize;
		OutPixels.Init(0, OutWidth * OutHeight * 4);
		for (int32 PixelIndex = 0; PixelIndex < SurfacePixels.Num(); ++PixelIndex)
		{
			const FColor& Color = SurfacePixels[PixelIndex];
			const int32 DestIndex = PixelIndex * 4;
			OutPixels[DestIndex] = Color.B;
			OutPixels[DestIndex + 1] = Color.G;
			OutPixels[DestIndex + 2] = Color.R;
			OutPixels[DestIndex + 3] = Color.A;
		}

		return OutPixels.Num() == OutWidth * OutHeight * 4;
	}

	bool TMRenderSkeletalMeshSceneCapturePixels(
		USkeletalMesh* SkeletalMesh,
		TArray<uint8>& OutPixels,
		int32& OutWidth,
		int32& OutHeight,
		const bool bUseReadableMaterial)
	{
		OutPixels.Reset();
		OutWidth = 0;
		OutHeight = 0;
		if (!SkeletalMesh)
		{
			return false;
		}

		FPreviewScene PreviewScene(FPreviewScene::ConstructionValues()
			.SetCreateDefaultLighting(true)
			.SetLightRotation(FRotator(-48.0f, -42.0f, 0.0f))
			.SetLightBrightness(8.0f)
			.SetSkyBrightness(0.8f)
			.AllowAudioPlayback(false)
			.SetCreatePhysicsScene(false)
			.ShouldSimulatePhysics(false)
			.SetTransactional(false)
			.SetEditor(true));
		UWorld* PreviewWorld = PreviewScene.GetWorld();
		if (!PreviewWorld)
		{
			return false;
		}

		const FBoxSphereBounds MeshBounds = SkeletalMesh->GetBounds();
		USkeletalMeshComponent* MeshComponent = NewObject<USkeletalMeshComponent>(
			GetTransientPackage(),
			TEXT("TMIconPreviewSkeletalMesh"));
		if (!MeshComponent)
		{
			return false;
		}

		MeshComponent->SetSkeletalMeshAsset(SkeletalMesh);
		MeshComponent->SetForcedLOD(1);
		MeshComponent->SetMobility(EComponentMobility::Movable);
		MeshComponent->SetVisibility(true);
		MeshComponent->SetHiddenInGame(false);
		MeshComponent->SetCastShadow(false);

		if (bUseReadableMaterial)
		{
			if (UMaterialInterface* ReadableMaterial = TMGetReadableIconMaterial())
			{
				for (int32 MaterialIndex = 0; MaterialIndex < MeshComponent->GetNumMaterials(); ++MaterialIndex)
				{
					MeshComponent->SetMaterial(MaterialIndex, ReadableMaterial);
				}
			}
		}

		const FRotator MeshRotation = FRotator::ZeroRotator;
		PreviewScene.AddComponent(
			MeshComponent,
			FTransform(MeshRotation, -MeshRotation.RotateVector(MeshBounds.Origin)));

		UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), TEXT("TMIconPreviewSkeletalRenderTarget"));
		if (!RenderTarget)
		{
			return false;
		}

		RenderTarget->ClearColor = FLinearColor::Transparent;
		RenderTarget->InitCustomFormat(TMThumbnailRenderSize, TMThumbnailRenderSize, PF_B8G8R8A8, true);
		RenderTarget->UpdateResourceImmediate(true);

		USceneCaptureComponent2D* CaptureComponent = NewObject<USceneCaptureComponent2D>(
			GetTransientPackage(),
			TEXT("TMIconPreviewSkeletalCapture"));
		if (!CaptureComponent)
		{
			return false;
		}

		constexpr float FieldOfViewDegrees = 26.0f;
		const FRotator ViewRotation(-18.0f, -38.0f, 0.0f);
		const FVector ViewForward = ViewRotation.Vector();
		const float ViewDistance = FMath::Max(
			MeshBounds.SphereRadius / FMath::Tan(FMath::DegreesToRadians(FieldOfViewDegrees * 0.5f)) * 1.30f,
			20.0f);

		CaptureComponent->TextureTarget = RenderTarget;
		CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		CaptureComponent->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
		CaptureComponent->FOVAngle = FieldOfViewDegrees;
		CaptureComponent->bCaptureEveryFrame = false;
		CaptureComponent->bCaptureOnMovement = false;
		CaptureComponent->ShowFlags.SetLighting(true);
		CaptureComponent->ShowFlags.SetMaterials(true);
		CaptureComponent->ShowFlags.SetPostProcessing(true);
		CaptureComponent->ShowFlags.SetAntiAliasing(false);
		CaptureComponent->PostProcessSettings.bOverride_AutoExposureMethod = true;
		CaptureComponent->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
		CaptureComponent->PostProcessSettings.bOverride_AutoExposureBias = true;
		CaptureComponent->PostProcessSettings.AutoExposureBias = 1.0f;
		CaptureComponent->PostProcessSettings.bOverride_BloomIntensity = true;
		CaptureComponent->PostProcessSettings.BloomIntensity = 0.0f;
		CaptureComponent->PostProcessBlendWeight = 1.0f;
		PreviewScene.AddComponent(
			CaptureComponent,
			FTransform(ViewRotation, -ViewForward * ViewDistance));

		PreviewWorld->UpdateWorldComponents(true, false);
		MeshComponent->MarkRenderStateDirty();
		CaptureComponent->MarkRenderStateDirty();
		PreviewWorld->Tick(LEVELTICK_All, 0.0f);
		CaptureComponent->CaptureScene();
		FlushRenderingCommands();

		FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
		if (!RenderTargetResource)
		{
			return false;
		}

		TArray<FColor> SurfacePixels;
		FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
		ReadFlags.SetLinearToGamma(true);
		if (!RenderTargetResource->ReadPixels(SurfacePixels, ReadFlags)
			|| SurfacePixels.Num() != TMThumbnailRenderSize * TMThumbnailRenderSize)
		{
			return false;
		}

		OutWidth = TMThumbnailRenderSize;
		OutHeight = TMThumbnailRenderSize;
		OutPixels.Init(0, OutWidth * OutHeight * 4);
		for (int32 PixelIndex = 0; PixelIndex < SurfacePixels.Num(); ++PixelIndex)
		{
			const FColor& Color = SurfacePixels[PixelIndex];
			const int32 DestIndex = PixelIndex * 4;
			OutPixels[DestIndex] = Color.B;
			OutPixels[DestIndex + 1] = Color.G;
			OutPixels[DestIndex + 2] = Color.R;
			OutPixels[DestIndex + 3] = Color.A;
		}

		return OutPixels.Num() == OutWidth * OutHeight * 4;
	}

	bool TMBuildSkeletalSceneCaptureIconPixels(
		USkeletalMesh* SkeletalMesh,
		const bool bUseReadableMaterial,
		TArray<uint8>& OutPixels)
	{
		OutPixels.Reset();

		TArray<uint8> RenderPixels;
		int32 RenderWidth = 0;
		int32 RenderHeight = 0;
		if (!TMRenderSkeletalMeshSceneCapturePixels(
			SkeletalMesh,
			RenderPixels,
			RenderWidth,
			RenderHeight,
			bUseReadableMaterial))
		{
			return false;
		}

		const bool bSceneFitted = TMBuildButtonIconPixelsFromAlpha(RenderPixels, RenderWidth, RenderHeight, OutPixels)
			|| TMBuildButtonIconPixels(RenderPixels, RenderWidth, RenderHeight, OutPixels);
		if (!bSceneFitted)
		{
			OutPixels.Reset();
			return false;
		}

		TMNormalizeSceneCaptureIconExposure(OutPixels, TMIconWidth, TMIconHeight);
		if (bUseReadableMaterial && !TMIconHasReadableColor(OutPixels, TMIconWidth, TMIconHeight))
		{
			TMApplyReadableTint(OutPixels, TMIconWidth, TMIconHeight);
		}

		if (!TMIconHasReadableColor(OutPixels, TMIconWidth, TMIconHeight))
		{
			OutPixels.Reset();
			return false;
		}

		return true;
	}

	bool TMBuildRealMaterialSceneCaptureIconPixels(UObject* RenderSourceObject, TArray<uint8>& OutPixels)
	{
		OutPixels.Reset();
		if (!RenderSourceObject)
		{
			return false;
		}

		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(RenderSourceObject))
		{
			TArray<uint8> RenderPixels;
			int32 RenderWidth = 0;
			int32 RenderHeight = 0;
			if (!TMRenderStaticMeshSceneCapturePixels(StaticMesh, RenderPixels, RenderWidth, RenderHeight))
			{
				return false;
			}

			const bool bSceneFitted = TMBuildButtonIconPixelsFromAlpha(RenderPixels, RenderWidth, RenderHeight, OutPixels)
				|| TMBuildButtonIconPixels(RenderPixels, RenderWidth, RenderHeight, OutPixels);
			if (bSceneFitted)
			{
				TMNormalizeSceneCaptureIconExposure(OutPixels, TMIconWidth, TMIconHeight);
			}
			return bSceneFitted && OutPixels.Num() == TMIconWidth * TMIconHeight * 4;
		}

		if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(RenderSourceObject))
		{
			TArray<uint8> RenderPixels;
			int32 RenderWidth = 0;
			int32 RenderHeight = 0;
			if (!TMRenderSkeletalMeshSceneCapturePixels(SkeletalMesh, RenderPixels, RenderWidth, RenderHeight, false))
			{
				return false;
			}

			const bool bSceneFitted = TMBuildButtonIconPixelsFromAlpha(RenderPixels, RenderWidth, RenderHeight, OutPixels)
				|| TMBuildButtonIconPixels(RenderPixels, RenderWidth, RenderHeight, OutPixels);
			if (bSceneFitted)
			{
				TMNormalizeSceneCaptureIconExposure(OutPixels, TMIconWidth, TMIconHeight);
				const bool bAllowDarkRealMaterialIcon = SkeletalMesh->GetPathName().Equals(
					TEXT("/Game/Weapons/Mesh/GrenadesAndMine/SK_Frag_Grenade.SK_Frag_Grenade"),
					ESearchCase::IgnoreCase);
				if (!bAllowDarkRealMaterialIcon && !TMRealMaterialIconHasUsableDetail(OutPixels, TMIconWidth, TMIconHeight))
				{
					UE_LOG(
						LogTemp,
						Display,
						TEXT("[TMIconGenerator] Rejected dark real-material skeletal mesh render for %s."),
						*SkeletalMesh->GetPathName());
					OutPixels.Reset();
					return false;
				}
			}
			return bSceneFitted && OutPixels.Num() == TMIconWidth * TMIconHeight * 4;
		}

		return false;
	}

	UTexture2D* TMFindLoadoutWeaponSourceIconTexture(const FAssetData& SourceAsset)
	{
		const FString ObjectPath = SourceAsset.GetObjectPathString();
		struct FTMSourceIconOverride
		{
			const TCHAR* SourcePath = nullptr;
			const TCHAR* TexturePath = nullptr;
		};

		static const FTMSourceIconOverride SourceIconOverrides[] =
		{
			{
				TEXT("/Game/MP_System_V3/Game/Weapons/Primary/Shotgun/Meshes/Shotgun.Shotgun"),
				TEXT("/Game/Weapons/Textures/UI/T_M890_HUD.T_M890_HUD")
			},
			{
				TEXT("/Game/MP_System_V3/Game/Weapons/Primary/SMG/Meshes/SMG_Kriss.SMG_Kriss"),
				TEXT("/Game/Weapons/Textures/UI/T_V014_HUD.T_V014_HUD")
			},
			{
				TEXT("/Game/MP_System_V3/Game/Weapons/Primary/SMG/Meshes/SMG_ACWI.SMG_ACWI"),
				TEXT("/Game/Weapons/Textures/UI/T_ACWI_HUD.T_ACWI_HUD")
			},
			{
				TEXT("/Game/MP_System_V3/Game/Weapons/Secondary/DE/Meshes/DE.DE"),
				TEXT("/Game/Weapons/Textures/UI/T_DE-42_HUD.T_DE-42_HUD")
			},
			{
				TEXT("/Game/MP_System_V3/Game/Weapons/Secondary/Pistol/Meshes/M9.M9"),
				TEXT("/Game/Weapons/Textures/UI/T_FPN16_HUD.T_FPN16_HUD")
			},
			{
				TEXT("/Game/Weapons/Mesh/Knife/SK_Knife1.SK_Knife1"),
				TEXT("/Game/AdvanceWeaponPack/Texture/UI/T_Knife_Icon.T_Knife_Icon")
			}
		};

		for (const FTMSourceIconOverride& SourceIconOverride : SourceIconOverrides)
		{
			if (ObjectPath.Equals(SourceIconOverride.SourcePath, ESearchCase::IgnoreCase))
			{
				return LoadObject<UTexture2D>(nullptr, SourceIconOverride.TexturePath);
			}
		}

		return nullptr;
	}

	struct FTMLoadoutSourceIconTransform
	{
		float RotationDegrees = 0.0f;
		float TargetWidthRatio = 0.90f;
		float TargetHeightRatio = 0.90f;
		float ScaleMultiplier = 1.0f;
	};

	bool TMFindLoadoutWeaponSourceIconTransform(
		const FAssetData& SourceAsset,
		FTMLoadoutSourceIconTransform& OutTransform)
	{
		const FString ObjectPath = SourceAsset.GetObjectPathString();
		struct FTMSourceIconTransformOverride
		{
			const TCHAR* SourcePath = nullptr;
			FTMLoadoutSourceIconTransform Transform;
		};

		static const FTMSourceIconTransformOverride TransformOverrides[] =
		{
			{
				TEXT("/Game/MP_System_V3/Game/Weapons/Primary/Shotgun/Meshes/Shotgun.Shotgun"),
				{ 40.0f, 0.98f, 0.98f, 1.0f }
			},
			{
				TEXT("/Game/MP_System_V3/Game/Weapons/Primary/SMG/Meshes/SMG_Kriss.SMG_Kriss"),
				{ 25.0f, 0.98f, 0.98f, 1.0f }
			},
			{
				TEXT("/Game/MP_System_V3/Game/Weapons/Primary/SMG/Meshes/SMG_ACWI.SMG_ACWI"),
				{ 25.0f, 0.98f, 0.98f, 1.0f }
			},
			{
				TEXT("/Game/MP_System_V3/Game/Weapons/Secondary/DE/Meshes/DE.DE"),
				{ 60.0f, 0.70f, 0.98f, 1.0f }
			},
			{
				TEXT("/Game/MP_System_V3/Game/Weapons/Secondary/Pistol/Meshes/M9.M9"),
				{ 59.0f, 0.70f, 0.98f, 1.0f }
			}
		};

		for (const FTMSourceIconTransformOverride& TransformOverride : TransformOverrides)
		{
			if (ObjectPath.Equals(TransformOverride.SourcePath, ESearchCase::IgnoreCase))
			{
				OutTransform = TransformOverride.Transform;
				return true;
			}
		}

		return false;
	}

	void TMKeepLargestLoadoutIconAlphaComponent(TArray<uint8>& PixelData, const int32 Width, const int32 Height)
	{
		if (Width <= 0 || Height <= 0 || PixelData.Num() != Width * Height * 4)
		{
			return;
		}

		const int32 PixelCount = Width * Height;
		TArray<uint8> Seen;
		Seen.Init(0, PixelCount);
		TArray<int32> Stack;
		TArray<int32> ComponentPixels;
		TArray<int32> LargestComponentPixels;

		for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
		{
			if (Seen[PixelIndex] != 0 || PixelData[(PixelIndex * 4) + 3] <= 16)
			{
				continue;
			}

			Stack.Reset();
			ComponentPixels.Reset();
			Stack.Add(PixelIndex);
			Seen[PixelIndex] = 1;
			while (!Stack.IsEmpty())
			{
				const int32 CurrentIndex = Stack.Pop(EAllowShrinking::No);
				ComponentPixels.Add(CurrentIndex);

				const int32 X = CurrentIndex % Width;
				const int32 Y = CurrentIndex / Width;
				for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
				{
					for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
					{
						if (OffsetX == 0 && OffsetY == 0)
						{
							continue;
						}

						const int32 NeighborX = X + OffsetX;
						const int32 NeighborY = Y + OffsetY;
						if (NeighborX < 0 || NeighborX >= Width || NeighborY < 0 || NeighborY >= Height)
						{
							continue;
						}

						const int32 NeighborIndex = (NeighborY * Width) + NeighborX;
						if (Seen[NeighborIndex] == 0 && PixelData[(NeighborIndex * 4) + 3] > 16)
						{
							Seen[NeighborIndex] = 1;
							Stack.Add(NeighborIndex);
						}
					}
				}
			}

			if (ComponentPixels.Num() > LargestComponentPixels.Num())
			{
				LargestComponentPixels = ComponentPixels;
			}
		}

		TArray<uint8> KeepMask;
		KeepMask.Init(0, PixelCount);
		for (const int32 ComponentPixelIndex : LargestComponentPixels)
		{
			KeepMask[ComponentPixelIndex] = 1;
		}

		for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
		{
			if (KeepMask[PixelIndex] != 0)
			{
				continue;
			}

			const int32 DataIndex = PixelIndex * 4;
			PixelData[DataIndex] = 0;
			PixelData[DataIndex + 1] = 0;
			PixelData[DataIndex + 2] = 0;
			PixelData[DataIndex + 3] = 0;
		}
	}

	bool TMBuildLoadoutWeaponMaterialIconPixels(
		const FAssetData& AssetData,
		UObject* RenderSourceObject,
		const bool bActiveVariant,
		TArray<uint8>& OutPixels)
	{
		OutPixels.Reset();
		if (!RenderSourceObject)
		{
			return false;
		}

		bool bBuiltPixels = false;
		const bool bIsScar = AssetData.GetObjectPathString().Contains(TEXT("SMG_Scar"), ESearchCase::IgnoreCase);
		if (UTexture2D* SourceIconTexture = TMFindLoadoutWeaponSourceIconTexture(AssetData))
		{
			TArray<uint8> SourceIconPixels;
			int32 SourceIconWidth = 0;
			int32 SourceIconHeight = 0;
			if (TMReadTextureSourceBgra8(SourceIconTexture, SourceIconPixels, SourceIconWidth, SourceIconHeight))
			{
				FTMLoadoutSourceIconTransform SourceIconTransform;
				if (TMFindLoadoutWeaponSourceIconTransform(AssetData, SourceIconTransform))
				{
					bBuiltPixels = TMBuildTransformedButtonIconPixels(
						SourceIconPixels,
						SourceIconWidth,
						SourceIconHeight,
						SourceIconTransform.RotationDegrees,
						SourceIconTransform.TargetWidthRatio,
						SourceIconTransform.TargetHeightRatio,
						SourceIconTransform.ScaleMultiplier,
						OutPixels);
				}

				if (!bBuiltPixels)
				{
					bBuiltPixels = TMBuildButtonIconPixelsFromAlpha(SourceIconPixels, SourceIconWidth, SourceIconHeight, OutPixels)
						|| TMBuildButtonIconPixels(SourceIconPixels, SourceIconWidth, SourceIconHeight, OutPixels);
				}
				if (bBuiltPixels)
				{
					UE_LOG(
						LogTemp,
						Display,
						TEXT("[TMIconGenerator] Using baked source HUD texture %s for %s."),
						*SourceIconTexture->GetPathName(),
						*AssetData.GetObjectPathString());
				}
			}
			else
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[TMIconGenerator] Failed to read baked source HUD texture %s for %s."),
					*SourceIconTexture->GetPathName(),
					*AssetData.GetObjectPathString());
			}
		}

		if (!bBuiltPixels)
		{
			if (bIsScar && TMBuildSkeletalMeshProjectedIconPixels(Cast<USkeletalMesh>(RenderSourceObject), OutPixels))
			{
				bBuiltPixels = true;
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMIconGenerator] Applied Scar loadout material projection before scene capture for %s."),
					*AssetData.GetObjectPathString());
			}
		}

		if (!bBuiltPixels)
		{
			bBuiltPixels = TMBuildRealMaterialSceneCaptureIconPixels(RenderSourceObject, OutPixels);
			if (!bBuiltPixels)
			{
				if (TMIsMeleeLoadoutMeshSource(AssetData)
					&& TMBuildMeleeStaticMeshProjectedIconPixels(Cast<UStaticMesh>(RenderSourceObject), OutPixels))
				{
					bBuiltPixels = true;
					UE_LOG(
						LogTemp,
						Display,
						TEXT("[TMIconGenerator] Applied melee loadout material projection fallback for %s."),
						*AssetData.GetObjectPathString());
				}
			}

			if (!bBuiltPixels)
			{
				if (TMIsWeaponDataTableMeshSourceWithVisualOverride(AssetData)
					&& TMBuildSkeletalMeshProjectedIconPixels(Cast<USkeletalMesh>(RenderSourceObject), OutPixels))
				{
					bBuiltPixels = true;
					UE_LOG(
						LogTemp,
						Display,
						TEXT("[TMIconGenerator] Applied loadout weapon projection fallback for %s."),
						*AssetData.GetObjectPathString());
				}
			}

			if (!bBuiltPixels)
			{
				return false;
			}
		}

		const bool bIsFrag = TMIsFragDataTableMeshSource(AssetData);
		if (bIsFrag)
		{
			TMKeepLargestLoadoutIconAlphaComponent(OutPixels, TMIconWidth, TMIconHeight);
		}
		else
		{
			TMCleanLoadoutWeaponIconAlpha(OutPixels, TMIconWidth, TMIconHeight);
		}
		if (TMIsMeleeLoadoutMeshSource(AssetData))
		{
			TMOrientMeleeLoadoutIconHorizontal(OutPixels);
			TMSmoothMeleeLoadoutIconMaterial(OutPixels, TMIconWidth, TMIconHeight);
		}

		float PostFitWidthRatio = 0.0f;
		float PostFitHeightRatio = 0.0f;
		if (TMFindLoadoutWeaponPostIconFit(AssetData, PostFitWidthRatio, PostFitHeightRatio))
		{
			TMFitIconForegroundToCanvas(OutPixels, TMIconWidth, TMIconHeight, PostFitWidthRatio, PostFitHeightRatio);
		}
		if (bIsFrag)
		{
			// Keep Frag in its captured material colors; the old UI tint made it a flat green silhouette.
		}
		TMApplyLoadoutWeaponRowIconAlignment(AssetData, OutPixels);
		TMRemoveScarLoadoutIconCaptureArtifacts(AssetData, OutPixels);
		if (!bIsFrag)
		{
			TMApplyLoadoutWeaponMaterialIconTone(OutPixels, TMIconWidth, TMIconHeight, bActiveVariant);
		}
		return OutPixels.Num() == TMIconWidth * TMIconHeight * 4;
	}

	UTexture2D* TMFindPreferredSourceIconTexture(const FAssetData& SourceAsset)
	{
		const FString SourceAssetName = SourceAsset.AssetName.ToString();
		struct FTMPreferredSourceIcon
		{
			const TCHAR* MeshNameToken = nullptr;
			const TCHAR* TexturePath = nullptr;
			bool bExactMatch = false;
		};

		static const FTMPreferredSourceIcon PreferredSourceIcons[] =
		{
			{
				TEXT("Silencer"),
				TEXT("/Game/Weapons/Textures/UI/T_Silencer_Barrel_HUD.T_Silencer_Barrel_HUD"),
				true
			},
			{
				TEXT("Compensator"),
				TEXT("/Game/AdvanceWeaponPack/Texture/UI/T_Compensator_Icon.T_Compensator_Icon"),
				true
			},
			{
				TEXT("ForeGrip"),
				TEXT("/Game/AdvanceWeaponPack/Texture/UI/T_GripA_Icon.T_GripA_Icon"),
				true
			},
			{
				TEXT("SM_VerticleTypeB_Grip"),
				TEXT("/Game/Weapons/Textures/UI/T_VerticalB_Grip_HUD.T_VerticalB_Grip_HUD"),
				true
			},
			{
				TEXT("SM_VerticleTypeC_Grip"),
				TEXT("/Game/Weapons/Textures/UI/T_VerticalC_Grip_HUD.T_VerticalC_Grip_HUD"),
				true
			},
			{
				TEXT("SM_Holographic_Sight"),
				TEXT("/Game/AdvanceWeaponPack/Texture/UI/T_Holographic_Icon.T_Holographic_Icon"),
				true
			},
			{
				TEXT("SM_Holographic_Scope"),
				TEXT("/Game/Weapons/Textures/UI/T_Holographic_Sight_HUD.T_Holographic_Sight_HUD"),
				true
			},
			{
				TEXT("RearSight"),
				TEXT("/Game/Weapons/Textures/UI/T_IronSight_Sight_HUD.T_IronSight_Sight_HUD"),
				true
			},
			{
				TEXT("FrontSight"),
				TEXT("/Game/Weapons/Textures/UI/T_IronSight_Sight_HUD.T_IronSight_Sight_HUD"),
				true
			},
			{
				TEXT("MiniSight"),
				TEXT("/Game/Weapons/Textures/UI/T_Reflex_Sight_HUD.T_Reflex_Sight_HUD"),
				true
			},
			{
				TEXT("SciFi_Scope"),
				TEXT("/Game/Weapons/Textures/UI/T_SVS16X_Sight_HUD.T_SVS16X_Sight_HUD"),
				true
			},
			{
				TEXT("SM_ACOG_Scope"),
				TEXT("/Game/Weapons/Textures/UI/T_ACOG_Sight_HUD.T_ACOG_Sight_HUD"),
				true
			},
			{
				TEXT("SM_RedDot_Sight"),
				TEXT("/Game/AdvanceWeaponPack/Texture/UI/T_RedDot_Icon.T_RedDot_Icon"),
				true
			},
			{
				TEXT("SM_Reflex_Sight"),
				TEXT("/Game/AdvanceWeaponPack/Texture/UI/T_Reflex_Icon.T_Reflex_Icon"),
				true
			},
			{
				TEXT("SM_Specter_Sight"),
				TEXT("/Game/AdvanceWeaponPack/Texture/UI/T_Specter2X_Icon.T_Specter2X_Icon"),
				true
			},
			{
				TEXT("SM_UTC_Sight"),
				TEXT("/Game/AdvanceWeaponPack/Texture/UI/T_UTC8X_Icon.T_UTC8X_Icon"),
				true
			},
			{
				TEXT("SM_DotSight"),
				TEXT("/Game/AdvanceWeaponPack/Texture/UI/T_RedDot_Icon.T_RedDot_Icon"),
				true
			},
			{
				TEXT("RDS"),
				TEXT("/Game/Weapons/Textures/UI/T_RedDot_Sight_HUD.T_RedDot_Sight_HUD"),
				true
			},
			{
				TEXT("SM_DemoOptic"),
				TEXT("/Game/Fps/Textures/Widgets/GunCustomization/T_ThumbDemoOptics.T_ThumbDemoOptics"),
				true
			}
		};

		for (const FTMPreferredSourceIcon& PreferredSourceIcon : PreferredSourceIcons)
		{
			const bool bMatches = PreferredSourceIcon.bExactMatch
				? SourceAssetName.Equals(PreferredSourceIcon.MeshNameToken, ESearchCase::IgnoreCase)
				: SourceAssetName.Contains(PreferredSourceIcon.MeshNameToken, ESearchCase::IgnoreCase);
			if (bMatches)
			{
				return LoadObject<UTexture2D>(nullptr, PreferredSourceIcon.TexturePath);
			}
		}

		return nullptr;
	}

	bool TMShouldUseSceneCaptureMeshRender(const FAssetData& SourceAsset)
	{
		const FString AssetName = SourceAsset.AssetName.ToString();
		return AssetName.Equals(TEXT("RDS"), ESearchCase::IgnoreCase)
			|| AssetName.Equals(TEXT("SM_Laser"), ESearchCase::IgnoreCase)
			|| AssetName.Equals(TEXT("ForeGrip"), ESearchCase::IgnoreCase)
			|| AssetName.Equals(TEXT("V_Grip"), ESearchCase::IgnoreCase)
			|| AssetName.Contains(TEXT("Grip"), ESearchCase::IgnoreCase)
			|| AssetName.Equals(TEXT("SM_Suppressor_Barrel"), ESearchCase::IgnoreCase)
			|| AssetName.Contains(TEXT("Suppressor"), ESearchCase::IgnoreCase)
			|| AssetName.Equals(TEXT("Silencer"), ESearchCase::IgnoreCase)
			|| AssetName.Equals(TEXT("Compensator"), ESearchCase::IgnoreCase);
	}

	bool TMShouldKeepRealMaterialStaticAttachmentIcon(const FAssetData& SourceAsset)
	{
		const FString ObjectPath = SourceAsset.GetObjectPathString();
		if (!ObjectPath.Contains(TEXT("/Mesh/Attachment/"), ESearchCase::IgnoreCase))
		{
			return false;
		}

		const FString AssetName = SourceAsset.AssetName.ToString();
		return AssetName.Contains(TEXT("Grip"), ESearchCase::IgnoreCase)
			|| AssetName.Contains(TEXT("Suppressor"), ESearchCase::IgnoreCase)
			|| AssetName.Contains(TEXT("Silencer"), ESearchCase::IgnoreCase)
			|| AssetName.Contains(TEXT("Compensator"), ESearchCase::IgnoreCase);
	}

	bool TMShouldUseSkeletalMeshFallbackRender(const FAssetData& SourceAsset)
	{
		const FString AssetName = SourceAsset.AssetName.ToString();
		return AssetName.Equals(TEXT("Shotgun"), ESearchCase::IgnoreCase)
			|| AssetName.Equals(TEXT("SMG_Kriss"), ESearchCase::IgnoreCase)
			|| AssetName.Equals(TEXT("SMG_ACWI"), ESearchCase::IgnoreCase)
			|| AssetName.Equals(TEXT("SMG_TAR"), ESearchCase::IgnoreCase)
			|| AssetName.Equals(TEXT("SMG_Scar"), ESearchCase::IgnoreCase);
	}

	bool TMIsTARDataTableMeshSource(const FAssetData& SourceAsset)
	{
		return SourceAsset.GetObjectPathString().Equals(
			TEXT("/Game/MP_System_V3/Game/Weapons/Primary/SMG/Meshes/SMG_TAR.SMG_TAR"),
			ESearchCase::IgnoreCase);
	}

	bool TMIsScarDataTableMeshSource(const FAssetData& SourceAsset)
	{
		return SourceAsset.GetObjectPathString().Equals(
			TEXT("/Game/MP_System_V3/Game/Weapons/Primary/SMG/Meshes/SMG_Scar.SMG_Scar"),
			ESearchCase::IgnoreCase);
	}

	bool TMIsShotgunDataTableMeshSource(const FAssetData& SourceAsset)
	{
		return SourceAsset.GetObjectPathString().Equals(
			TEXT("/Game/MP_System_V3/Game/Weapons/Primary/Shotgun/Meshes/Shotgun.Shotgun"),
			ESearchCase::IgnoreCase);
	}

	bool TMIsKrissDataTableMeshSource(const FAssetData& SourceAsset)
	{
		return SourceAsset.GetObjectPathString().Equals(
			TEXT("/Game/MP_System_V3/Game/Weapons/Primary/SMG/Meshes/SMG_Kriss.SMG_Kriss"),
			ESearchCase::IgnoreCase);
	}

	bool TMIsACWIDataTableMeshSource(const FAssetData& SourceAsset)
	{
		return SourceAsset.GetObjectPathString().Equals(
			TEXT("/Game/MP_System_V3/Game/Weapons/Primary/SMG/Meshes/SMG_ACWI.SMG_ACWI"),
			ESearchCase::IgnoreCase);
	}

	bool TMIsDEDataTableMeshSource(const FAssetData& SourceAsset)
	{
		return SourceAsset.GetObjectPathString().Equals(
			TEXT("/Game/MP_System_V3/Game/Weapons/Secondary/DE/Meshes/DE.DE"),
			ESearchCase::IgnoreCase);
	}

	bool TMIsM9DataTableMeshSource(const FAssetData& SourceAsset)
	{
		return SourceAsset.GetObjectPathString().Equals(
			TEXT("/Game/MP_System_V3/Game/Weapons/Secondary/Pistol/Meshes/M9.M9"),
			ESearchCase::IgnoreCase);
	}

	bool TMIsFragDataTableMeshSource(const FAssetData& SourceAsset)
	{
		return SourceAsset.GetObjectPathString().Equals(
			TEXT("/Game/MP_System_V3/Game/Weapons/Explosives/Frag/Meshes/Frag.Frag"),
			ESearchCase::IgnoreCase);
	}

	bool TMIsWeaponDataTableMeshSourceWithVisualOverride(const FAssetData& SourceAsset)
	{
		return TMIsTARDataTableMeshSource(SourceAsset)
			|| TMIsScarDataTableMeshSource(SourceAsset)
			|| TMIsShotgunDataTableMeshSource(SourceAsset)
			|| TMIsKrissDataTableMeshSource(SourceAsset)
			|| TMIsACWIDataTableMeshSource(SourceAsset)
			|| TMIsFragDataTableMeshSource(SourceAsset);
	}

	bool TMIsAttachmentLikeWeaponVisualMesh(const USkeletalMesh* VisualMesh)
	{
		if (!VisualMesh)
		{
			return true;
		}

		const FString AssetName = VisualMesh->GetName();
		return AssetName.Contains(TEXT("Mag"), ESearchCase::IgnoreCase)
			|| AssetName.Contains(TEXT("Sight"), ESearchCase::IgnoreCase)
			|| AssetName.Contains(TEXT("Laser"), ESearchCase::IgnoreCase)
			|| AssetName.Contains(TEXT("Grip"), ESearchCase::IgnoreCase)
			|| AssetName.Contains(TEXT("Suppressor"), ESearchCase::IgnoreCase)
			|| AssetName.Contains(TEXT("Silencer"), ESearchCase::IgnoreCase)
			|| AssetName.Contains(TEXT("Compensator"), ESearchCase::IgnoreCase);
	}

	UObject* TMResolveWeaponVisualIconSourceObject(
		const TCHAR* WeaponName,
		const TCHAR* WeaponClassPath,
		const TCHAR* ReferencedVisualMeshPath)
	{
		if (!WeaponName || !WeaponClassPath || !ReferencedVisualMeshPath)
		{
			return nullptr;
		}

		UClass* WeaponClass = LoadClass<AActor>(
			nullptr,
			WeaponClassPath);
		AActor* DefaultWeapon = WeaponClass ? Cast<AActor>(WeaponClass->GetDefaultObject()) : nullptr;
		if (!DefaultWeapon)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TMIconGenerator] Failed to load %s while resolving %s visual mesh."), WeaponClassPath, WeaponName);
			return nullptr;
		}

		TArray<USkeletalMeshComponent*> SkeletalMeshComponents;
		DefaultWeapon->GetComponents<USkeletalMeshComponent>(SkeletalMeshComponents);
		for (USkeletalMeshComponent* SkeletalMeshComponent : SkeletalMeshComponents)
		{
			USkeletalMesh* VisualMesh =
				SkeletalMeshComponent ? SkeletalMeshComponent->GetSkeletalMeshAsset() : nullptr;
			if (VisualMesh && !TMIsAttachmentLikeWeaponVisualMesh(VisualMesh))
			{
				return VisualMesh;
			}
		}

		if (UBlueprintGeneratedClass* BlueprintClass = Cast<UBlueprintGeneratedClass>(WeaponClass))
		{
			if (USimpleConstructionScript* ConstructionScript = BlueprintClass->SimpleConstructionScript)
			{
				const TArray<USCS_Node*>& Nodes = ConstructionScript->GetAllNodes();
				for (USCS_Node* Node : Nodes)
				{
					UActorComponent* ComponentTemplate =
						Node ? Node->GetActualComponentTemplate(BlueprintClass) : nullptr;
					USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(ComponentTemplate);
					USkeletalMesh* VisualMesh =
						SkeletalMeshComponent ? SkeletalMeshComponent->GetSkeletalMeshAsset() : nullptr;
					if (VisualMesh && !TMIsAttachmentLikeWeaponVisualMesh(VisualMesh))
					{
						return VisualMesh;
					}
				}
			}
		}

		if (USkeletalMesh* ReferencedVisualMesh = LoadObject<USkeletalMesh>(
			nullptr,
			ReferencedVisualMeshPath))
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMIconGenerator] Using %s referenced visual mesh %s for %s icon generation."),
				WeaponClassPath,
				*ReferencedVisualMesh->GetPathName(),
				WeaponName);
			return ReferencedVisualMesh;
		}

		UE_LOG(LogTemp, Warning, TEXT("[TMIconGenerator] %s has no skeletal visual mesh for %s icon generation."), WeaponClassPath, WeaponName);
		return nullptr;
	}

	UObject* TMFindWeaponVisualIconSourceObject(const FAssetData& SourceAsset)
	{
		if (SourceAsset.GetObjectPathString().Equals(
			TEXT("/Game/MeleeWeapons/Meshes/SK_Kunai_01.SK_Kunai_01"),
			ESearchCase::IgnoreCase))
		{
			return LoadObject<UStaticMesh>(
				nullptr,
				TEXT("/Game/MeleeWeapons/Meshes/SM_Kunai_01.SM_Kunai_01"));
		}

		if (SourceAsset.GetObjectPathString().Equals(
			TEXT("/Game/MeleeWeapons/Meshes/SK_Bayonet_01.SK_Bayonet_01"),
			ESearchCase::IgnoreCase))
		{
			return LoadObject<UStaticMesh>(
				nullptr,
				TEXT("/Game/MeleeWeapons/Meshes/SM_Bayonet_01.SM_Bayonet_01"));
		}

		if (SourceAsset.GetObjectPathString().Equals(
			TEXT("/Game/MeleeWeapons/Meshes/SK_Cleaver_01.SK_Cleaver_01"),
			ESearchCase::IgnoreCase))
		{
			return LoadObject<UStaticMesh>(
				nullptr,
				TEXT("/Game/MeleeWeapons/Meshes/SM_Cleaver_01.SM_Cleaver_01"));
		}

		if (TMIsTARDataTableMeshSource(SourceAsset))
		{
			return TMResolveWeaponVisualIconSourceObject(
				TEXT("TAR"),
				TEXT("/Game/MP_System_V3/Game/Weapons/Primary/TAR/BP_TAR.BP_TAR_C"),
				TEXT("/Game/Modular_AR_Pack/Mesh/T21/SKM_T21.SKM_T21"));
		}

		if (TMIsScarDataTableMeshSource(SourceAsset))
		{
			return TMResolveWeaponVisualIconSourceObject(
				TEXT("Scar"),
				TEXT("/Game/MP_System_V3/Game/Weapons/Primary/Scar/BP_Scar.BP_Scar_C"),
				TEXT("/Game/Modular_AR_Pack/Mesh/SCAL/SKM_SCAL_Complete.SKM_SCAL_Complete"));
		}

		if (TMIsShotgunDataTableMeshSource(SourceAsset))
		{
			return TMResolveWeaponVisualIconSourceObject(
				TEXT("Shotgun"),
				TEXT("/Game/MP_System_V3/Game/Weapons/Primary/Shotgun/BP_Shotgun.BP_Shotgun_C"),
				TEXT("/Game/Weapons/Mesh/Shotgun/SK_M890_Shotgun_Yaxis.SK_M890_Shotgun_Yaxis"));
		}

		if (TMIsKrissDataTableMeshSource(SourceAsset))
		{
			return TMResolveWeaponVisualIconSourceObject(
				TEXT("Kriss"),
				TEXT("/Game/MP_System_V3/Game/Weapons/Primary/Kriss/BP_Kriss.BP_Kriss_C"),
				TEXT("/Game/Weapons/Mesh/SMG/SK_V014_SMG_Yaxis.SK_V014_SMG_Yaxis"));
		}

		if (TMIsACWIDataTableMeshSource(SourceAsset))
		{
			return TMResolveWeaponVisualIconSourceObject(
				TEXT("ACWI"),
				TEXT("/Game/MP_System_V3/Game/Weapons/Primary/ACWI/BP_ACWI.BP_ACWI_C"),
				TEXT("/Game/Weapons/Mesh/Assault/SK_ACWI_Assault_Yaxis.SK_ACWI_Assault_Yaxis"));
		}

		if (TMIsDEDataTableMeshSource(SourceAsset))
		{
			return LoadObject<USkeletalMesh>(
				nullptr,
				TEXT("/Game/Weapons/Mesh/Pistol/SK_DE-42_Pistol_Xaxis.SK_DE-42_Pistol_Xaxis"));
		}

		if (TMIsM9DataTableMeshSource(SourceAsset))
		{
			return LoadObject<USkeletalMesh>(
				nullptr,
				TEXT("/Game/Weapons/Mesh/Pistol/SK_NFP-16_Yaxis.SK_NFP-16_Yaxis"));
		}

		if (TMIsFragDataTableMeshSource(SourceAsset))
		{
			return LoadObject<USkeletalMesh>(
				nullptr,
				TEXT("/Game/Weapons/Mesh/GrenadesAndMine/SK_Frag_Grenade.SK_Frag_Grenade"));
		}

		return nullptr;
	}

	bool TMReadTextureSourceBgra8(UTexture2D* Texture, TArray<uint8>& OutPixels, int32& OutWidth, int32& OutHeight)
	{
		OutPixels.Reset();
		OutWidth = 0;
		OutHeight = 0;

		if (!Texture || !Texture->Source.IsValid())
		{
			return false;
		}

		const int64 SourceWidth = Texture->Source.GetSizeX();
		const int64 SourceHeight = Texture->Source.GetSizeY();
		if (SourceWidth <= 0 || SourceHeight <= 0 || SourceWidth > MAX_int32 || SourceHeight > MAX_int32)
		{
			return false;
		}

		const ETextureSourceFormat SourceFormat = Texture->Source.GetFormat();
		const uint8* SourceData = Texture->Source.LockMipReadOnly(0);
		if (!SourceData)
		{
			return false;
		}

		OutWidth = static_cast<int32>(SourceWidth);
		OutHeight = static_cast<int32>(SourceHeight);
		OutPixels.Init(0, OutWidth * OutHeight * 4);

		if (SourceFormat == TSF_BGRA8 || SourceFormat == TSF_BGRE8)
		{
			FMemory::Memcpy(OutPixels.GetData(), SourceData, OutPixels.Num());
		}
		else if (SourceFormat == TSF_G8)
		{
			for (int32 PixelIndex = 0; PixelIndex < OutWidth * OutHeight; ++PixelIndex)
			{
				const uint8 Value = SourceData[PixelIndex];
				const int32 DestIndex = PixelIndex * 4;
				OutPixels[DestIndex] = Value;
				OutPixels[DestIndex + 1] = Value;
				OutPixels[DestIndex + 2] = Value;
				OutPixels[DestIndex + 3] = 255;
			}
		}
		else if (SourceFormat == TSF_RGBA16)
		{
			const uint16* SourceColorData = reinterpret_cast<const uint16*>(SourceData);
			for (int32 PixelIndex = 0; PixelIndex < OutWidth * OutHeight; ++PixelIndex)
			{
				const int32 SourceIndex = PixelIndex * 4;
				const int32 DestIndex = PixelIndex * 4;
				OutPixels[DestIndex] = static_cast<uint8>(SourceColorData[SourceIndex + 2] >> 8);
				OutPixels[DestIndex + 1] = static_cast<uint8>(SourceColorData[SourceIndex + 1] >> 8);
				OutPixels[DestIndex + 2] = static_cast<uint8>(SourceColorData[SourceIndex] >> 8);
				OutPixels[DestIndex + 3] = static_cast<uint8>(SourceColorData[SourceIndex + 3] >> 8);
			}
		}
		else
		{
			Texture->Source.UnlockMip(0);
			OutPixels.Reset();
			OutWidth = 0;
			OutHeight = 0;
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[TMIconGenerator] Unsupported source texture format %d for %s."),
				static_cast<int32>(SourceFormat),
				*Texture->GetPathName());
			return false;
		}

		Texture->Source.UnlockMip(0);
		return OutPixels.Num() == OutWidth * OutHeight * 4;
	}

	FString TMGetIconAssetName(const FAssetData& SourceAsset, const TCHAR* VariantSuffix)
	{
		return ObjectTools::SanitizeObjectName(
			FString::Printf(TEXT("T_%s_%s"), *SourceAsset.AssetName.ToString(), VariantSuffix));
	}

	FString TMGetObjectPathForPackageAndAsset(const FString& PackageName, const FString& AssetName)
	{
		return FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName);
	}

	bool TMSavePackageForAsset(UPackage* Package, UObject* Asset, const FString& PackageName)
	{
		if (!Package || !Asset)
		{
			return false;
		}

		const FString PackageFilename = FPackageName::LongPackageNameToFilename(
			PackageName,
			FPackageName::GetAssetPackageExtension());

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFilename), true);

		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (PlatformFile.FileExists(*PackageFilename) && PlatformFile.IsReadOnly(*PackageFilename))
		{
			PlatformFile.SetReadOnly(*PackageFilename, false);
		}

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		SaveArgs.bSlowTask = false;

		return UPackage::SavePackage(Package, Asset, *PackageFilename, SaveArgs);
	}

	bool TMSaveIconPreviewPng(const FString& AssetName, const TArray<uint8>& PixelData, const int32 Width, const int32 Height)
	{
		if (Width <= 0 || Height <= 0 || PixelData.Num() != Width * Height * 4)
		{
			return false;
		}

		const FString PreviewDirectory = FPaths::ProjectSavedDir() / TEXT("UIIconPreviews");
		IFileManager::Get().MakeDirectory(*PreviewDirectory, true);

		const FString PreviewFilename = PreviewDirectory / FString::Printf(TEXT("%s.png"), *AssetName);
		const FImageView ImageView(
			const_cast<uint8*>(PixelData.GetData()),
			Width,
			Height,
			ERawImageFormat::BGRA8);
		const bool bSaved = FImageUtils::SaveImageByExtension(*PreviewFilename, ImageView);
		if (bSaved)
		{
			UE_LOG(LogTemp, Display, TEXT("[TMIconGenerator] Saved preview PNG %s."), *PreviewFilename);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[TMIconGenerator] Failed to save preview PNG %s."), *PreviewFilename);
		}

		return bSaved;
	}

	void TMPadTransparentIconColor(TArray<uint8>& PixelData, const int32 Width, const int32 Height)
	{
		if (Width <= 0 || Height <= 0 || PixelData.Num() != Width * Height * 4)
		{
			return;
		}

		TArray<uint8> WorkingPixels = PixelData;
		constexpr int32 PassCount = 8;
		for (int32 Pass = 0; Pass < PassCount; ++Pass)
		{
			TArray<uint8> NextPixels = WorkingPixels;
			bool bChangedAnyPixel = false;
			for (int32 Y = 0; Y < Height; ++Y)
			{
				for (int32 X = 0; X < Width; ++X)
				{
					const int32 DataIndex = ((Y * Width) + X) * 4;
					if (WorkingPixels[DataIndex + 3] > 0)
					{
						continue;
					}

					int32 BlueSum = 0;
					int32 GreenSum = 0;
					int32 RedSum = 0;
					int32 NeighborCount = 0;
					for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
					{
						for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
						{
							if (OffsetX == 0 && OffsetY == 0)
							{
								continue;
							}

							const int32 NeighborX = X + OffsetX;
							const int32 NeighborY = Y + OffsetY;
							if (NeighborX < 0 || NeighborX >= Width || NeighborY < 0 || NeighborY >= Height)
							{
								continue;
							}

							const int32 NeighborIndex = ((NeighborY * Width) + NeighborX) * 4;
							if (WorkingPixels[NeighborIndex + 3] <= 0)
							{
								continue;
							}

							BlueSum += WorkingPixels[NeighborIndex];
							GreenSum += WorkingPixels[NeighborIndex + 1];
							RedSum += WorkingPixels[NeighborIndex + 2];
							++NeighborCount;
						}
					}

					if (NeighborCount <= 0)
					{
						continue;
					}

					NextPixels[DataIndex] = static_cast<uint8>(BlueSum / NeighborCount);
					NextPixels[DataIndex + 1] = static_cast<uint8>(GreenSum / NeighborCount);
					NextPixels[DataIndex + 2] = static_cast<uint8>(RedSum / NeighborCount);
					NextPixels[DataIndex + 3] = 0;
					bChangedAnyPixel = true;
				}
			}

			WorkingPixels = MoveTemp(NextPixels);
			if (!bChangedAnyPixel)
			{
				break;
			}
		}

		for (int32 PixelIndex = 0; PixelIndex < Width * Height; ++PixelIndex)
		{
			const int32 DataIndex = PixelIndex * 4;
			if (PixelData[DataIndex + 3] != 0)
			{
				continue;
			}

			PixelData[DataIndex] = WorkingPixels[DataIndex];
			PixelData[DataIndex + 1] = WorkingPixels[DataIndex + 1];
			PixelData[DataIndex + 2] = WorkingPixels[DataIndex + 2];
		}
	}

	bool TMCreateOrUpdateIconTexture(
		const FAssetData& SourceAsset,
		const TCHAR* VariantSuffix,
		const TArray<uint8>& PixelData,
		const int32 Width,
		const int32 Height)
	{
		if (Width <= 0 || Height <= 0 || PixelData.Num() != Width * Height * 4)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[TMIconGenerator] Invalid thumbnail data for %s. Size=%dx%d Bytes=%d"),
				*SourceAsset.GetObjectPathString(),
				Width,
				Height,
				PixelData.Num());
			return false;
		}

		const FString AssetName = TMGetIconAssetName(SourceAsset, VariantSuffix);
		const FString PackageName = FString(TMIconOutputPath) / AssetName;
		const FString ObjectPath = TMGetObjectPathForPackageAndAsset(PackageName, AssetName);

		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();

		UObject* ExistingObject = StaticFindObject(UObject::StaticClass(), Package, *AssetName);
		if (ExistingObject && !ExistingObject->IsA<UTexture2D>())
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMIconGenerator] Cannot create %s because another non-texture asset already uses that name."),
				*ObjectPath);
			return false;
		}

		UTexture2D* Texture = Cast<UTexture2D>(ExistingObject);
		const bool bCreatedTexture = Texture == nullptr;
		if (!Texture)
		{
			Texture = NewObject<UTexture2D>(
				Package,
				*AssetName,
				RF_Public | RF_Standalone | RF_Transactional);
		}

		TArray<uint8> StoredPixels = PixelData;
		TMPadTransparentIconColor(StoredPixels, Width, Height);

		Texture->Modify();
		Texture->PreEditChange(nullptr);
		Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, StoredPixels.GetData());
		Texture->SRGB = true;
		Texture->CompressionSettings = TC_EditorIcon;
		Texture->MipGenSettings = TMGS_NoMipmaps;
		Texture->LODGroup = TEXTUREGROUP_UI;
		Texture->NeverStream = true;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		Texture->PostEditChange();
		Texture->MarkPackageDirty();

		if (bCreatedTexture)
		{
			FAssetRegistryModule::AssetCreated(Texture);
		}

		const bool bSaved = TMSavePackageForAsset(Package, Texture, PackageName);
		if (bSaved)
		{
			TMSaveIconPreviewPng(AssetName, StoredPixels, Width, Height);
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMIconGenerator] Saved icon %s (%dx%d) from %s."),
				*ObjectPath,
				Width,
				Height,
				*SourceAsset.GetObjectPathString());
		}
		else
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("[TMIconGenerator] Failed to save icon %s from %s."),
				*ObjectPath,
				*SourceAsset.GetObjectPathString());
		}

		return bSaved;
	}
}

namespace
{
	void TMGenerateLoadoutWeaponActiveIconsConsoleCommand()
	{
		FTouchMeEditorModule& TouchMeEditorModule =
			FModuleManager::LoadModuleChecked<FTouchMeEditorModule>(TEXT("TouchMeEditor"));
		TouchMeEditorModule.GenerateLoadoutWeaponActiveIcons();
	}

	FAutoConsoleCommand TMGenerateLoadoutWeaponActiveIconsCommand(
		TEXT("TM.GenerateLoadoutWeaponActiveIcons"),
		TEXT("Generate real-material Icon and Icon_Active textures for loadout weapon row icons."),
		FConsoleCommandDelegate::CreateStatic(&TMGenerateLoadoutWeaponActiveIconsConsoleCommand));
}

void FTouchMeEditorModule::StartupModule()
{
	if (IsRunningCommandlet())
	{
		return;
	}

	FContentBrowserModule& ContentBrowserModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	TArray<FContentBrowserMenuExtender_SelectedAssets>& Extenders =
		ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
	Extenders.Add(FContentBrowserMenuExtender_SelectedAssets::CreateRaw(
		this,
		&FTouchMeEditorModule::OnExtendAssetSelectionMenu));
	ContentBrowserAssetExtenderDelegateHandle = Extenders.Last().GetHandle();
}

void FTouchMeEditorModule::ShutdownModule()
{
	if (!ContentBrowserAssetExtenderDelegateHandle.IsValid()
		|| !FModuleManager::Get().IsModuleLoaded(TEXT("ContentBrowser")))
	{
		return;
	}

	FContentBrowserModule& ContentBrowserModule =
		FModuleManager::GetModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	TArray<FContentBrowserMenuExtender_SelectedAssets>& Extenders =
		ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
	Extenders.RemoveAll([this](const FContentBrowserMenuExtender_SelectedAssets& Delegate)
	{
		return Delegate.GetHandle() == ContentBrowserAssetExtenderDelegateHandle;
	});
}

TSharedRef<FExtender> FTouchMeEditorModule::OnExtendAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets)
{
	TSharedRef<FExtender> Extender = MakeShared<FExtender>();

	const bool bHasSupportedMesh = SelectedAssets.ContainsByPredicate(
		[](const FAssetData& AssetData)
		{
			return IsSupportedMeshAsset(AssetData);
		});

	if (!bHasSupportedMesh)
	{
		return Extender;
	}

	Extender->AddMenuExtension(
		TEXT("GetAssetActions"),
		EExtensionHook::After,
		nullptr,
		FMenuExtensionDelegate::CreateRaw(
			this,
			&FTouchMeEditorModule::BuildAssetSelectionMenu,
			SelectedAssets));

	return Extender;
}

void FTouchMeEditorModule::BuildAssetSelectionMenu(
	FMenuBuilder& MenuBuilder,
	TArray<FAssetData> SelectedAssets) const
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("GenerateUIIcon", "Generate UI Button Icons"),
		LOCTEXT(
			"GenerateUIIconTooltip",
			"Render selected Static Mesh and Skeletal Mesh assets into transparent 512x128 Texture2D UI button icons under /Game/UI/Generated/Icons."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.Texture2D")),
		FUIAction(FExecuteAction::CreateRaw(
			this,
			&FTouchMeEditorModule::GenerateIconsForAssets,
			MoveTemp(SelectedAssets))));
}

void FTouchMeEditorModule::GenerateLoadoutWeaponActiveIcons() const
{
	TArray<FAssetData> SourceAssets = TMCollectLoadoutWeaponMeshAssetsFromDataTable();
	GenerateLoadoutWeaponActiveIconsForAssets(MoveTemp(SourceAssets));
}

void FTouchMeEditorModule::GenerateLoadoutWeaponActiveIconsForAssets(TArray<FAssetData> SourceAssets) const
{
	if (SourceAssets.IsEmpty())
	{
		TMShowNotification(
			LOCTEXT("GeneratedNoLoadoutWeaponIcons", "No loadout weapon meshes were found in DT_Weapons CoreData.Mesh. Check the Output Log."),
			SNotificationItem::CS_Fail);
		return;
	}

	const bool bPreviousMaterialOnly = bTMGenerateLoadoutWeaponMaterialOnly;
	const bool bPreviousActiveOnly = bTMGenerateLoadoutWeaponActiveOnly;
	bTMGenerateLoadoutWeaponMaterialOnly = true;
	bTMGenerateLoadoutWeaponActiveOnly = false;
	GenerateIconsForAssets(SourceAssets);
	bTMGenerateLoadoutWeaponMaterialOnly = false;
	bTMGenerateLoadoutWeaponActiveOnly = true;
	GenerateIconsForAssets(MoveTemp(SourceAssets));
	bTMGenerateLoadoutWeaponMaterialOnly = bPreviousMaterialOnly;
	bTMGenerateLoadoutWeaponActiveOnly = bPreviousActiveOnly;
}

void FTouchMeEditorModule::GenerateIconsForAssets(TArray<FAssetData> SelectedAssets) const
{
	TArray<FAssetData> MeshAssets;
	for (const FAssetData& AssetData : SelectedAssets)
	{
		if (IsSupportedMeshAsset(AssetData))
		{
			MeshAssets.Add(AssetData);
		}
	}

	if (MeshAssets.IsEmpty())
	{
		TMShowNotification(
			LOCTEXT("NoSupportedMeshes", "Select a Static Mesh or Skeletal Mesh first."),
			SNotificationItem::CS_Fail);
		return;
	}

	FScopedSlowTask Progress(
		static_cast<float>(MeshAssets.Num()),
		LOCTEXT("GenerateUIIconsProgress", "Generating UI button icons..."));
	Progress.MakeDialog(false);

	int32 SavedCount = 0;
	for (const FAssetData& AssetData : MeshAssets)
	{
		Progress.EnterProgressFrame(1.0f, FText::FromName(AssetData.AssetName));

		UObject* SourceObject = AssetData.GetAsset();
		if (!SourceObject)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TMIconGenerator] Failed to load %s."), *AssetData.GetObjectPathString());
			continue;
		}

		UObject* RenderSourceObject = SourceObject;
		if (UObject* VisualIconSourceObject = TMFindWeaponVisualIconSourceObject(AssetData))
		{
			RenderSourceObject = VisualIconSourceObject;
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMIconGenerator] Using weapon visual mesh %s while saving icon for %s."),
				*RenderSourceObject->GetPathName(),
				*AssetData.GetObjectPathString());
		}

		TArray<uint8> NormalPixels;
		bool bBuiltNormalPixels = false;
		bool bUsedPreferredSourceIcon = false;
		const TCHAR* IconVariantSuffix = TEXT("Icon");
		const bool bLoadoutMaterialVariantMode =
			bTMGenerateLoadoutWeaponMaterialOnly || bTMGenerateLoadoutWeaponActiveOnly;
		if (bTMGenerateLoadoutWeaponActiveOnly)
		{
			if (!TMBuildLoadoutWeaponMaterialIconPixels(AssetData, RenderSourceObject, true, NormalPixels))
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[TMIconGenerator] Failed to build active real-material icon pixels for %s; falling back to mesh render."),
					*AssetData.GetObjectPathString());
			}
			else
			{
				if (TMCreateOrUpdateIconTexture(AssetData, TEXT("Icon_Active"), NormalPixels, TMIconWidth, TMIconHeight))
				{
					++SavedCount;
				}
				continue;
			}

			IconVariantSuffix = TEXT("Icon_Active");
		}

		if (bTMGenerateLoadoutWeaponMaterialOnly)
		{
			if (!TMBuildLoadoutWeaponMaterialIconPixels(AssetData, RenderSourceObject, false, NormalPixels))
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[TMIconGenerator] Failed to build inactive real-material icon pixels for %s; falling back to mesh render."),
					*AssetData.GetObjectPathString());
			}
			else
			{
				if (TMCreateOrUpdateIconTexture(AssetData, TEXT("Icon"), NormalPixels, TMIconWidth, TMIconHeight))
				{
					++SavedCount;
				}
				continue;
			}
		}

		if (UTexture2D* PreferredSourceIcon = TMFindPreferredSourceIconTexture(AssetData))
		{
			TArray<uint8> RenderPixels;
			int32 RenderWidth = 0;
			int32 RenderHeight = 0;
			if (TMReadTextureSourceBgra8(PreferredSourceIcon, RenderPixels, RenderWidth, RenderHeight))
			{
				bUsedPreferredSourceIcon = true;
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMIconGenerator] Using source UI texture %s for %s."),
					*PreferredSourceIcon->GetPathName(),
					*AssetData.GetObjectPathString());
				if (AssetData.AssetName.ToString().Equals(TEXT("SMG_ACWI"), ESearchCase::IgnoreCase))
				{
					bBuiltNormalPixels = TMBuildTransformedButtonIconPixels(
						RenderPixels,
						RenderWidth,
						RenderHeight,
						16.0f,
						0.98f,
						0.94f,
						1.60f,
						NormalPixels);
				}

				if (!bBuiltNormalPixels)
				{
					bBuiltNormalPixels = TMBuildButtonIconPixels(RenderPixels, RenderWidth, RenderHeight, NormalPixels);
				}
			}
			else
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[TMIconGenerator] Failed to read source UI texture %s for %s; using mesh thumbnail path."),
					*PreferredSourceIcon->GetPathName(),
					*AssetData.GetObjectPathString());
			}
		}

		if (!bBuiltNormalPixels && !bUsedPreferredSourceIcon)
		{
			TArray<uint8> ProjectedMeshPixels;
			if (TMIsWeaponDataTableMeshSourceWithVisualOverride(AssetData)
				&& TMBuildSkeletalMeshProjectedIconPixels(Cast<USkeletalMesh>(RenderSourceObject), ProjectedMeshPixels))
			{
				NormalPixels = MoveTemp(ProjectedMeshPixels);
				bBuiltNormalPixels = true;
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMIconGenerator] Applied primary weapon material projection pass for %s."),
					*AssetData.GetObjectPathString());
			}
		}

		if (!bBuiltNormalPixels && !bUsedPreferredSourceIcon)
		{
			TArray<uint8> RenderPixels;
			int32 RenderWidth = 0;
			int32 RenderHeight = 0;
			if (TMShouldUseSceneCaptureMeshRender(AssetData))
			{
				if (TMRenderStaticMeshSceneCapturePixels(Cast<UStaticMesh>(RenderSourceObject), RenderPixels, RenderWidth, RenderHeight))
				{
					const bool bSceneFitted = TMBuildButtonIconPixelsFromAlpha(RenderPixels, RenderWidth, RenderHeight, NormalPixels)
						|| TMBuildButtonIconPixels(RenderPixels, RenderWidth, RenderHeight, NormalPixels);
					if (bSceneFitted)
					{
						TMNormalizeSceneCaptureIconExposure(NormalPixels, TMIconWidth, TMIconHeight);
					}
					const bool bSceneReadable = bSceneFitted
						&& (TMIconHasReadableColor(NormalPixels, TMIconWidth, TMIconHeight)
							|| TMShouldKeepRealMaterialStaticAttachmentIcon(AssetData));
					if (bSceneReadable)
					{
						bBuiltNormalPixels = true;
						UE_LOG(
							LogTemp,
							Display,
							TEXT("[TMIconGenerator] Using scene-captured mesh render for %s."),
							*AssetData.GetObjectPathString());
					}
				}
			}
		}

		if (!bBuiltNormalPixels && !bUsedPreferredSourceIcon)
		{
			if (TMShouldUseSkeletalMeshFallbackRender(AssetData)
				&& TMBuildSkeletalSceneCaptureIconPixels(
					Cast<USkeletalMesh>(RenderSourceObject),
					false,
					NormalPixels))
			{
				bBuiltNormalPixels = true;
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMIconGenerator] Using scene-captured skeletal mesh render for %s."),
					*AssetData.GetObjectPathString());
			}
		}

		if (!bBuiltNormalPixels && !bUsedPreferredSourceIcon)
		{
			if (TMShouldUseSkeletalMeshFallbackRender(AssetData)
				&& TMBuildSkeletalSceneCaptureIconPixels(Cast<USkeletalMesh>(RenderSourceObject), true, NormalPixels))
			{
				bBuiltNormalPixels = true;
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMIconGenerator] Using readable skeletal mesh render for %s."),
					*AssetData.GetObjectPathString());
			}
		}

		if (!bBuiltNormalPixels && !bUsedPreferredSourceIcon)
		{
			TArray<uint8> RenderPixels;
			int32 RenderWidth = 0;
			int32 RenderHeight = 0;
			if (TMRenderMeshThumbnailPixels(RenderSourceObject, RenderPixels, RenderWidth, RenderHeight, false)
				&& TMBuildButtonIconPixels(RenderPixels, RenderWidth, RenderHeight, NormalPixels))
			{
				if (TMIsWeaponDataTableMeshSourceWithVisualOverride(AssetData))
				{
					TMNormalizeSceneCaptureIconExposure(NormalPixels, TMIconWidth, TMIconHeight);
					bBuiltNormalPixels = true;
					UE_LOG(
						LogTemp,
						Display,
						TEXT("[TMIconGenerator] Using weapon visual mesh thumbnail for %s."),
						*AssetData.GetObjectPathString());
				}
				else if (TMIconHasReadableColor(NormalPixels, TMIconWidth, TMIconHeight))
				{
					bBuiltNormalPixels = true;
					UE_LOG(
						LogTemp,
						Display,
						TEXT("[TMIconGenerator] Using original mesh thumbnail for %s."),
						*AssetData.GetObjectPathString());
				}
			}
		}

		if (!bBuiltNormalPixels && !bUsedPreferredSourceIcon)
		{
			TArray<uint8> ProjectedMeshPixels;
			if (TMShouldUseSkeletalMeshFallbackRender(AssetData)
				&& TMBuildSkeletalMeshProjectedIconPixels(Cast<USkeletalMesh>(RenderSourceObject), ProjectedMeshPixels))
			{
				NormalPixels = MoveTemp(ProjectedMeshPixels);
				bBuiltNormalPixels = true;
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMIconGenerator] Applied skeletal geometry projection pass for %s."),
					*AssetData.GetObjectPathString());
			}
		}

		if (!bBuiltNormalPixels && !bUsedPreferredSourceIcon)
		{
			TArray<uint8> ProjectedMeshPixels;
			if (TMBuildStaticMeshProjectedIconPixels(Cast<UStaticMesh>(RenderSourceObject), ProjectedMeshPixels))
			{
				NormalPixels = MoveTemp(ProjectedMeshPixels);
				bBuiltNormalPixels = true;
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMIconGenerator] Applied geometry projection pass for dark mesh thumbnail %s."),
					*AssetData.GetObjectPathString());
			}
			else
			{
				TArray<uint8> RenderPixels;
				int32 RenderWidth = 0;
				int32 RenderHeight = 0;
				if (TMRenderMeshThumbnailPixels(RenderSourceObject, RenderPixels, RenderWidth, RenderHeight, true)
					&& TMBuildButtonIconPixels(RenderPixels, RenderWidth, RenderHeight, NormalPixels))
				{
					TMApplyReadableTint(NormalPixels, TMIconWidth, TMIconHeight);
					bBuiltNormalPixels = true;
				}
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMIconGenerator] Applied readable tint pass for dark mesh thumbnail %s."),
					*AssetData.GetObjectPathString());
			}
		}

		if (!bBuiltNormalPixels)
		{
			UE_LOG(LogTemp, Warning, TEXT("[TMIconGenerator] Failed to build icon pixels for %s."), *AssetData.GetObjectPathString());
			continue;
		}

		if (TMIsWeaponDataTableMeshSourceWithVisualOverride(AssetData))
		{
			TMCleanLoadoutWeaponIconAlpha(NormalPixels, TMIconWidth, TMIconHeight);
			TMApplyLoadoutWeaponRowIconAlignment(AssetData, NormalPixels);
			TMApplyLoadoutWeaponMaterialIconTone(
				NormalPixels,
				TMIconWidth,
				TMIconHeight,
				bTMGenerateLoadoutWeaponActiveOnly);
		}
		else if (bLoadoutMaterialVariantMode)
		{
			TMCleanLoadoutWeaponIconAlpha(NormalPixels, TMIconWidth, TMIconHeight);
			TMApplyLoadoutWeaponMaterialIconTone(
				NormalPixels,
				TMIconWidth,
				TMIconHeight,
				bTMGenerateLoadoutWeaponActiveOnly);
		}

		if (TMCreateOrUpdateIconTexture(AssetData, IconVariantSuffix, NormalPixels, TMIconWidth, TMIconHeight))
		{
			++SavedCount;
		}
	}

	if (SavedCount > 0)
	{
		TMShowNotification(
			FText::Format(
				LOCTEXT("GeneratedUIIcons", "Generated {0} UI button icon texture(s) in /Game/UI/Generated/Icons."),
				FText::AsNumber(SavedCount)),
			SNotificationItem::CS_Success);
	}
	else
	{
		TMShowNotification(
			LOCTEXT("GeneratedNoUIIcons", "No UI icons were generated. Check the Output Log."),
			SNotificationItem::CS_Fail);
	}
}

bool FTouchMeEditorModule::IsSupportedMeshAsset(const FAssetData& AssetData)
{
	return AssetData.AssetClassPath == UStaticMesh::StaticClass()->GetClassPathName()
		|| AssetData.AssetClassPath == USkeletalMesh::StaticClass()->GetClassPathName();
}

IMPLEMENT_MODULE(FTouchMeEditorModule, TouchMeEditor)

#undef LOCTEXT_NAMESPACE
