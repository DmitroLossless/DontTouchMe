#include "TouchMeEditor.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/Engine.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/FileManager.h"
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
#include "Styling/AppStyle.h"
#include "ThumbnailRendering/SceneThumbnailInfo.h"
#include "UObject/SavePackage.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

#define LOCTEXT_NAMESPACE "TouchMeEditor"

namespace
{
	constexpr int32 TMIconWidth = 512;
	constexpr int32 TMIconHeight = 128;
	constexpr int32 TMThumbnailRenderSize = 1024;
	const TCHAR* TMIconOutputPath = TEXT("/Game/UI/Generated/Icons");

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

	uint8 TMQuantizeChannel(const float Value, const int32 Levels)
	{
		const float MaxLevel = static_cast<float>(FMath::Max(Levels - 1, 1));
		const float Quantized = FMath::RoundToFloat(FMath::Clamp(Value, 0.0f, 1.0f) * MaxLevel) / MaxLevel;
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Quantized * 255.0f), 0, 255));
	}

	void TMApplyComicButtonStyle(TArray<uint8>& PixelData, const int32 Width, const int32 Height)
	{
		if (Width <= 0 || Height <= 0 || PixelData.Num() != Width * Height * 4)
		{
			return;
		}

		const TArray<uint8> SourcePixels = PixelData;
		TArray<float> EdgeMask;
		EdgeMask.Init(0.0f, Width * Height);

		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 PixelIndex = ((Y * Width) + X) * 4;
				const float Alpha = static_cast<float>(SourcePixels[PixelIndex + 3]) / 255.0f;
				if (Alpha <= 0.02f)
				{
					continue;
				}

				const float AlphaX = FMath::Abs(TMGetPixelAlpha(SourcePixels, Width, Height, X - 1, Y)
					- TMGetPixelAlpha(SourcePixels, Width, Height, X + 1, Y));
				const float AlphaY = FMath::Abs(TMGetPixelAlpha(SourcePixels, Width, Height, X, Y - 1)
					- TMGetPixelAlpha(SourcePixels, Width, Height, X, Y + 1));
				const float LumaX = FMath::Abs(TMGetPixelLuminance(SourcePixels, Width, Height, X - 1, Y)
					- TMGetPixelLuminance(SourcePixels, Width, Height, X + 1, Y));
				const float LumaY = FMath::Abs(TMGetPixelLuminance(SourcePixels, Width, Height, X, Y - 1)
					- TMGetPixelLuminance(SourcePixels, Width, Height, X, Y + 1));
				const float LumaEdge = FMath::Clamp(((LumaX + LumaY) - 0.16f) / 0.22f, 0.0f, 1.0f);

				EdgeMask[(Y * Width) + X] = FMath::Clamp(
					((AlphaX + AlphaY) * 2.25f) + (LumaEdge * Alpha * 0.22f),
					0.0f,
					1.0f);
			}
		}

		PixelData.Init(0, Width * Height * 4);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				float PaintStrength = 0.0f;
				for (int32 OffsetY = -3; OffsetY <= 3; ++OffsetY)
				{
					const int32 SourceY = FMath::Clamp(Y + OffsetY, 0, Height - 1);
					for (int32 OffsetX = -3; OffsetX <= 3; ++OffsetX)
					{
						const int32 SourceX = FMath::Clamp(X + OffsetX, 0, Width - 1);
						const float Distance = FMath::Sqrt(static_cast<float>((OffsetX * OffsetX) + (OffsetY * OffsetY)));
						const float Falloff = FMath::Clamp(1.0f - (Distance / 3.75f), 0.0f, 1.0f);
						PaintStrength = FMath::Max(PaintStrength, EdgeMask[(SourceY * Width) + SourceX] * Falloff);
					}
				}

				if (PaintStrength <= 0.06f)
				{
					continue;
				}

				const float Noise = TMHashToUnitFloat(
					(static_cast<uint32>(X) * 1973u)
					^ (static_cast<uint32>(Y) * 9277u)
					^ 0x8F15C3u);
				const float DryBrush = Noise < 0.11f ? 0.42f : (0.74f + Noise * 0.26f);
				const float StrokeAlpha = FMath::Clamp(PaintStrength * DryBrush * 1.45f, 0.0f, 1.0f);
				const int32 PixelIndex = ((Y * Width) + X) * 4;
				PixelData[PixelIndex] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(228.0f + Noise * 20.0f), 0, 255));
				PixelData[PixelIndex + 1] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(238.0f + Noise * 14.0f), 0, 255));
				PixelData[PixelIndex + 2] = 255;
				PixelData[PixelIndex + 3] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(StrokeAlpha * 255.0f), 0, 255));
			}
		}
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
			ReadableMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.86f, 0.88f, 0.90f, 1.0f));
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

	UTexture2D* TMFindPreferredSourceIconTexture(const FAssetData& SourceAsset)
	{
		const FString SourceAssetName = SourceAsset.AssetName.ToString();
		if (SourceAssetName.Contains(TEXT("Holographic"), ESearchCase::IgnoreCase))
		{
			return LoadObject<UTexture2D>(
				nullptr,
				TEXT("/Game/AdvanceWeaponPack/Texture/UI/T_Holographic_Icon.T_Holographic_Icon"));
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

		Texture->Modify();
		Texture->PreEditChange(nullptr);
		Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, PixelData.GetData());
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
			TMSaveIconPreviewPng(AssetName, PixelData, Width, Height);
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
			"Render selected Static Mesh and Skeletal Mesh assets into transparent normal and white paint contour 512x128 Texture2D UI button icons under /Game/UI/Generated/Icons."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.Texture2D")),
		FUIAction(FExecuteAction::CreateRaw(
			this,
			&FTouchMeEditorModule::GenerateIconsForAssets,
			MoveTemp(SelectedAssets))));
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

		TArray<uint8> RenderPixels;
		int32 RenderWidth = 0;
		int32 RenderHeight = 0;
		if (UTexture2D* PreferredSourceIcon = TMFindPreferredSourceIconTexture(AssetData))
		{
			if (TMReadTextureSourceBgra8(PreferredSourceIcon, RenderPixels, RenderWidth, RenderHeight))
			{
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMIconGenerator] Using source UI texture %s for %s."),
					*PreferredSourceIcon->GetPathName(),
					*AssetData.GetObjectPathString());
			}
			else
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[TMIconGenerator] Failed to read source UI texture %s for %s; falling back to mesh thumbnail."),
					*PreferredSourceIcon->GetPathName(),
					*AssetData.GetObjectPathString());
			}
		}

		if (RenderPixels.IsEmpty()
			&& !TMRenderMeshThumbnailPixels(SourceObject, RenderPixels, RenderWidth, RenderHeight, true))
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[TMIconGenerator] Invalid thumbnail data for %s. Size=%dx%d Bytes=%d"),
				*AssetData.GetObjectPathString(),
				RenderWidth,
				RenderHeight,
				RenderPixels.Num());
			continue;
		}

		TArray<uint8> NormalPixels;
		if (!TMBuildButtonIconPixels(RenderPixels, RenderWidth, RenderHeight, NormalPixels))
		{
			UE_LOG(LogTemp, Warning, TEXT("[TMIconGenerator] Failed to fit icon pixels for %s."), *AssetData.GetObjectPathString());
			continue;
		}

		if (TMCreateOrUpdateIconTexture(AssetData, TEXT("Icon"), NormalPixels, TMIconWidth, TMIconHeight))
		{
			++SavedCount;
		}

		TArray<uint8> ComicPixels = NormalPixels;
		TMApplyComicButtonStyle(ComicPixels, TMIconWidth, TMIconHeight);
		if (TMCreateOrUpdateIconTexture(AssetData, TEXT("ComicIcon"), ComicPixels, TMIconWidth, TMIconHeight))
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
