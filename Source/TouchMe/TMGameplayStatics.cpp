// Copyright Epic Games, Inc. All Rights Reserved.

#include "TMGameplayStatics.h"
#include "TouchMe.h"
#include "Gun/Gun.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/OverlapResult.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "EngineLogs.h"
#include "Misc/PackageName.h"
#include "Kismet/GameplayStaticsTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/EngineVersion.h"
#include "NiagaraCommon.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/KismetSystemLibrary.h"
#include "SceneView.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ContentWidget.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Containers/Ticker.h"
#include "Components/PrimitiveComponent.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Math/InverseRotationMatrix.h"
#include "UObject/Package.h"
#include "Engine/Texture2D.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "ParticleHelper.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "AudioDevice.h"
#include "SaveGameSystem.h"
#include "DVRStreaming.h"
#include "PlatformFeatures.h"
#include "GameFramework/Character.h"
#include "InputCoreTypes.h"
#include "Sound/DialogueWave.h"
#include "GameFramework/SaveGame.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Components/DecalComponent.h"
#include "Components/ForceFeedbackComponent.h"
#include "Logging/MessageLog.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "PhysicsEngine/BodySetup.h"
#include "Misc/EngineVersion.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Sound/SoundCue.h"
#include "Audio/ActorSoundParameterInterface.h"
#include "Audio/AudioTraceUtil.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "TimerManager.h"
#include "TMFoliageCollisionPushTester.h"
#include "TMFoliageExplosionCollisionTester.h"
#include "TMFoliageImpulseSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/TMWeaponIconResolver.h"

#if WITH_EDITOR
#include "AnimGraphNode_CopyBone.h"
#include "AnimGraphNode_Fabrik.h"
#include "AnimGraphNode_ModifyBone.h"
#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/SavePackage.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(TMGameplayStatics)

#if WITH_ACCESSIBILITY
#include "Framework/Application/SlateApplication.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Accessibility/SlateAccessibleMessageHandler.h"
#endif

#define LOCTEXT_NAMESPACE "TMGameplayStatics"

namespace TMGameplayStatics
{
	constexpr float LoadoutWeaponLayerIconWidth = 288.0f;
	constexpr float LoadoutWeaponLayerIconHeight = 72.0f;
	constexpr float LoadoutWeaponLayerIconHoverScale = 1.07f;
	constexpr float LoadoutGearShimmerPeriod = 7.5f;
	constexpr float LoadoutGearShimmerDuration = 0.55f;
	constexpr float LoadoutGearShimmerFirstDelay = 1.5f;
	constexpr float LoadoutReturnShimmerPeriod = 15.0f;
	constexpr float LoadoutReturnShimmerDuration = 0.55f;
	constexpr float LoadoutReturnShimmerFirstDelay = 3.0f;
	const FLinearColor LoadoutReturnNormalTint(0.090842f, 0.001214f, 0.002125f, 0.901961f);

	struct FLoadoutWeaponLayerIconHoverState
	{
		TWeakObjectPtr<UButton> Button;
		TWeakObjectPtr<UImage> IconImage;
		bool bHovered = false;
	};

	TArray<FLoadoutWeaponLayerIconHoverState> LoadoutWeaponLayerIconHoverStates;
	FTSTicker::FDelegateHandle LoadoutWeaponLayerIconHoverTickerHandle;

	struct FLoadoutGearShimmerState
	{
		TWeakObjectPtr<UButton> Button;
		FButtonStyle BaseStyle;
		FLinearColor BaseColorAndOpacity = FLinearColor::White;
		FLinearColor BaseBackgroundColor = FLinearColor::White;
		float Period = LoadoutGearShimmerPeriod;
		float Duration = LoadoutGearShimmerDuration;
		float Elapsed = 0.0f;
		float PreviousIntensity = 0.0f;
	};

	TArray<FLoadoutGearShimmerState> LoadoutGearShimmerStates;
	FTSTicker::FDelegateHandle LoadoutGearShimmerTickerHandle;

	struct FLoadoutImageShimmerState
	{
		TWeakObjectPtr<UImage> Image;
		TWeakObjectPtr<UButton> HoverButton;
		FSlateBrush BaseBrush;
		FLinearColor BaseColorAndOpacity = FLinearColor::White;
		float Period = LoadoutReturnShimmerPeriod;
		float Duration = LoadoutReturnShimmerDuration;
		float Elapsed = 0.0f;
		float PreviousIntensity = 0.0f;
		bool bPreviousHovered = false;
	};

	struct FLoadoutDeferredImageShimmerRegistration
	{
		TWeakObjectPtr<UUserWidget> OwnerWidget;
		FName ImageName;
		FName HoverButtonName;
		float Period = LoadoutReturnShimmerPeriod;
		float Duration = LoadoutReturnShimmerDuration;
		float FirstDelay = LoadoutReturnShimmerFirstDelay;
		float DelayRemaining = 0.2f;
	};

	TArray<FLoadoutImageShimmerState> LoadoutImageShimmerStates;
	TArray<FLoadoutDeferredImageShimmerRegistration> LoadoutDeferredImageShimmerRegistrations;

	void ApplyLoadoutWeaponLayerIconHoverVisual(UImage* IconImage, const bool bHovered)
	{
		if (!IconImage)
		{
			return;
		}

		IconImage->SetRenderTransformPivot(FVector2D(0.0f, 0.5f));
		IconImage->SetRenderScale(bHovered
			? FVector2D(LoadoutWeaponLayerIconHoverScale, LoadoutWeaponLayerIconHoverScale)
			: FVector2D(1.0f, 1.0f));
	}

	bool TickLoadoutWeaponLayerIconHover(const float DeltaTime)
	{
		for (int32 Index = LoadoutWeaponLayerIconHoverStates.Num() - 1; Index >= 0; --Index)
		{
			FLoadoutWeaponLayerIconHoverState& State = LoadoutWeaponLayerIconHoverStates[Index];
			UButton* Button = State.Button.Get();
			UImage* IconImage = State.IconImage.Get();
			if (!Button || !IconImage)
			{
				LoadoutWeaponLayerIconHoverStates.RemoveAtSwap(Index);
				continue;
			}

			const bool bHovered = Button->IsHovered();
			if (State.bHovered != bHovered)
			{
				State.bHovered = bHovered;
				ApplyLoadoutWeaponLayerIconHoverVisual(IconImage, bHovered);
			}
		}

		if (LoadoutWeaponLayerIconHoverStates.Num() == 0)
		{
			LoadoutWeaponLayerIconHoverTickerHandle.Reset();
			return false;
		}

		return true;
	}

	void RegisterLoadoutWeaponLayerIconHover(UButton* Button, UImage* IconImage)
	{
		if (!Button || !IconImage)
		{
			return;
		}

		for (FLoadoutWeaponLayerIconHoverState& State : LoadoutWeaponLayerIconHoverStates)
		{
			if (State.Button.Get() == Button)
			{
				State.IconImage = IconImage;
				State.bHovered = Button->IsHovered();
				ApplyLoadoutWeaponLayerIconHoverVisual(IconImage, State.bHovered);
				return;
			}
		}

		FLoadoutWeaponLayerIconHoverState& State = LoadoutWeaponLayerIconHoverStates.AddDefaulted_GetRef();
		State.Button = Button;
		State.IconImage = IconImage;
		State.bHovered = Button->IsHovered();
		ApplyLoadoutWeaponLayerIconHoverVisual(IconImage, State.bHovered);

		if (!LoadoutWeaponLayerIconHoverTickerHandle.IsValid())
		{
			LoadoutWeaponLayerIconHoverTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateStatic(&TickLoadoutWeaponLayerIconHover));
		}
	}

	FLinearColor BlendLoadoutGearShimmerTint(const FLinearColor& BaseTint, const float Intensity)
	{
		const FLinearColor HighlightTint = FLinearColor::FromSRGBColor(FColor(255, 246, 216, 255));
		const float BlendAlpha = FMath::Clamp(Intensity, 0.0f, 1.0f);
		FLinearColor Result = FMath::Lerp(BaseTint, HighlightTint, BlendAlpha);
		Result.A = FMath::Lerp(BaseTint.A, 1.0f, BlendAlpha);
		return Result;
	}

	void ApplyLoadoutGearShimmerVisual(
		UButton* Button,
		const FButtonStyle& BaseStyle,
		const FLinearColor& BaseColorAndOpacity,
		const FLinearColor& BaseBackgroundColor,
		const float Intensity)
	{
		if (!Button)
		{
			return;
		}

		FButtonStyle Style = BaseStyle;
		Style.Normal.TintColor = FSlateColor(BlendLoadoutGearShimmerTint(
			BaseStyle.Normal.TintColor.GetSpecifiedColor(),
			Intensity));
		Style.Hovered.TintColor = FSlateColor(BlendLoadoutGearShimmerTint(
			BaseStyle.Hovered.TintColor.GetSpecifiedColor(),
			Intensity));
		Style.Pressed.TintColor = FSlateColor(BlendLoadoutGearShimmerTint(
			BaseStyle.Pressed.TintColor.GetSpecifiedColor(),
			Intensity));
		Style.Disabled.TintColor = FSlateColor(BlendLoadoutGearShimmerTint(
			BaseStyle.Disabled.TintColor.GetSpecifiedColor(),
			Intensity));

		Button->SetStyle(Style);
		Button->SetColorAndOpacity(BlendLoadoutGearShimmerTint(BaseColorAndOpacity, Intensity * 0.35f));
		Button->SetBackgroundColor(BlendLoadoutGearShimmerTint(BaseBackgroundColor, Intensity * 0.18f));
		Button->SynchronizeProperties();
	}

	void ApplyLoadoutImageShimmerVisual(
		UImage* Image,
		const FSlateBrush& BaseBrush,
		const FLinearColor& BaseColorAndOpacity,
		const float Intensity,
		const bool bHovered)
	{
		if (!Image)
		{
			return;
		}

		FSlateBrush Brush = BaseBrush;
		if (bHovered)
		{
			Brush.TintColor = FSlateColor(FLinearColor::White);
			Image->SetBrush(Brush);
			Image->SetColorAndOpacity(FLinearColor::White);
			return;
		}

		Brush.TintColor = FSlateColor(BlendLoadoutGearShimmerTint(
			BaseBrush.TintColor.GetSpecifiedColor(),
			Intensity));
		Image->SetBrush(Brush);
		Image->SetColorAndOpacity(BlendLoadoutGearShimmerTint(BaseColorAndOpacity, Intensity * 0.35f));
	}

	bool IsLoadoutReturnImageStillUsingEditorPlaceholder(const UImage* Image)
	{
		if (!Image)
		{
			return false;
		}

		const FLinearColor BrushTint = Image->GetBrush().TintColor.GetSpecifiedColor();
		const FLinearColor ColorAndOpacity = Image->GetColorAndOpacity();
		const bool bMagentaBrush = BrushTint.R > 0.9f && BrushTint.G < 0.2f && BrushTint.B > 0.9f;
		const bool bWhiteColor = ColorAndOpacity.R > 0.9f
			&& ColorAndOpacity.G > 0.9f
			&& ColorAndOpacity.B > 0.9f;
		return bMagentaBrush && bWhiteColor;
	}

	void RegisterLoadoutImageShimmer(
		UImage* Image,
		UButton* HoverButton,
		const float Period,
		const float Duration,
		const float FirstDelay);

	bool TickLoadoutGearShimmer(const float DeltaTime)
	{
		for (int32 Index = LoadoutDeferredImageShimmerRegistrations.Num() - 1; Index >= 0; --Index)
		{
			FLoadoutDeferredImageShimmerRegistration& State = LoadoutDeferredImageShimmerRegistrations[Index];
			UUserWidget* OwnerWidget = State.OwnerWidget.Get();
			if (!OwnerWidget)
			{
				LoadoutDeferredImageShimmerRegistrations.RemoveAtSwap(Index);
				continue;
			}

			State.DelayRemaining -= DeltaTime;
			if (State.DelayRemaining > 0.0f)
			{
				continue;
			}

			UImage* Image = Cast<UImage>(OwnerWidget->GetWidgetFromName(State.ImageName));
			if (!Image)
			{
				LoadoutDeferredImageShimmerRegistrations.RemoveAtSwap(Index);
				continue;
			}

			UButton* HoverButton = State.HoverButtonName.IsNone()
				? nullptr
				: Cast<UButton>(OwnerWidget->GetWidgetFromName(State.HoverButtonName));
			RegisterLoadoutImageShimmer(Image, HoverButton, State.Period, State.Duration, State.FirstDelay);
			LoadoutDeferredImageShimmerRegistrations.RemoveAtSwap(Index);
		}

		for (int32 Index = LoadoutGearShimmerStates.Num() - 1; Index >= 0; --Index)
		{
			FLoadoutGearShimmerState& State = LoadoutGearShimmerStates[Index];
			UButton* Button = State.Button.Get();
			if (!Button)
			{
				LoadoutGearShimmerStates.RemoveAtSwap(Index);
				continue;
			}

			State.Elapsed = FMath::Fmod(State.Elapsed + DeltaTime, State.Period);
			const bool bInPulse = State.Elapsed <= State.Duration;
			const float PulseT = bInPulse
				? FMath::Clamp(State.Elapsed / State.Duration, 0.0f, 1.0f)
				: 0.0f;
			const float Intensity = bInPulse ? FMath::Sin(PulseT * PI) : 0.0f;
			if (Intensity > 0.001f || State.PreviousIntensity > 0.001f)
			{
				ApplyLoadoutGearShimmerVisual(
					Button,
					State.BaseStyle,
					State.BaseColorAndOpacity,
					State.BaseBackgroundColor,
					Intensity);
			}

			State.PreviousIntensity = Intensity;
		}

		for (int32 Index = LoadoutImageShimmerStates.Num() - 1; Index >= 0; --Index)
		{
			FLoadoutImageShimmerState& State = LoadoutImageShimmerStates[Index];
			UImage* Image = State.Image.Get();
			if (!Image)
			{
				LoadoutImageShimmerStates.RemoveAtSwap(Index);
				continue;
			}

			State.Elapsed = FMath::Fmod(State.Elapsed + DeltaTime, State.Period);
			const bool bInPulse = State.Elapsed <= State.Duration;
			const float PulseT = bInPulse
				? FMath::Clamp(State.Elapsed / State.Duration, 0.0f, 1.0f)
				: 0.0f;
			const float Intensity = bInPulse ? FMath::Sin(PulseT * PI) : 0.0f;
			const bool bHovered = State.HoverButton.IsValid() && State.HoverButton->IsHovered();
			if (bHovered
				|| State.bPreviousHovered
				|| Intensity > 0.001f
				|| State.PreviousIntensity > 0.001f)
			{
				ApplyLoadoutImageShimmerVisual(
					Image,
					State.BaseBrush,
					State.BaseColorAndOpacity,
					Intensity,
					bHovered);
			}

			State.PreviousIntensity = Intensity;
			State.bPreviousHovered = bHovered;
		}

		if (LoadoutGearShimmerStates.Num() == 0
			&& LoadoutImageShimmerStates.Num() == 0
			&& LoadoutDeferredImageShimmerRegistrations.Num() == 0)
		{
			LoadoutGearShimmerTickerHandle.Reset();
			return false;
		}

		return true;
	}

	void RegisterLoadoutGearShimmer(
		UButton* Button,
		const float Period = LoadoutGearShimmerPeriod,
		const float Duration = LoadoutGearShimmerDuration,
		const float FirstDelay = LoadoutGearShimmerFirstDelay)
	{
		if (!Button)
		{
			return;
		}

		for (FLoadoutGearShimmerState& State : LoadoutGearShimmerStates)
		{
			if (State.Button.Get() == Button)
			{
				return;
			}
		}

		FLoadoutGearShimmerState& State = LoadoutGearShimmerStates.AddDefaulted_GetRef();
		State.Button = Button;
		State.BaseStyle = Button->GetStyle();
		State.BaseColorAndOpacity = Button->GetColorAndOpacity();
		State.BaseBackgroundColor = Button->GetBackgroundColor();
		State.Period = Period;
		State.Duration = Duration;
		State.Elapsed = Period - FirstDelay;
		ApplyLoadoutGearShimmerVisual(
			Button,
			State.BaseStyle,
			State.BaseColorAndOpacity,
			State.BaseBackgroundColor,
			0.0f);

		if (!LoadoutGearShimmerTickerHandle.IsValid())
		{
			LoadoutGearShimmerTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateStatic(&TickLoadoutGearShimmer));
		}
	}

	void RegisterLoadoutImageShimmer(
		UImage* Image,
		UButton* HoverButton,
		const float Period,
		const float Duration,
		const float FirstDelay)
	{
		if (!Image)
		{
			return;
		}

		for (FLoadoutImageShimmerState& State : LoadoutImageShimmerStates)
		{
			if (State.Image.Get() == Image)
			{
				return;
			}
		}

		FSlateBrush BaseBrush = Image->GetBrush();
		FLinearColor BaseColorAndOpacity = Image->GetColorAndOpacity();
		if (IsLoadoutReturnImageStillUsingEditorPlaceholder(Image))
		{
			BaseBrush.TintColor = FSlateColor(LoadoutReturnNormalTint);
			BaseColorAndOpacity = FLinearColor::White;
		}

		const FLinearColor BaseBrushTint = BaseBrush.TintColor.GetSpecifiedColor();
		if (BaseBrushTint.A <= KINDA_SMALL_NUMBER || BaseColorAndOpacity.A <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		FLoadoutImageShimmerState& State = LoadoutImageShimmerStates.AddDefaulted_GetRef();
		State.Image = Image;
		State.HoverButton = HoverButton;
		State.BaseBrush = BaseBrush;
		State.BaseColorAndOpacity = BaseColorAndOpacity;
		State.Period = Period;
		State.Duration = Duration;
		State.Elapsed = Period - FirstDelay;
		State.bPreviousHovered = HoverButton && HoverButton->IsHovered();
		ApplyLoadoutImageShimmerVisual(
			Image,
			State.BaseBrush,
			State.BaseColorAndOpacity,
			0.0f,
			State.bPreviousHovered);

		if (!LoadoutGearShimmerTickerHandle.IsValid())
		{
			LoadoutGearShimmerTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateStatic(&TickLoadoutGearShimmer));
		}
	}

	void RegisterLoadoutImageShimmerDeferred(
		UUserWidget* OwnerWidget,
		const FName ImageName,
		const FName HoverButtonName,
		const float Period,
		const float Duration,
		const float FirstDelay)
	{
		if (!OwnerWidget || ImageName.IsNone())
		{
			return;
		}

		for (FLoadoutDeferredImageShimmerRegistration& State : LoadoutDeferredImageShimmerRegistrations)
		{
			if (State.OwnerWidget.Get() == OwnerWidget && State.ImageName == ImageName)
			{
				return;
			}
		}

		FLoadoutDeferredImageShimmerRegistration& State =
			LoadoutDeferredImageShimmerRegistrations.AddDefaulted_GetRef();
		State.OwnerWidget = OwnerWidget;
		State.ImageName = ImageName;
		State.HoverButtonName = HoverButtonName;
		State.Period = Period;
		State.Duration = Duration;
		State.FirstDelay = FirstDelay;

		if (!LoadoutGearShimmerTickerHandle.IsValid())
		{
			LoadoutGearShimmerTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateStatic(&TickLoadoutGearShimmer));
		}
	}

	struct FALSTurnInPlaceBridgeState
	{
		bool bHasSourceYaw = false;
		double SourceYaw = 0.0;
	};

	TMap<TWeakObjectPtr<ACharacter>, FALSTurnInPlaceBridgeState> GALSTurnInPlaceBridgeStates;

	bool GetEnumLikePropertyValue(const UObject* Object, const FName PropertyName, int64& OutValue, FString& OutDisplayName)
	{
		OutValue = 0;
		OutDisplayName.Reset();

		if (!Object)
		{
			return false;
		}

		const FProperty* Property = Object->GetClass()->FindPropertyByName(PropertyName);
		if (!Property)
		{
			return false;
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
		const UEnum* Enum = nullptr;

		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			OutValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
			Enum = EnumProperty->GetEnum();
		}
		else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			OutValue = ByteProperty->GetPropertyValue(ValuePtr);
			Enum = ByteProperty->Enum;
		}
		else
		{
			return false;
		}

		if (Enum)
		{
			OutDisplayName = Enum->GetDisplayNameTextByValue(OutValue).ToString();
			if (OutDisplayName.IsEmpty())
			{
				OutDisplayName = Enum->GetNameStringByValue(OutValue);
			}
		}

		return true;
	}

	bool SetEnumLikePropertyValue(UObject* Object, const FName PropertyName, const int64 Value)
	{
		if (!Object)
		{
			return false;
		}

		FProperty* Property = Object->GetClass()->FindPropertyByName(PropertyName);
		if (!Property)
		{
			return false;
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, Value);
			return true;
		}

		if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			ByteProperty->SetPropertyValue(ValuePtr, static_cast<uint8>(Value));
			return true;
		}

		return false;
	}

	FString NormalizeOverlayEnumText(FString Text)
	{
		FString RightSide;
		if (Text.Split(TEXT("::"), nullptr, &RightSide, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			Text = RightSide;
		}

		Text = Text.ToUpper();
		Text.ReplaceInline(TEXT(" "), TEXT(""));
		Text.ReplaceInline(TEXT("_"), TEXT(""));
		Text.ReplaceInline(TEXT("-"), TEXT(""));
		return Text;
	}

	void AddOverlayAlias(TArray<FString>& Aliases, const FString& Alias)
	{
		const FString NormalizedAlias = NormalizeOverlayEnumText(Alias);
		if (!NormalizedAlias.IsEmpty())
		{
			Aliases.AddUnique(NormalizedAlias);
		}
	}

	bool HasOverlayAlias(const TArray<FString>& Aliases, const TCHAR* Alias)
	{
		const FString NormalizedAlias = NormalizeOverlayEnumText(Alias);
		return Aliases.Contains(NormalizedAlias);
	}

	bool IsHeadHitBoneName(const FName BoneName)
	{
		if (BoneName.IsNone())
		{
			return false;
		}

		FString NormalizedName = BoneName.ToString().ToLower();
		NormalizedName.ReplaceInline(TEXT("-"), TEXT("_"));
		NormalizedName.ReplaceInline(TEXT(" "), TEXT("_"));

		static const TSet<FString> ExactHeadBoneNames = {
			TEXT("head"),
			TEXT("head_jnt"),
			TEXT("head_end"),
			TEXT("facialroot"),
			TEXT("facial_root"),
			TEXT("jaw"),
			TEXT("jawbone"),
			TEXT("mandible"),
			TEXT("eye_l"),
			TEXT("eye_r"),
			TEXT("eyeball_l"),
			TEXT("eyeball_r"),
			TEXT("teeth"),
			TEXT("tongue")
		};

		if (ExactHeadBoneNames.Contains(NormalizedName))
		{
			return true;
		}

		return NormalizedName.StartsWith(TEXT("head_"))
			|| NormalizedName.EndsWith(TEXT("_head"))
			|| NormalizedName.Contains(TEXT("_head_"))
			|| NormalizedName.StartsWith(TEXT("facial_"))
			|| NormalizedName.StartsWith(TEXT("face_"))
			|| NormalizedName.StartsWith(TEXT("jaw_"))
			|| NormalizedName.StartsWith(TEXT("eye_"))
			|| NormalizedName.StartsWith(TEXT("eyeball_"));
	}

	bool IsLoadoutWeaponLayerName(const UUserWidget* WeaponLayerWidget, const TCHAR* WeaponName)
	{
		if (!WeaponLayerWidget || !WeaponName)
		{
			return false;
		}

		const FString WidgetName = WeaponLayerWidget->GetName();
		return WidgetName.Equals(WeaponName, ESearchCase::IgnoreCase)
			|| WidgetName.StartsWith(FString::Printf(TEXT("%s_"), WeaponName), ESearchCase::IgnoreCase);
	}

	FString GetLoadoutWeaponLayerDisplayText(const UUserWidget* WeaponLayerWidget)
	{
		if (!WeaponLayerWidget)
		{
			return FString();
		}

		const UTextBlock* NameText = Cast<UTextBlock>(WeaponLayerWidget->GetWidgetFromName(TEXT("NameText")));
		if (NameText)
		{
			const FString Text = NameText->GetText().ToString().TrimStartAndEnd();
			if (!Text.IsEmpty())
			{
				return Text;
			}
		}

		FString FirstText;
		if (WeaponLayerWidget->WidgetTree)
		{
			WeaponLayerWidget->WidgetTree->ForEachWidget(
				[&FirstText](UWidget* Widget)
				{
					if (!FirstText.IsEmpty())
					{
						return;
					}

					const UTextBlock* TextBlock = Cast<UTextBlock>(Widget);
					if (!TextBlock)
					{
						return;
					}

					FirstText = TextBlock->GetText().ToString().TrimStartAndEnd();
				});
		}

		return FirstText;
	}

	FString GetLoadoutWeaponLayerIdentifier(const UUserWidget* WeaponLayerWidget)
	{
		if (!WeaponLayerWidget)
		{
			return FString();
		}

		static const FName IdentifierPropertyNames[] =
		{
			TEXT("WeaponIdentifier"),
			TEXT("Weapon Identifier"),
			TEXT("WeaponID")
		};

		for (const FName& IdentifierPropertyName : IdentifierPropertyNames)
		{
			if (const FNameProperty* IdentifierNameProperty =
				FindFProperty<FNameProperty>(WeaponLayerWidget->GetClass(), IdentifierPropertyName))
			{
				return IdentifierNameProperty->GetPropertyValue_InContainer(WeaponLayerWidget).ToString();
			}
		}

		return FString();
	}

	bool IsLoadoutWeaponLayerIdentity(const UUserWidget* WeaponLayerWidget, const TCHAR* WeaponName, const TCHAR* DisplayAlias = nullptr)
	{
		if (IsLoadoutWeaponLayerName(WeaponLayerWidget, WeaponName))
		{
			return true;
		}

		const FString WeaponIdentifier = GetLoadoutWeaponLayerIdentifier(WeaponLayerWidget);
		if (WeaponIdentifier.Equals(WeaponName, ESearchCase::IgnoreCase))
		{
			return true;
		}

		const FString DisplayText = GetLoadoutWeaponLayerDisplayText(WeaponLayerWidget);
		return DisplayText.Equals(WeaponName, ESearchCase::IgnoreCase)
			|| (DisplayAlias && DisplayText.Equals(DisplayAlias, ESearchCase::IgnoreCase));
	}

	bool ShouldHideLoadoutWeaponLayer(const UUserWidget* WeaponLayerWidget)
	{
		return TMWeaponIconResolver::ShouldCollapseWeaponRow(WeaponLayerWidget);
	}

	UTexture2D* GetLoadoutWeaponLayerIconTexture(const UUserWidget* WeaponLayerWidget)
	{
		return TMWeaponIconResolver::ResolveIconTexture(WeaponLayerWidget, false);
	}

	FLinearColor GetLoadoutWeaponLayerIconTint(const UTextBlock* NameText)
	{
		if (!NameText)
		{
			return FLinearColor::White;
		}

		FLinearColor Tint = NameText->GetColorAndOpacity().GetSpecifiedColor();
		if (Tint.A <= 0.01f)
		{
			Tint = FLinearColor::White;
		}

		Tint.A = 1.0f;
		return Tint;
	}

	FSlateBrush MakeLoadoutWeaponLayerIconBrush(UTexture2D* IconTexture)
	{
		FSlateBrush Brush;
		Brush.DrawAs = IconTexture ? ESlateBrushDrawType::Image : ESlateBrushDrawType::NoDrawType;
		if (IconTexture)
		{
			Brush.SetResourceObject(IconTexture);
		}
		Brush.SetImageSize(FVector2D(LoadoutWeaponLayerIconWidth, LoadoutWeaponLayerIconHeight));
		return Brush;
	}

	void ApplyLoadoutWeaponLayerSlotSize(UWidget* Widget)
	{
		if (!Widget || !Widget->Slot)
		{
			return;
		}

		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot))
		{
			CanvasSlot->SetAutoSize(false);
			CanvasSlot->SetSize(FVector2D(LoadoutWeaponLayerIconWidth, LoadoutWeaponLayerIconHeight));
			return;
		}

		if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(Widget->Slot))
		{
			VerticalSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			VerticalSlot->SetPadding(FMargin(0.0f));
			VerticalSlot->SetHorizontalAlignment(HAlign_Left);
			VerticalSlot->SetVerticalAlignment(VAlign_Center);
			return;
		}

		if (UHorizontalBoxSlot* HorizontalSlot = Cast<UHorizontalBoxSlot>(Widget->Slot))
		{
			HorizontalSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			HorizontalSlot->SetPadding(FMargin(0.0f));
			HorizontalSlot->SetHorizontalAlignment(HAlign_Left);
			HorizontalSlot->SetVerticalAlignment(VAlign_Center);
		}
	}

#if WITH_EDITOR
	bool IsWeaponPoseRelevantBone(const FName BoneName)
	{
		static const TSet<FName> RelevantBones = {
			TEXT("Camera_FP"),
			TEXT("FP_Camera"),
			TEXT("VB Control"),
			TEXT("VB Hand_R"),
			TEXT("VB Hand_L"),
			TEXT("VB LHS_ik_hand_l"),
			TEXT("VB LHS_ik_hand_r"),
			TEXT("VB RHS_ik_hand_l"),
			TEXT("VB RHS_ik_hand_gun"),
			TEXT("Weapon"),
			TEXT("hand_r"),
			TEXT("hand_l"),
			TEXT("ik_hand_l"),
			TEXT("ik_hand_r"),
			TEXT("ik_hand_gun"),
			TEXT("head")
		};
		return RelevantBones.Contains(BoneName);
	}

	FString DescribeGraphPinLinks(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return TEXT("None");
		}

		TArray<FString> Links;
		for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			const UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
			Links.Add(FString::Printf(
				TEXT("%s.%s"),
				LinkedNode ? *LinkedNode->GetName() : TEXT("None"),
				LinkedPin ? *LinkedPin->PinName.ToString() : TEXT("None")));
		}

		return Links.Num() > 0 ? FString::Join(Links, TEXT(", ")) : TEXT("None");
	}

	bool ShouldDumpGraphPin(const UEdGraphPin* Pin)
	{
		return Pin
			&& (Pin->LinkedTo.Num() > 0
				|| Pin->PinName == TEXT("ComponentPose")
				|| Pin->PinName == TEXT("Pose")
				|| Pin->PinName == TEXT("Translation")
				|| Pin->PinName == TEXT("Rotation")
				|| Pin->PinName == TEXT("Scale")
				|| Pin->PinName == TEXT("Alpha")
				|| Pin->PinName == TEXT("EffectorTransform"));
	}

	void AppendNodePins(const UEdGraphNode* Node, FString& Dump)
	{
		if (!Node)
		{
			return;
		}

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!ShouldDumpGraphPin(Pin))
			{
				continue;
			}

			Dump += FString::Printf(
				TEXT("    Pin=%s Dir=%s Default={%s} Links=%s\n"),
				*Pin->PinName.ToString(),
				Pin->Direction == EGPD_Input ? TEXT("In") : TEXT("Out"),
				*Pin->DefaultValue,
				*DescribeGraphPinLinks(Pin));
		}
	}

	void AppendAnimBlueprintGraphDump(const TCHAR* AssetPath, FString& Dump)
	{
		UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, AssetPath);
		if (!AnimBlueprint)
		{
			Dump += FString::Printf(TEXT("FAILED load %s\n"), AssetPath);
			return;
		}

		Dump += FString::Printf(TEXT("ASSET %s\n"), *AnimBlueprint->GetPathName());
		TArray<UEdGraph*> Graphs;
		AnimBlueprint->GetAllGraphs(Graphs);

		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}

			for (UEdGraphNode* GraphNode : Graph->Nodes)
			{
				if (const UAnimGraphNode_ModifyBone* ModifyBoneNode = Cast<UAnimGraphNode_ModifyBone>(GraphNode))
				{
					const FName BoneName = ModifyBoneNode->Node.BoneToModify.BoneName;
					if (!IsWeaponPoseRelevantBone(BoneName))
					{
						continue;
					}

					Dump += FString::Printf(
						TEXT("  Graph=%s Node=%s Type=ModifyBone Comment={%s} Bone=%s TM=%d RM=%d TS=%d RS=%d Loc=%s Rot=%s\n"),
						*Graph->GetName(),
						*ModifyBoneNode->GetName(),
						*ModifyBoneNode->NodeComment,
						*BoneName.ToString(),
						static_cast<int32>(ModifyBoneNode->Node.TranslationMode),
						static_cast<int32>(ModifyBoneNode->Node.RotationMode),
						static_cast<int32>(ModifyBoneNode->Node.TranslationSpace),
						static_cast<int32>(ModifyBoneNode->Node.RotationSpace),
						*ModifyBoneNode->Node.Translation.ToString(),
						*ModifyBoneNode->Node.Rotation.ToString());
					AppendNodePins(ModifyBoneNode, Dump);
				}
				else if (const UAnimGraphNode_CopyBone* CopyBoneNode = Cast<UAnimGraphNode_CopyBone>(GraphNode))
				{
					const FName SourceBoneName = CopyBoneNode->Node.SourceBone.BoneName;
					const FName TargetBoneName = CopyBoneNode->Node.TargetBone.BoneName;
					if (!IsWeaponPoseRelevantBone(SourceBoneName) && !IsWeaponPoseRelevantBone(TargetBoneName))
					{
						continue;
					}

					Dump += FString::Printf(
						TEXT("  Graph=%s Node=%s Type=CopyBone Source=%s Target=%s CopyT=%d CopyR=%d CopyS=%d Space=%d\n"),
						*Graph->GetName(),
						*CopyBoneNode->GetName(),
						*SourceBoneName.ToString(),
						*TargetBoneName.ToString(),
						CopyBoneNode->Node.bCopyTranslation ? 1 : 0,
						CopyBoneNode->Node.bCopyRotation ? 1 : 0,
						CopyBoneNode->Node.bCopyScale ? 1 : 0,
						static_cast<int32>(CopyBoneNode->Node.ControlSpace));
					AppendNodePins(CopyBoneNode, Dump);
				}
				else if (const UAnimGraphNode_Fabrik* FabrikNode = Cast<UAnimGraphNode_Fabrik>(GraphNode))
				{
					const FName RootBoneName = FabrikNode->Node.RootBone.BoneName;
					const FName TipBoneName = FabrikNode->Node.TipBone.BoneName;
					const FName EffectorTargetName = FabrikNode->Node.EffectorTarget.bUseSocket
						? FabrikNode->Node.EffectorTarget.SocketReference.SocketName
						: FabrikNode->Node.EffectorTarget.BoneReference.BoneName;
					if (!IsWeaponPoseRelevantBone(RootBoneName)
						&& !IsWeaponPoseRelevantBone(TipBoneName)
						&& !IsWeaponPoseRelevantBone(EffectorTargetName))
					{
						continue;
					}

					Dump += FString::Printf(
						TEXT("  Graph=%s Node=%s Type=FABRIK Root=%s Tip=%s EffectorTarget=%s UseSocket=%d ETSpace=%d ERSource=%d AlphaType=%d Alpha=%.3f AlphaCurve=%s Effector=%s\n"),
						*Graph->GetName(),
						*FabrikNode->GetName(),
						*RootBoneName.ToString(),
						*TipBoneName.ToString(),
						*EffectorTargetName.ToString(),
						FabrikNode->Node.EffectorTarget.bUseSocket ? 1 : 0,
						static_cast<int32>(FabrikNode->Node.EffectorTransformSpace.GetValue()),
						static_cast<int32>(FabrikNode->Node.EffectorRotationSource.GetValue()),
						static_cast<int32>(FabrikNode->Node.AlphaInputType),
						FabrikNode->Node.Alpha,
						*FabrikNode->Node.AlphaCurveName.ToString(),
						*FabrikNode->Node.EffectorTransform.ToString());
					AppendNodePins(FabrikNode, Dump);
				}
			}
		}
	}
#endif

	TArray<FString> BuildOverlayStateAliases(const int64 PoseValue, const FString& PoseDisplayName)
	{
		TArray<FString> Aliases;
		AddOverlayAlias(Aliases, PoseDisplayName);

		switch (PoseValue)
		{
		case 0:
			AddOverlayAlias(Aliases, TEXT("DEFAULT"));
			break;
		case 1:
			AddOverlayAlias(Aliases, TEXT("RIFLE"));
			break;
		case 2:
			AddOverlayAlias(Aliases, TEXT("PISTOL_1H"));
			break;
		case 3:
			AddOverlayAlias(Aliases, TEXT("PISTOL_2H"));
			break;
		case 4:
			AddOverlayAlias(Aliases, TEXT("BOW"));
			break;
		case 5:
			AddOverlayAlias(Aliases, TEXT("TORCH"));
			break;
		case 6:
			AddOverlayAlias(Aliases, TEXT("BINOCULARS"));
			break;
		case 7:
			AddOverlayAlias(Aliases, TEXT("BOX"));
			break;
		case 8:
			AddOverlayAlias(Aliases, TEXT("BARREL"));
			break;
		case 9:
			AddOverlayAlias(Aliases, TEXT("INJURED"));
			break;
		case 10:
			AddOverlayAlias(Aliases, TEXT("HANDS_TIED"));
			break;
		default:
			break;
		}

		if (HasOverlayAlias(Aliases, TEXT("PISTOL1H")))
		{
			AddOverlayAlias(Aliases, TEXT("PISTOL_ONE_HANDED"));
		}
		if (HasOverlayAlias(Aliases, TEXT("PISTOL2H")))
		{
			AddOverlayAlias(Aliases, TEXT("PISTOL_TWO_HANDED"));
		}
		bool bHasBinocularAlias = false;
		for (const FString& Alias : Aliases)
		{
			if (Alias.Contains(TEXT("BINOCULAR")))
			{
				bHasBinocularAlias = true;
				break;
			}
		}
		if (bHasBinocularAlias)
		{
			AddOverlayAlias(Aliases, TEXT("BINOCULARS"));
		}

		return Aliases;
	}

	bool ResolveEnumValueByAliases(const UEnum* Enum, const TArray<FString>& Aliases, int64& OutValue)
	{
		if (!Enum || Aliases.IsEmpty())
		{
			return false;
		}

		for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
		{
#if WITH_METADATA
			if (Enum->HasMetaData(TEXT("Hidden"), Index))
			{
				continue;
			}
#endif

			const FString Name = NormalizeOverlayEnumText(Enum->GetNameStringByIndex(Index));
			const FString DisplayName = NormalizeOverlayEnumText(Enum->GetDisplayNameTextByIndex(Index).ToString());
			if (Aliases.Contains(Name) || Aliases.Contains(DisplayName))
			{
				OutValue = Enum->GetValueByIndex(Index);
				return true;
			}
		}

		return false;
	}

	bool SetEnumLikeValueByAliases(FProperty* Property, void* ValuePtr, const TArray<FString>& Aliases)
	{
		if (!Property || !ValuePtr || Aliases.IsEmpty())
		{
			return false;
		}

		UEnum* Enum = nullptr;
		FNumericProperty* NumericProperty = nullptr;
		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			Enum = EnumProperty->GetEnum();
			NumericProperty = EnumProperty->GetUnderlyingProperty();
		}
		else if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			Enum = ByteProperty->Enum;
			NumericProperty = ByteProperty;
		}

		int64 Value = 0;
		if (!Enum || !NumericProperty || !ResolveEnumValueByAliases(Enum, Aliases, Value))
		{
			return false;
		}

		NumericProperty->SetIntPropertyValue(ValuePtr, Value);
		return true;
	}

	bool SetEnumLikePropertyValueByAliases(UObject* Object, const FName PropertyName, const TArray<FString>& Aliases)
	{
		if (!Object || Aliases.IsEmpty())
		{
			return false;
		}

		FProperty* Property = Object->GetClass()->FindPropertyByName(PropertyName);
		if (!Property)
		{
			return false;
		}

		return SetEnumLikeValueByAliases(Property, Property->ContainerPtrToValuePtr<void>(Object), Aliases);
	}

	bool SetEnumLikePropertyValueByAliasesOrValue(UObject* Object, const FName PropertyName, const TArray<FString>& Aliases, const int64 FallbackValue)
	{
		if (!Object)
		{
			return false;
		}

		FProperty* Property = Object->GetClass()->FindPropertyByName(PropertyName);
		if (!Property)
		{
			return false;
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
		UEnum* Enum = nullptr;
		FNumericProperty* NumericProperty = nullptr;
		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			Enum = EnumProperty->GetEnum();
			NumericProperty = EnumProperty->GetUnderlyingProperty();
		}
		else if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			Enum = ByteProperty->Enum;
			NumericProperty = ByteProperty;
		}

		if (!NumericProperty)
		{
			return false;
		}

		int64 TargetValue = FallbackValue;
		if (Enum)
		{
			ResolveEnumValueByAliases(Enum, Aliases, TargetValue);
		}

		if (NumericProperty->GetSignedIntPropertyValue(ValuePtr) == TargetValue)
		{
			return false;
		}

		NumericProperty->SetIntPropertyValue(ValuePtr, TargetValue);
		return true;
	}

	bool GetBoolPropertyValueByName(const UObject* Object, const FName PropertyName, bool& bOutValue)
	{
		if (!Object)
		{
			return false;
		}

		const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!BoolProperty)
		{
			return false;
		}

		bOutValue = BoolProperty->GetPropertyValue_InContainer(Object);
		return true;
	}

	bool GetNumericPropertyValueByName(const UObject* Object, const FName PropertyName, double& OutValue)
	{
		if (!Object)
		{
			return false;
		}

		const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!NumericProperty)
		{
			return false;
		}

		const void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(Object);
		OutValue = NumericProperty->IsFloatingPoint()
			? NumericProperty->GetFloatingPointPropertyValue(ValuePtr)
			: static_cast<double>(NumericProperty->GetSignedIntPropertyValue(ValuePtr));
		return true;
	}

	bool SetNumericPropertyValueByName(UObject* Object, const FName PropertyName, const double Value)
	{
		if (!Object)
		{
			return false;
		}

		FNumericProperty* NumericProperty = CastField<FNumericProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!NumericProperty)
		{
			return false;
		}

		void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(Object);
		const double CurrentValue = NumericProperty->IsFloatingPoint()
			? NumericProperty->GetFloatingPointPropertyValue(ValuePtr)
			: static_cast<double>(NumericProperty->GetSignedIntPropertyValue(ValuePtr));
		if (FMath::IsNearlyEqual(CurrentValue, Value, KINDA_SMALL_NUMBER))
		{
			return false;
		}

		if (NumericProperty->IsFloatingPoint())
		{
			NumericProperty->SetFloatingPointPropertyValue(ValuePtr, Value);
		}
		else
		{
			NumericProperty->SetIntPropertyValue(ValuePtr, FMath::RoundToInt64(Value));
		}
		return true;
	}

	bool SetRotatorPropertyValueByName(UObject* Object, const FName PropertyName, const FRotator& Value)
	{
		if (!Object)
		{
			return false;
		}

		FStructProperty* StructProperty = CastField<FStructProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!StructProperty || StructProperty->Struct != TBaseStructure<FRotator>::Get())
		{
			return false;
		}

		FRotator* ValuePtr = StructProperty->ContainerPtrToValuePtr<FRotator>(Object);
		if (ValuePtr && ValuePtr->Equals(Value, KINDA_SMALL_NUMBER))
		{
			return false;
		}

		if (ValuePtr)
		{
			*ValuePtr = Value;
			return true;
		}
		return false;
	}

	bool SetVector2DPropertyValueByName(UObject* Object, const FName PropertyName, const FVector2D& Value)
	{
		if (!Object)
		{
			return false;
		}

		FStructProperty* StructProperty = CastField<FStructProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!StructProperty || StructProperty->Struct != TBaseStructure<FVector2D>::Get())
		{
			return false;
		}

		FVector2D* ValuePtr = StructProperty->ContainerPtrToValuePtr<FVector2D>(Object);
		if (ValuePtr && ValuePtr->Equals(Value, KINDA_SMALL_NUMBER))
		{
			return false;
		}

		if (ValuePtr)
		{
			*ValuePtr = Value;
			return true;
		}
		return false;
	}

	void AddRotationModeAlias(TArray<FString>& Aliases, const FString& Alias)
	{
		AddOverlayAlias(Aliases, Alias);
	}

	TArray<FString> BuildRotationModeAliasesForAimState(const ACharacter* Character, const bool bAiming)
	{
		TArray<FString> Aliases;
		if (bAiming)
		{
			AddRotationModeAlias(Aliases, TEXT("Aiming"));
			return Aliases;
		}

		int64 DesiredRotationModeValue = 1;
		FString DesiredRotationModeDisplayName;
		if (GetEnumLikePropertyValue(Character, TEXT("DesiredRotationMode"), DesiredRotationModeValue, DesiredRotationModeDisplayName))
		{
			AddRotationModeAlias(Aliases, DesiredRotationModeDisplayName);
			switch (DesiredRotationModeValue)
			{
			case 0:
				AddRotationModeAlias(Aliases, TEXT("VelocityDirection"));
				AddRotationModeAlias(Aliases, TEXT("Velocity Direction"));
				break;
			case 1:
				AddRotationModeAlias(Aliases, TEXT("LookingDirection"));
				AddRotationModeAlias(Aliases, TEXT("Looking Direction"));
				break;
			case 2:
				AddRotationModeAlias(Aliases, TEXT("Aiming"));
				break;
			default:
				break;
			}
		}

		if (Aliases.IsEmpty())
		{
			AddRotationModeAlias(Aliases, TEXT("LookingDirection"));
			AddRotationModeAlias(Aliases, TEXT("Looking Direction"));
		}

		return Aliases;
	}

	bool AliasesContainAiming(const TArray<FString>& Aliases)
	{
		return Aliases.Contains(NormalizeOverlayEnumText(TEXT("Aiming")));
	}

	TArray<FString> BuildLookingDirectionAliases()
	{
		TArray<FString> Aliases;
		AddRotationModeAlias(Aliases, TEXT("LookingDirection"));
		AddRotationModeAlias(Aliases, TEXT("Looking Direction"));
		return Aliases;
	}

	TArray<FString> BuildThirdPersonViewModeAliases()
	{
		TArray<FString> Aliases;
		AddOverlayAlias(Aliases, TEXT("ThirdPerson"));
		AddOverlayAlias(Aliases, TEXT("Third Person"));
		AddOverlayAlias(Aliases, TEXT("TP"));
		return Aliases;
	}

	bool SetBoolPropertyByName(UObject* Object, const FName PropertyName, const bool bValue)
	{
		if (!Object)
		{
			return false;
		}

		FBoolProperty* BoolProperty = CastField<FBoolProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!BoolProperty)
		{
			return false;
		}

		if (BoolProperty->GetPropertyValue_InContainer(Object) == bValue)
		{
			return false;
		}

		BoolProperty->SetPropertyValue_InContainer(Object, bValue);
		return true;
	}

	bool SetObjectPropertyValueByName(UObject* Object, const FName PropertyName, UObject* Value)
	{
		if (!Object || !Value)
		{
			return false;
		}

		FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!ObjectProperty || !Value->IsA(ObjectProperty->PropertyClass))
		{
			return false;
		}

		UObject* CurrentValue = ObjectProperty->GetObjectPropertyValue_InContainer(Object);
		if (CurrentValue == Value)
		{
			return false;
		}

		ObjectProperty->SetObjectPropertyValue_InContainer(Object, Value);
		return true;
	}

	bool CallSetRotationModeFunction(UObject* Object, const TArray<FString>& Aliases)
	{
		if (!Object || Aliases.IsEmpty())
		{
			return false;
		}

		UFunction* Function = Object->FindFunction(TEXT("SetRotationMode"));
		if (!Function)
		{
			return false;
		}

		void* Parameters = FMemory_Alloca(Function->ParmsSize);
		FMemory::Memzero(Parameters, Function->ParmsSize);

		bool bSetRotationMode = false;
		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property || !Property->HasAnyPropertyFlags(CPF_Parm) || Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				continue;
			}

			void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Parameters);
			if (!bSetRotationMode && SetEnumLikeValueByAliases(Property, ValuePtr, Aliases))
			{
				bSetRotationMode = true;
			}
			else if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
			{
				const FString PropertyName = Property->GetName();
				if (PropertyName.Contains(TEXT("Force"), ESearchCase::IgnoreCase)
					|| PropertyName.Equals(TEXT("bForce"), ESearchCase::IgnoreCase))
				{
					BoolProperty->SetPropertyValue(ValuePtr, true);
				}
			}
		}

		if (!bSetRotationMode)
		{
			return false;
		}

		Object->ProcessEvent(Function, Parameters);
		return true;
	}

	bool PatchLinkedOverlayAnimMasters(USkeletalMeshComponent* Mesh, UAnimInstance* MasterAnimInstance)
	{
		if (!Mesh || !MasterAnimInstance)
		{
			return false;
		}

		bool bChanged = false;
		const USkeletalMeshComponent* ConstMesh = Mesh;
		for (UAnimInstance* LinkedAnimInstance : ConstMesh->GetLinkedAnimInstances())
		{
			if (!LinkedAnimInstance || LinkedAnimInstance == MasterAnimInstance)
			{
				continue;
			}

			bChanged |= SetObjectPropertyValueByName(LinkedAnimInstance, TEXT("AnimBPMaster"), MasterAnimInstance);
		}

		return bChanged;
	}

	bool ApplyRotationModeAliasesToAnimInstance(UAnimInstance* AnimInstance, const TArray<FString>& Aliases)
	{
		if (!AnimInstance)
		{
			return false;
		}

		const bool bAiming = AliasesContainAiming(Aliases);
		bool bChanged = SetEnumLikePropertyValueByAliases(AnimInstance, TEXT("RotationMode"), Aliases);
		bChanged |= SetBoolPropertyByName(AnimInstance, TEXT("bIsAiming"), bAiming);
		bChanged |= SetBoolPropertyByName(AnimInstance, TEXT("bAiming"), bAiming);
		bChanged |= SetBoolPropertyByName(AnimInstance, TEXT("IsAiming"), bAiming);
		return bChanged;
	}

	bool ApplyRotationModeAliasesToMesh(USkeletalMeshComponent* Mesh, const TArray<FString>& Aliases)
	{
		if (!Mesh)
		{
			return false;
		}

		bool bChanged = false;
		UAnimInstance* AnimInstance = Mesh->GetAnimInstance();
		bChanged |= ApplyRotationModeAliasesToAnimInstance(AnimInstance, Aliases);
		bChanged |= PatchLinkedOverlayAnimMasters(Mesh, AnimInstance);

		const USkeletalMeshComponent* ConstMesh = Mesh;
		for (UAnimInstance* LinkedAnimInstance : ConstMesh->GetLinkedAnimInstances())
		{
			bChanged |= ApplyRotationModeAliasesToAnimInstance(LinkedAnimInstance, Aliases);
		}

		return bChanged;
	}

	bool ReadNumericPropertyFromMeshAnimInstances(const USkeletalMeshComponent* Mesh, const FName PropertyName, double& OutValue)
	{
		if (!Mesh)
		{
			return false;
		}

		if (const UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
		{
			if (GetNumericPropertyValueByName(AnimInstance, PropertyName, OutValue))
			{
				return true;
			}
		}

		for (const UAnimInstance* LinkedAnimInstance : Mesh->GetLinkedAnimInstances())
		{
			if (GetNumericPropertyValueByName(LinkedAnimInstance, PropertyName, OutValue))
			{
				return true;
			}
		}

		return false;
	}

	bool ReadTurnYawOffset(ACharacter* Character, double& OutYawOffset)
	{
		if (!Character)
		{
			return false;
		}

		static const FName YawOffsetPropertyNames[] =
		{
			TEXT("YawOffset"),
			TEXT("TurnYawOffset"),
			TEXT("AimYawOffset"),
			TEXT("RotationYawOffset")
		};

		USkeletalMeshComponent* Mesh = Character->GetMesh();
		for (const FName PropertyName : YawOffsetPropertyNames)
		{
			if (ReadNumericPropertyFromMeshAnimInstances(Mesh, PropertyName, OutYawOffset)
				|| GetNumericPropertyValueByName(Character, PropertyName, OutYawOffset))
			{
				OutYawOffset = FRotator::NormalizeAxis(OutYawOffset);
				return true;
			}
		}

		return false;
	}

	double ReadALSTurnPitch(ACharacter* Character, const FRotator& BaseAimRotation)
	{
		double Pitch = BaseAimRotation.Pitch;
		if (!Character)
		{
			return Pitch;
		}

		double MPSPitch = 0.0;
		if (GetNumericPropertyValueByName(Character, TEXT("Pitch"), MPSPitch)
			|| ReadNumericPropertyFromMeshAnimInstances(Character->GetMesh(), TEXT("Pitch"), MPSPitch))
		{
			Pitch = MPSPitch;
		}

		return FRotator::NormalizeAxis(Pitch);
	}

	bool ReadALSAimBool(ACharacter* Character, bool& bOutAiming)
	{
		if (!Character)
		{
			return false;
		}

		static const FName AimPropertyNames[] =
		{
			TEXT("bIsAiming"),
			TEXT("IsAiming"),
			TEXT("IsAiming?"),
			TEXT("bAiming"),
			TEXT("Aiming"),
			TEXT("bADS"),
			TEXT("IsADS"),
			TEXT("bIsADS")
		};

		for (const FName PropertyName : AimPropertyNames)
		{
			if (GetBoolPropertyValueByName(Character, PropertyName, bOutAiming))
			{
				return true;
			}
		}

		if (const USkeletalMeshComponent* Mesh = Character->GetMesh())
		{
			if (const UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
			{
				for (const FName PropertyName : AimPropertyNames)
				{
					if (GetBoolPropertyValueByName(AnimInstance, PropertyName, bOutAiming))
					{
						return true;
					}
				}
			}
		}

		return false;
	}

	bool ApplyViewModeToAnimInstance(UAnimInstance* AnimInstance, const TArray<FString>& ViewModeAliases)
	{
		return SetEnumLikePropertyValueByAliasesOrValue(AnimInstance, TEXT("ViewMode"), ViewModeAliases, 0);
	}

	bool ApplyLookingDirectionToAnimInstance(UAnimInstance* AnimInstance, const TArray<FString>& RotationModeAliases)
	{
		if (!AnimInstance)
		{
			return false;
		}

		bool bChanged = SetEnumLikePropertyValueByAliasesOrValue(AnimInstance, TEXT("RotationMode"), RotationModeAliases, 1);
		bChanged |= SetBoolPropertyByName(AnimInstance, TEXT("bIsAiming"), false);
		bChanged |= SetBoolPropertyByName(AnimInstance, TEXT("bAiming"), false);
		bChanged |= SetBoolPropertyByName(AnimInstance, TEXT("IsAiming"), false);
		return bChanged;
	}

	bool ApplyALSTurnValuesToObject(UObject* Object, const FRotator& AimingRotation, const FVector2D& AimingAngle, const double AimYawRate)
	{
		if (!Object)
		{
			return false;
		}

		bool bChanged = SetRotatorPropertyValueByName(Object, TEXT("AimingRotation"), AimingRotation);
		bChanged |= SetNumericPropertyValueByName(Object, TEXT("AimYawRate"), AimYawRate);
		bChanged |= SetVector2DPropertyValueByName(Object, TEXT("AimingAngle"), AimingAngle);
		return bChanged;
	}

	bool ApplyALSTurnValuesToMesh(USkeletalMeshComponent* Mesh, const FRotator& AimingRotation, const FVector2D& AimingAngle, const double AimYawRate, const bool bApplyLookingDirection)
	{
		if (!Mesh)
		{
			return false;
		}

		bool bChanged = false;
		const TArray<FString> ViewModeAliases = BuildThirdPersonViewModeAliases();
		const TArray<FString> RotationModeAliases = BuildLookingDirectionAliases();

		if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
		{
			bChanged |= ApplyALSTurnValuesToObject(AnimInstance, AimingRotation, AimingAngle, AimYawRate);
			bChanged |= ApplyViewModeToAnimInstance(AnimInstance, ViewModeAliases);
			if (bApplyLookingDirection)
			{
				bChanged |= ApplyLookingDirectionToAnimInstance(AnimInstance, RotationModeAliases);
			}
		}

		const USkeletalMeshComponent* ConstMesh = Mesh;
		for (UAnimInstance* LinkedAnimInstance : ConstMesh->GetLinkedAnimInstances())
		{
			bChanged |= ApplyALSTurnValuesToObject(LinkedAnimInstance, AimingRotation, AimingAngle, AimYawRate);
			bChanged |= ApplyViewModeToAnimInstance(LinkedAnimInstance, ViewModeAliases);
			if (bApplyLookingDirection)
			{
				bChanged |= ApplyLookingDirectionToAnimInstance(LinkedAnimInstance, RotationModeAliases);
			}
		}

		return bChanged;
	}

	void CleanupTurnBridgeStatesIfNeeded()
	{
		if (GALSTurnInPlaceBridgeStates.Num() < 128)
		{
			return;
		}

		for (auto It = GALSTurnInPlaceBridgeStates.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}

	TSubclassOf<UAnimInstance> LoadMPSOverlayAnimClass(const int64 PoseValue, const FString& PoseDisplayName)
	{
		const FString NormalizedPose = PoseDisplayName.Replace(TEXT(" "), TEXT("")).ToUpper();

		const TCHAR* ClassPath = TEXT("/Game/Test/ABP_OverlayPose_Default.ABP_OverlayPose_Default_C");
		if (PoseValue == 1 || NormalizedPose.Contains(TEXT("RIFLE")))
		{
			ClassPath = TEXT("/Game/Test/ABP_Overlay_Rifle.ABP_Overlay_Rifle_C");
		}
		else if (PoseValue == 2 || NormalizedPose.Contains(TEXT("PISTOL1H")))
		{
			ClassPath = TEXT("/Game/Test/ABP_Overlay_Pistol1H.ABP_Overlay_Pistol1H_C");
		}
		else if (PoseValue == 3 || NormalizedPose.Contains(TEXT("PISTOL2H")))
		{
			ClassPath = TEXT("/Game/Test/ABP_Overlay_Pistol2H.ABP_Overlay_Pistol2H_C");
		}
		else if (PoseValue == 4 || NormalizedPose.Contains(TEXT("BOW")))
		{
			ClassPath = TEXT("/Game/Test/ABP_Overlay_Bow.ABP_Overlay_Bow_C");
		}
		else if (PoseValue == 5 || NormalizedPose.Contains(TEXT("TORCH")))
		{
			ClassPath = TEXT("/Game/Test/ABP_Overlay_Torch.ABP_Overlay_Torch_C");
		}
		else if (PoseValue == 6 || NormalizedPose.Contains(TEXT("BINOCULAR")))
		{
			ClassPath = TEXT("/Game/Test/ABP_Overlay_Binoculars.ABP_Overlay_Binoculars_C");
		}
		else if (PoseValue == 7 || NormalizedPose.Contains(TEXT("BOX")))
		{
			ClassPath = TEXT("/Game/Test/ABP_Overlay_Box.ABP_Overlay_Box_C");
		}
		else if (PoseValue == 8 || NormalizedPose.Contains(TEXT("BARREL")))
		{
			ClassPath = TEXT("/Game/Test/ABP_Overlay_Barrel.ABP_Overlay_Barrel_C");
		}
		else if (PoseValue == 9 || NormalizedPose.Contains(TEXT("INJURED")))
		{
			ClassPath = TEXT("/Game/Test/ABP_Overlay_Injured.ABP_Overlay_Injured_C");
		}
		else if (PoseValue == 10 || NormalizedPose.Contains(TEXT("HANDSTIED")))
		{
			ClassPath = TEXT("/Game/Test/ABP_Overlay_HandsTied.ABP_Overlay_HandsTied_C");
		}

		return LoadClass<UAnimInstance>(nullptr, ClassPath);
	}

	AActor* GetActorOwnerFromWorldContextObject(UObject* WorldContextObject)
	{
		if (AActor* Actor = Cast<AActor>(WorldContextObject))
		{
			return Actor;
		}
		return WorldContextObject->GetTypedOuter<AActor>();
	}
	const AActor* GetActorOwnerFromWorldContextObject(const UObject* WorldContextObject)
	{
		if (const AActor* Actor = Cast<const AActor>(WorldContextObject))
		{
			return Actor;
		}
		return WorldContextObject->GetTypedOuter<AActor>();
	}

	bool IsAttachedMuzzleFlashTarget(const UFXSystemAsset* EmitterTemplate, const FName AttachPointName)
	{
		return EmitterTemplate
			&& EmitterTemplate->GetName().Equals(TEXT("NS_MuzzleFlash"), ESearchCase::IgnoreCase)
			&& AttachPointName.IsEqual(TEXT("Muzzle"), ENameCase::IgnoreCase);
	}

	const TCHAR* AttachLocationTypeToString(const EAttachLocation::Type LocationType);

	bool PropertyNameMatches(const FProperty* Property, const TCHAR* ExpectedName)
	{
		if (!Property)
		{
			return false;
		}

		const FString Expected(ExpectedName);
		const FString PropertyName = Property->GetName();
		const bool bMatchesName = PropertyName.Equals(Expected, ESearchCase::IgnoreCase)
			|| PropertyName.StartsWith(Expected + TEXT("_"), ESearchCase::IgnoreCase)
			|| Property->GetAuthoredName().Equals(Expected, ESearchCase::IgnoreCase);
#if WITH_EDITOR
		return bMatchesName || Property->GetDisplayNameText().ToString().Equals(Expected, ESearchCase::IgnoreCase);
#else
		return bMatchesName;
#endif
	}

	const FProperty* FindPropertyByName(const UStruct* Struct, const TCHAR* ExpectedName)
	{
		if (!Struct)
		{
			return nullptr;
		}

		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* Property = *It;
			if (PropertyNameMatches(Property, ExpectedName))
			{
				return Property;
			}
		}

		return nullptr;
	}

	bool AssetPropertyMatches(const FProperty* Property, const void* Container, const UFXSystemAsset* EmitterTemplate)
	{
		if (!Property || !Container || !EmitterTemplate)
		{
			return false;
		}

		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			const UObject* Value = ObjectProperty->GetObjectPropertyValue_InContainer(Container);
			return Value == EmitterTemplate
				|| (Value && Value->GetPathName().Equals(EmitterTemplate->GetPathName(), ESearchCase::IgnoreCase));
		}

		if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
		{
			const FSoftObjectPtr* Value = SoftObjectProperty->ContainerPtrToValuePtr<FSoftObjectPtr>(Container);
			if (!Value)
			{
				return false;
			}

			const FSoftObjectPath ValuePath = Value->ToSoftObjectPath();
			const FSoftObjectPath TemplatePath(EmitterTemplate);
			return ValuePath == TemplatePath
				|| ValuePath.GetAssetPathString().Equals(TemplatePath.GetAssetPathString(), ESearchCase::IgnoreCase);
		}

		return false;
	}

	bool TryGetAttachedParticleScaleFromWeaponTable(const UFXSystemAsset* EmitterTemplate, FVector& OutScale, FName& OutRowName)
	{
		static const TCHAR* WeaponTablePath = TEXT("/Game/MP_System_V3/Game/Blueprints/DataTables/DT_Weapons.DT_Weapons");

		UDataTable* WeaponTable = LoadObject<UDataTable>(nullptr, WeaponTablePath);
		if (!WeaponTable || !WeaponTable->GetRowStruct())
		{
			return false;
		}

		const FStructProperty* FeaturesProperty = CastField<FStructProperty>(FindPropertyByName(WeaponTable->GetRowStruct(), TEXT("Features")));
		if (!FeaturesProperty || !FeaturesProperty->Struct)
		{
			return false;
		}

		const FProperty* AttachedParticleProperty = FindPropertyByName(FeaturesProperty->Struct, TEXT("Attached Particle"));
		const FStructProperty* AttachedOffsetProperty = CastField<FStructProperty>(FindPropertyByName(FeaturesProperty->Struct, TEXT("Attached offset")));
		if (!AttachedParticleProperty || !AttachedOffsetProperty || AttachedOffsetProperty->Struct != TBaseStructure<FTransform>::Get())
		{
			return false;
		}

		for (const TPair<FName, uint8*>& RowPair : WeaponTable->GetRowMap())
		{
			if (!RowPair.Value)
			{
				continue;
			}

			const void* FeaturesValue = FeaturesProperty->ContainerPtrToValuePtr<void>(RowPair.Value);
			if (!AssetPropertyMatches(AttachedParticleProperty, FeaturesValue, EmitterTemplate))
			{
				continue;
			}

			const FTransform* AttachedOffset = AttachedOffsetProperty->ContainerPtrToValuePtr<FTransform>(FeaturesValue);
			if (!AttachedOffset)
			{
				return false;
			}

			OutScale = AttachedOffset->GetScale3D();
			OutRowName = RowPair.Key;
			return !OutScale.IsNearlyZero();
		}

		return false;
	}

	void ApplyAttachedMuzzleFlashScale(
		const UFXSystemAsset* EmitterTemplate,
		const FName AttachPointName,
		FVector& Scale)
	{
		if (!IsAttachedMuzzleFlashTarget(EmitterTemplate, AttachPointName) || !Scale.Equals(FVector(1.0f), KINDA_SMALL_NUMBER))
		{
			return;
		}

		FVector TableScale = FVector::OneVector;
		FName RowName = NAME_None;
		if (!TryGetAttachedParticleScaleFromWeaponTable(EmitterTemplate, TableScale, RowName))
		{
			return;
		}

		UE_LOG(
			LogTouchMeRuntimeTrace,
			Display,
			TEXT("[MuzzleFXScaleFix] Asset=%s Row=%s IncomingScale=%s -> TableAttachedScale=%s"),
			*EmitterTemplate->GetPathName(),
			*RowName.ToString(),
			*Scale.ToCompactString(),
			*TableScale.ToCompactString());

		Scale = TableScale;
	}

	void FixWorldSpaceMuzzleFlashOffset(
		const UFXSystemAsset* EmitterTemplate,
		const USceneComponent* AttachToComponent,
		const FName AttachPointName,
		FVector& Location,
		FRotator& Rotation,
		const EAttachLocation::Type LocationType)
	{
		if (!IsAttachedMuzzleFlashTarget(EmitterTemplate, AttachPointName) || !AttachToComponent)
		{
			return;
		}

		if (LocationType != EAttachLocation::SnapToTarget && LocationType != EAttachLocation::SnapToTargetIncludingScale)
		{
			return;
		}

		if (!AttachToComponent->DoesSocketExist(AttachPointName))
		{
			return;
		}

		const FTransform SocketWorldTransform = AttachToComponent->GetSocketTransform(AttachPointName, RTS_World);
		const FVector SocketWorldLocation = SocketWorldTransform.GetLocation();

		UE_LOG(
			LogTouchMeRuntimeTrace,
			Display,
			TEXT("[MuzzleFXAttachFix] Asset=%s AttachTo=%s AttachPoint=%s IncomingLoc=%s IncomingRot=%s SocketWorldLoc=%s SocketWorldRot=%s LocationType=%s -> RelativeLoc=Zero RelativeRot=Zero"),
			*EmitterTemplate->GetPathName(),
			*AttachToComponent->GetPathName(),
			*AttachPointName.ToString(),
			*Location.ToCompactString(),
			*Rotation.ToCompactString(),
			*SocketWorldLocation.ToCompactString(),
			*SocketWorldTransform.Rotator().ToCompactString(),
			AttachLocationTypeToString(LocationType));

		Location = FVector::ZeroVector;
		Rotation = FRotator::ZeroRotator;
	}

	const TCHAR* AttachLocationTypeToString(const EAttachLocation::Type LocationType)
	{
		switch (LocationType)
		{
		case EAttachLocation::KeepRelativeOffset:
			return TEXT("KeepRelativeOffset");
		case EAttachLocation::KeepWorldPosition:
			return TEXT("KeepWorldPosition");
		case EAttachLocation::SnapToTarget:
			return TEXT("SnapToTarget");
		case EAttachLocation::SnapToTargetIncludingScale:
			return TEXT("SnapToTargetIncludingScale");
		default:
			return TEXT("Unknown");
		}
	}

	bool IsGrenadeExplosionFXAsset(const UFXSystemAsset* EmitterTemplate)
	{
		if (!EmitterTemplate)
		{
			return false;
		}

		const FString AssetPath = EmitterTemplate->GetPathName();
		return AssetPath.Contains(TEXT("GrenadeEXP"), ESearchCase::IgnoreCase)
			|| AssetPath.Contains(TEXT("LTGrenadeEXP"), ESearchCase::IgnoreCase)
			|| AssetPath.Contains(TEXT("Explosion_Grenade"), ESearchCase::IgnoreCase)
			|| AssetPath.Contains(TEXT("Explosion_GrenadeLauncher"), ESearchCase::IgnoreCase)
			|| AssetPath.Contains(TEXT("P_Explosion_Grenade"), ESearchCase::IgnoreCase)
			|| AssetPath.Contains(TEXT("NS_Explosion_Frag"), ESearchCase::IgnoreCase)
			|| AssetPath.Contains(TEXT("NS_Explosion_GrenadeLauncher"), ESearchCase::IgnoreCase);
	}

	void TryApplyFoliageImpulseForFX(
		const UObject* WorldContextObject,
		const UFXSystemAsset* EmitterTemplate,
		const FVector& Location)
	{
		if (!IsGrenadeExplosionFXAsset(EmitterTemplate) || !GEngine)
		{
			return;
		}

		UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
		if (!World)
		{
			return;
		}

		if (UTMFoliageImpulseSubsystem* FoliageImpulseSubsystem = World->GetSubsystem<UTMFoliageImpulseSubsystem>())
		{
			FoliageImpulseSubsystem->AddDefaultRadialImpulse(Location);
		}
	}

}

//////////////////////////////////////////////////////////////////////////
// UAtomGameplayStatics

UTMGameplayStatics::UTMGameplayStatics(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UTMGameplayStatics::PlaySoundAtLocationDistanced(
	const UObject* WorldContextObject,
	USoundBase* Sound,
	FVector Location,
	FRotator Rotation,
	float VolumeMultiplier,
	float PitchMultiplier,
	float StartTime,
	USoundAttenuation* AttenuationSettings,
	USoundConcurrency* ConcurrencySettings,
	const AActor* OwningActor,
	const UInitialActiveSoundParams* InitialParams)
{
	QUICK_SCOPE_CYCLE_COUNTER(UTMGameplayStatics_PlaySoundAtLocationDistanced);

	if (!Sound || !GEngine || !GEngine->UseSound())
	{
		return;
	}

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (!World || !World->bAllowAudioPlayback || World->IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	FVector ListenerLocation = FVector::ZeroVector;

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(WorldContextObject, 0))
	{
		FVector ViewLoc;
		FRotator ViewRot;
		PC->GetPlayerViewPoint(ViewLoc, ViewRot);
		ListenerLocation = ViewLoc;
	}
	else
	{
		ListenerLocation = Location;
	}

	const float DistanceCm = FVector::Distance(ListenerLocation, Location);
	float DelaySeconds = DistanceCm / 34300.f;
	DelaySeconds = FMath::Clamp(DelaySeconds, 0.f, 1.5f);

	auto PlayNow = [=]()
		{
			if (!World) return;

			if (FAudioDeviceHandle AudioDevice = World->GetAudioDevice())
			{
				TArray<FAudioParameter> Params;
				if (InitialParams)
				{
					Params.Append(InitialParams->AudioParams);
				}

				const AActor* ActiveSoundOwner =
					OwningActor ? OwningActor : TMGameplayStatics::GetActorOwnerFromWorldContextObject(WorldContextObject);

				UActorSoundParameterInterface::Fill(ActiveSoundOwner, Params);

				AudioDevice->PlaySoundAtLocation(
					Sound, World,
					VolumeMultiplier, PitchMultiplier, StartTime,
					Location, Rotation,
					AttenuationSettings, ConcurrencySettings,
					MoveTemp(Params),
					ActiveSoundOwner);
			}
		};

	if (DelaySeconds <= KINDA_SMALL_NUMBER)
	{
		PlayNow();
		return;
	}

	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(PlayNow), DelaySeconds, false);
}

UAudioComponent* UTMGameplayStatics::SpawnSoundAtLocationDistanced(const UObject* WorldContextObject, USoundBase* Sound, FVector Location, FRotator Rotation, float VolumeMultiplier, float PitchMultiplier, float StartTime, USoundAttenuation* AttenuationSettings, USoundConcurrency* ConcurrencySettings, bool bAutoDestroy)
{
	QUICK_SCOPE_CYCLE_COUNTER(UAtomGameplayStatics_SpawnSoundAtLocation);

	if (!Sound || !GEngine || !GEngine->UseSound())
	{
		return nullptr;
	}

	UWorld* ThisWorld = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (!ThisWorld || !ThisWorld->bAllowAudioPlayback || ThisWorld->IsNetMode(NM_DedicatedServer))
	{
		return nullptr;
	}

	const bool bIsInGameWorld = ThisWorld->IsGameWorld();

	// Derive an owner from the WorldContextObject
	AActor* WorldContextOwner = TMGameplayStatics::GetActorOwnerFromWorldContextObject(const_cast<UObject*>(WorldContextObject));

	FAudioDevice::FCreateComponentParams Params(ThisWorld, WorldContextOwner);
	Params.SetLocation(Location);
	Params.AttenuationSettings = AttenuationSettings;
	
	if (ConcurrencySettings)
	{
		Params.ConcurrencySet.Add(ConcurrencySettings);
	}

	UAudioComponent* AudioComponent = FAudioDevice::CreateComponent(Sound, Params);

	if (AudioComponent)
	{
		AudioComponent->SetWorldLocationAndRotation(Location, Rotation);
		AudioComponent->SetVolumeMultiplier(VolumeMultiplier);
		AudioComponent->SetPitchMultiplier(PitchMultiplier);
		AudioComponent->bAllowSpatialization	= Params.ShouldUseAttenuation();
		AudioComponent->bIsUISound				= !bIsInGameWorld;
		AudioComponent->bAutoDestroy			= bAutoDestroy;
		AudioComponent->SubtitlePriority		= Sound->GetSubtitlePriority();
		AudioComponent->bStopWhenOwnerDestroyed = false;
		AudioComponent->Play(StartTime);
	}

	return AudioComponent;
}

UFXSystemComponent* UTMGameplayStatics::SpawnFXSystemAtLocation(
	const UObject* WorldContextObject,
	UFXSystemAsset* EmitterTemplate,
	FVector Location,
	FRotator Rotation,
	FVector Scale,
	bool bAutoDestroy,
	EPSCPoolMethod PoolingMethod,
	bool bAutoActivateSystem)
{
	if (!EmitterTemplate)
	{
		return nullptr;
	}

	if (UParticleSystem* CascadeSystem = Cast<UParticleSystem>(EmitterTemplate))
	{
		UParticleSystemComponent* Component = UGameplayStatics::SpawnEmitterAtLocation(
			WorldContextObject,
			CascadeSystem,
			Location,
			Rotation,
			Scale,
			bAutoDestroy,
			PoolingMethod,
			bAutoActivateSystem);
		TMGameplayStatics::TryApplyFoliageImpulseForFX(WorldContextObject, EmitterTemplate, Location);
		return Component;
	}

	if (UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(EmitterTemplate))
	{
		UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			WorldContextObject,
			NiagaraSystem,
			Location,
			Rotation,
			Scale,
			bAutoDestroy,
			bAutoActivateSystem,
			ToNiagaraPooling(PoolingMethod));
		TMGameplayStatics::TryApplyFoliageImpulseForFX(WorldContextObject, EmitterTemplate, Location);
		return Component;
	}

	return nullptr;
}

UFXSystemComponent* UTMGameplayStatics::SpawnFXSystemAttached(
	UFXSystemAsset* EmitterTemplate,
	USceneComponent* AttachToComponent,
	FName AttachPointName,
	FVector Location,
	FRotator Rotation,
	FVector Scale,
	EAttachLocation::Type LocationType,
	bool bAutoDestroy,
	EPSCPoolMethod PoolingMethod,
	bool bAutoActivate)
{
	if (!EmitterTemplate || !AttachToComponent)
	{
		return nullptr;
	}

	TMGameplayStatics::FixWorldSpaceMuzzleFlashOffset(EmitterTemplate, AttachToComponent, AttachPointName, Location, Rotation, LocationType);
	TMGameplayStatics::ApplyAttachedMuzzleFlashScale(EmitterTemplate, AttachPointName, Scale);

	if (UParticleSystem* CascadeSystem = Cast<UParticleSystem>(EmitterTemplate))
	{
		UParticleSystemComponent* Component = UGameplayStatics::SpawnEmitterAttached(
			CascadeSystem,
			AttachToComponent,
			AttachPointName,
			Location,
			Rotation,
			Scale,
			LocationType,
			bAutoDestroy,
			PoolingMethod,
			bAutoActivate);
		return Component;
	}

	if (UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(EmitterTemplate))
	{
		UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAttached(
			NiagaraSystem,
			AttachToComponent,
			AttachPointName,
			Location,
			Rotation,
			Scale,
			LocationType,
			bAutoDestroy,
			ToNiagaraPooling(PoolingMethod),
			bAutoActivate);
		return Component;
	}

	return nullptr;
}

void UTMGameplayStatics::ApplyRadialFoliageImpulse(
	const UObject* WorldContextObject,
	const FVector Origin,
	const float Radius,
	const float ImpulseStrength,
	const float Duration)
{
	if (!WorldContextObject || !GEngine)
	{
		return;
	}

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World)
	{
		return;
	}

	if (UTMFoliageImpulseSubsystem* FoliageImpulseSubsystem = World->GetSubsystem<UTMFoliageImpulseSubsystem>())
	{
		FoliageImpulseSubsystem->AddRadialImpulse(Origin, Radius, ImpulseStrength, Duration);
	}
}

ATMFoliageExplosionCollisionTester* UTMGameplayStatics::SpawnFoliageExplosionCollisionTester(
	const UObject* WorldContextObject,
	const FVector Origin,
	const float Radius,
	const float Strength,
	const FVector PullDirection,
	const float BendDistance,
	const float ExpansionDuration,
	const bool bAutoDestroyAfterExpansion)
{
	if (!WorldContextObject || !GEngine)
	{
		return nullptr;
	}

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = MakeUniqueObjectName(World, ATMFoliageExplosionCollisionTester::StaticClass(), TEXT("TM_FoliageExplosionCollisionTester"));
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ATMFoliageExplosionCollisionTester* Tester = World->SpawnActor<ATMFoliageExplosionCollisionTester>(
		ATMFoliageExplosionCollisionTester::StaticClass(),
		Origin,
		FRotator::ZeroRotator,
		SpawnParameters);
	if (!Tester)
	{
		return nullptr;
	}

	Tester->Radius = FMath::Max(1.f, Radius);
	Tester->Strength = FMath::Max(0.f, Strength);
	Tester->PullDirection = PullDirection;
	Tester->BendDistance = FMath::Max(0.f, BendDistance);
	Tester->ExpansionDuration = FMath::Max(0.f, ExpansionDuration);
	Tester->bAutoDestroyAfterExpansion = bAutoDestroyAfterExpansion;
	Tester->RefreshAffectedFoliage();
	return Tester;
}

ATMFoliageCollisionPushTester* UTMGameplayStatics::SpawnFoliageCollisionPushTester(
	const UObject* WorldContextObject,
	const FVector Origin,
	const float Radius,
	const float ExpansionDuration,
	const bool bAutoDestroyAfterExpansion,
	const bool bCreatePhysicsProxyBodies)
{
	if (!WorldContextObject || !GEngine)
	{
		return nullptr;
	}

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = MakeUniqueObjectName(World, ATMFoliageCollisionPushTester::StaticClass(), TEXT("TM_FoliageCollisionPushTester"));
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ATMFoliageCollisionPushTester* Tester = World->SpawnActor<ATMFoliageCollisionPushTester>(
		ATMFoliageCollisionPushTester::StaticClass(),
		Origin,
		FRotator::ZeroRotator,
		SpawnParameters);
	if (!Tester)
	{
		return nullptr;
	}

	Tester->Radius = FMath::Max(1.f, Radius);
	Tester->ExpansionDuration = FMath::Max(0.f, ExpansionDuration);
	Tester->bAutoDestroyAfterExpansion = bAutoDestroyAfterExpansion;
	Tester->bCreatePhysicsProxyBodies = bCreatePhysicsProxyBodies;
	if (bCreatePhysicsProxyBodies)
	{
		Tester->RebuildProxyBodies();
	}

	return Tester;
}

void UTMGameplayStatics::PlayWeaponSpawnFeedbackForActor(AActor* WeaponActor)
{
	AGun* Gun = Cast<AGun>(WeaponActor);
	if (!Gun)
	{
		return;
	}

	Gun->PlayWeaponSpawnFeedback();
}

void UTMGameplayStatics::LogLoadoutPreviewOffsetApplied(
	AActor* WeaponActor,
	USceneComponent* TargetComponent,
	FVector AppliedViewOffset)
{
	const FString WeaponName = IsValid(WeaponActor)
		? WeaponActor->GetName()
		: FString(TEXT("None"));
	const FString WeaponPath = IsValid(WeaponActor)
		? WeaponActor->GetPathName()
		: FString(TEXT("None"));
	const FString ComponentName = IsValid(TargetComponent)
		? TargetComponent->GetName()
		: FString(TEXT("None"));
	const FString ComponentPath = IsValid(TargetComponent)
		? TargetComponent->GetPathName()
		: FString(TEXT("None"));

	const FString ComponentWorldLocation = IsValid(TargetComponent)
		? TargetComponent->GetComponentLocation().ToString()
		: FString(TEXT("None"));
	const FString ComponentRelativeLocation = IsValid(TargetComponent)
		? TargetComponent->GetRelativeLocation().ToString()
		: FString(TEXT("None"));
	const FString ComponentWorldTransform = IsValid(TargetComponent)
		? TargetComponent->GetComponentTransform().ToHumanReadableString()
		: FString(TEXT("None"));
	const FString ActorLocation = IsValid(WeaponActor)
		? WeaponActor->GetActorLocation().ToString()
		: FString(TEXT("None"));

	UE_LOG(
		LogTemp,
		Display,
		TEXT("[TMLoadoutOffsetApply] Applied loadout preview offset. Weapon=%s Path=%s Component=%s ComponentPath=%s AppliedDT_ViewOffset=%s ComponentWorldLocation=%s ComponentRelativeLocation=%s ComponentWorldTransform=%s ActorLocation=%s"),
		*WeaponName,
		*WeaponPath,
		*ComponentName,
		*ComponentPath,
		*AppliedViewOffset.ToString(),
		*ComponentWorldLocation,
		*ComponentRelativeLocation,
		*ComponentWorldTransform,
		*ActorLocation);

	if (!IsValid(WeaponActor) || !IsValid(TargetComponent) || TargetComponent->GetOwner() != WeaponActor)
	{
		return;
	}

	if (TargetComponent->GetFName() != TEXT("Item"))
	{
		return;
	}

	static const FName TransformatorName(TEXT("ActiveWeaponTransformator"));
	AActor* Transformator = nullptr;
	if (UWorld* World = WeaponActor->GetWorld())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Candidate = *It;
			if (!IsValid(Candidate) || Candidate == WeaponActor)
			{
				continue;
			}

			const bool bHasTransformatorTag = Candidate->ActorHasTag(TransformatorName);
			const bool bHasTransformatorName = Candidate->GetFName() == TransformatorName
				|| Candidate->GetName().Contains(TransformatorName.ToString());
#if WITH_EDITOR
			const bool bHasTransformatorLabel = Candidate->GetActorLabel() == TransformatorName.ToString();
#else
			const bool bHasTransformatorLabel = false;
#endif
			if (bHasTransformatorTag || bHasTransformatorName || bHasTransformatorLabel)
			{
				Transformator = Candidate;
				break;
			}
		}
	}

	if (!IsValid(Transformator))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[TMLoadoutTransformator] Cannot snap Item after DT_ViewOffset; transformator not found for Weapon=%s Component=%s."),
			*WeaponActor->GetName(),
			*TargetComponent->GetName());
		return;
	}

	TargetComponent->SetWorldLocationAndRotation(
		Transformator->GetActorLocation(),
		Transformator->GetActorRotation(),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);

	UE_LOG(
		LogTemp,
		Display,
		TEXT("[TMLoadoutTransformator] Snapped visible Item to transformator after DT_ViewOffset. Weapon=%s Component=%s ComponentWorldLocation=%s ComponentWorldRotation=%s TransformatorLocation=%s TransformatorRotation=%s"),
		*WeaponActor->GetName(),
		*TargetComponent->GetName(),
		*TargetComponent->GetComponentLocation().ToString(),
		*TargetComponent->GetComponentRotation().ToString(),
		*Transformator->GetActorLocation().ToString(),
		*Transformator->GetActorRotation().ToString());
}

namespace
{
	bool TMIsLoadoutPreviewWidget(UUserWidget* Widget)
	{
		if (!Widget)
		{
			return false;
		}

		const UClass* WidgetClass = Widget->GetClass();
		return WidgetClass
			&& WidgetClass->GetPathName().Contains(TEXT("W_Loadout"))
			&& FindFProperty<FObjectPropertyBase>(WidgetClass, TEXT("ActiveWeapon")) != nullptr;
	}

	void TMCollectLoadoutPreviewWidgets(UUserWidget* OwnerWidget, TArray<UUserWidget*>& OutWidgets)
	{
		if (!OwnerWidget)
		{
			return;
		}

		if (TMIsLoadoutPreviewWidget(OwnerWidget))
		{
			OutWidgets.AddUnique(OwnerWidget);
		}

		static const FName LoadoutWidgetNames[] =
		{
			TEXT("W_Loadout_C_0"),
			TEXT("W_Loadout")
		};

		for (const FName LoadoutWidgetName : LoadoutWidgetNames)
		{
			if (UUserWidget* LoadoutWidget = Cast<UUserWidget>(OwnerWidget->GetWidgetFromName(LoadoutWidgetName)))
			{
				if (TMIsLoadoutPreviewWidget(LoadoutWidget))
				{
					OutWidgets.AddUnique(LoadoutWidget);
				}
			}
		}

		if (!OwnerWidget->WidgetTree)
		{
			return;
		}

		OwnerWidget->WidgetTree->ForEachWidget(
			[&OutWidgets](UWidget* Widget)
			{
				UUserWidget* UserWidget = Cast<UUserWidget>(Widget);
				if (TMIsLoadoutPreviewWidget(UserWidget))
				{
					OutWidgets.AddUnique(UserWidget);
				}
			});

		UWorld* OwnerWorld = OwnerWidget->GetWorld();
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			UUserWidget* CandidateWidget = *It;
			if (!CandidateWidget || CandidateWidget->HasAnyFlags(RF_ClassDefaultObject))
			{
				continue;
			}

			if (OwnerWorld && CandidateWidget->GetWorld() != OwnerWorld)
			{
				continue;
			}

			if (TMIsLoadoutPreviewWidget(CandidateWidget))
			{
				OutWidgets.AddUnique(CandidateWidget);
			}
		}
	}

	void TMCleanupLoadoutPreviewWidget(UUserWidget* LoadoutWidget)
	{
		if (!LoadoutWidget)
		{
			return;
		}

		FObjectPropertyBase* ActiveWeaponProperty =
			FindFProperty<FObjectPropertyBase>(LoadoutWidget->GetClass(), TEXT("ActiveWeapon"));
		if (!ActiveWeaponProperty)
		{
			return;
		}

		AActor* ActiveWeapon = Cast<AActor>(ActiveWeaponProperty->GetObjectPropertyValue_InContainer(LoadoutWidget));
		if (IsValid(ActiveWeapon) && !ActiveWeapon->IsActorBeingDestroyed())
		{
			ActiveWeapon->SetActorHiddenInGame(true);
			ActiveWeapon->SetActorEnableCollision(false);
			TArray<UPrimitiveComponent*> PrimitiveComponents;
			ActiveWeapon->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
			for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
			{
				if (!PrimitiveComponent)
				{
					continue;
				}

				PrimitiveComponent->SetVisibility(false, true);
				PrimitiveComponent->SetHiddenInGame(true, true);
				PrimitiveComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
			ActiveWeapon->Destroy();
		}

		ActiveWeaponProperty->SetObjectPropertyValue_InContainer(LoadoutWidget, nullptr);

		if (FBoolProperty* IsViewerProperty = FindFProperty<FBoolProperty>(LoadoutWidget->GetClass(), TEXT("IsViewer?")))
		{
			IsViewerProperty->SetPropertyValue_InContainer(LoadoutWidget, false);
		}
	}

	constexpr float TMLoadoutPreviewShootCooldownSeconds = 1.5f;
	constexpr float TMLoadoutPreviewShootTraceDistance = 100000.0f;
	constexpr float TMLoadoutPreviewShootOcclusionTolerance = 1.0f;
	const FName TMLoadoutActiveWeaponPropertyName(TEXT("ActiveWeapon"));

	struct FTMLoadoutPreviewShootState
	{
		TWeakObjectPtr<UUserWidget> LoadoutWidget;
		double NextAllowedShootTime = 0.0;
	};

	TArray<FTMLoadoutPreviewShootState> TMLoadoutPreviewShootStates;
	FTSTicker::FDelegateHandle TMLoadoutPreviewShootTickerHandle;
	bool bTMLoadoutPreviewLeftMouseWasDown = false;

	bool TMIsLoadoutPreviewLeftMouseDown(const APlayerController* PlayerController)
	{
		if (PlayerController && PlayerController->IsInputKeyDown(EKeys::LeftMouseButton))
		{
			return true;
		}

		return FSlateApplication::IsInitialized()
			&& FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
	}

	AActor* TMGetLoadoutActiveWeapon(UUserWidget* LoadoutWidget)
	{
		if (!LoadoutWidget)
		{
			return nullptr;
		}

		FObjectPropertyBase* ActiveWeaponProperty =
			FindFProperty<FObjectPropertyBase>(LoadoutWidget->GetClass(), TMLoadoutActiveWeaponPropertyName);
		return ActiveWeaponProperty
			? Cast<AActor>(ActiveWeaponProperty->GetObjectPropertyValue_InContainer(LoadoutWidget))
			: nullptr;
	}

	bool TMIsWidgetVisibleForLoadoutPreviewShoot(const UUserWidget* Widget)
	{
		return Widget
			&& !Widget->HasAnyFlags(RF_ClassDefaultObject)
			&& Widget->GetWorld()
			&& Widget->IsVisible();
	}

	bool TMIsWidgetPaintedForLoadoutPreviewShoot(const UWidget* Widget)
	{
		if (!Widget || !Widget->IsVisible())
		{
			return false;
		}

		const FVector2D AbsoluteSize = Widget->GetCachedGeometry().GetAbsoluteSize();
		return AbsoluteSize.X > 1.0f && AbsoluteSize.Y > 1.0f;
	}

	bool TMIsLoadoutAttachmentsWidgetVisible(const UUserWidget* LoadoutWidget)
	{
		if (!LoadoutWidget)
		{
			return false;
		}

		UWorld* LoadoutWorld = LoadoutWidget->GetWorld();
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			UUserWidget* CandidateWidget = *It;
			if (!CandidateWidget
				|| CandidateWidget == LoadoutWidget
				|| CandidateWidget->HasAnyFlags(RF_ClassDefaultObject))
			{
				continue;
			}

			if (LoadoutWorld && CandidateWidget->GetWorld() != LoadoutWorld)
			{
				continue;
			}

			const UClass* CandidateClass = CandidateWidget->GetClass();
			if (CandidateClass
				&& CandidateClass->GetPathName().Contains(TEXT("W_Attachments"))
				&& TMIsWidgetPaintedForLoadoutPreviewShoot(CandidateWidget))
			{
				return true;
			}
		}

		return false;
	}

	APlayerController* TMResolveLoadoutPlayerController(UUserWidget* LoadoutWidget)
	{
		if (!LoadoutWidget)
		{
			return nullptr;
		}

		if (APlayerController* OwningPlayer = LoadoutWidget->GetOwningPlayer())
		{
			return OwningPlayer;
		}

		return UGameplayStatics::GetPlayerController(LoadoutWidget, 0);
	}

	bool TMIsActorRelatedToLoadoutWeapon(const AActor* CandidateActor, const AActor* WeaponActor)
	{
		if (!CandidateActor || !WeaponActor)
		{
			return false;
		}

		for (const AActor* Actor = CandidateActor; Actor; Actor = Actor->GetAttachParentActor())
		{
			if (Actor == WeaponActor)
			{
				return true;
			}
		}

		for (const AActor* Actor = CandidateActor; Actor; Actor = Actor->GetOwner())
		{
			if (Actor == WeaponActor)
			{
				return true;
			}
		}

		return false;
	}

	bool TMIsHitPartOfLoadoutWeapon(const FHitResult& HitResult, const AActor* WeaponActor)
	{
		if (!WeaponActor)
		{
			return false;
		}

		if (TMIsActorRelatedToLoadoutWeapon(HitResult.GetActor(), WeaponActor))
		{
			return true;
		}

		const UPrimitiveComponent* HitComponent = HitResult.GetComponent();
		if (HitComponent && TMIsActorRelatedToLoadoutWeapon(HitComponent->GetOwner(), WeaponActor))
		{
			return true;
		}

		for (const USceneComponent* SceneComponent = HitComponent; SceneComponent; SceneComponent = SceneComponent->GetAttachParent())
		{
			if (TMIsActorRelatedToLoadoutWeapon(SceneComponent->GetOwner(), WeaponActor))
			{
				return true;
			}
		}

		return false;
	}

	float TMResolveTraceHitDistance(const FHitResult& HitResult, const FVector& TraceStart)
	{
		if (HitResult.Distance > KINDA_SMALL_NUMBER)
		{
			return HitResult.Distance;
		}

		const FVector HitLocation = HitResult.ImpactPoint.IsNearlyZero()
			? HitResult.Location
			: HitResult.ImpactPoint;
		return FVector::Dist(TraceStart, HitLocation);
	}

	bool TMLineBoxIntersection(
		const FBox& Box,
		const FVector& TraceStart,
		const FVector& TraceEnd,
		float& OutTime)
	{
		OutTime = 0.0f;
		if (!Box.IsValid)
		{
			return false;
		}

		const FVector Segment = TraceEnd - TraceStart;
		float MinTime = 0.0f;
		float MaxTime = 1.0f;

		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const float Start = TraceStart[Axis];
			const float Direction = Segment[Axis];
			const float Min = Box.Min[Axis];
			const float Max = Box.Max[Axis];

			if (FMath::IsNearlyZero(Direction))
			{
				if (Start < Min || Start > Max)
				{
					return false;
				}

				continue;
			}

			float AxisMinTime = (Min - Start) / Direction;
			float AxisMaxTime = (Max - Start) / Direction;
			if (AxisMinTime > AxisMaxTime)
			{
				Swap(AxisMinTime, AxisMaxTime);
			}

			MinTime = FMath::Max(MinTime, AxisMinTime);
			MaxTime = FMath::Min(MaxTime, AxisMaxTime);
			if (MinTime > MaxTime)
			{
				return false;
			}
		}

		OutTime = MinTime;
		return true;
	}

	bool TMTraceLoadoutWeaponComponents(
		AGun* Gun,
		const FVector& TraceStart,
		const FVector& TraceEnd,
		FHitResult& OutHitResult)
	{
		if (!IsValid(Gun) || Gun->IsHidden())
		{
			return false;
		}

		bool bFoundHit = false;
		float BestDistance = TNumericLimits<float>::Max();
		FCollisionQueryParams ComponentTraceParams(SCENE_QUERY_STAT(TMLoadoutPreviewShoot_ComponentTrace), true);
		ComponentTraceParams.bReturnPhysicalMaterial = true;

		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(Gun);
		for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			if (!IsValid(PrimitiveComponent)
				|| !PrimitiveComponent->IsRegistered()
				|| !PrimitiveComponent->IsVisible())
			{
				continue;
			}

			FHitResult ComponentHitResult;
			if (!PrimitiveComponent->LineTraceComponent(
				ComponentHitResult,
				TraceStart,
				TraceEnd,
				ComponentTraceParams))
			{
				continue;
			}

			ComponentHitResult.Component = PrimitiveComponent;
			const float HitDistance = TMResolveTraceHitDistance(ComponentHitResult, TraceStart);
			if (HitDistance < BestDistance)
			{
				BestDistance = HitDistance;
				OutHitResult = ComponentHitResult;
				bFoundHit = true;
			}
		}

		if (!bFoundHit)
		{
			const float SegmentLength = FVector::Dist(TraceStart, TraceEnd);
			for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
			{
				if (!IsValid(PrimitiveComponent)
					|| !PrimitiveComponent->IsRegistered()
					|| !PrimitiveComponent->IsVisible())
				{
					continue;
				}

				float HitTime = 0.0f;
				if (!TMLineBoxIntersection(
					PrimitiveComponent->Bounds.GetBox(),
					TraceStart,
					TraceEnd,
					HitTime))
				{
					continue;
				}

				const float HitDistance = SegmentLength * HitTime;
				if (HitDistance < BestDistance)
				{
					BestDistance = HitDistance;
					OutHitResult = FHitResult();
					OutHitResult.Component = PrimitiveComponent;
					OutHitResult.Distance = HitDistance;
					OutHitResult.Location = FMath::Lerp(TraceStart, TraceEnd, HitTime);
					OutHitResult.ImpactPoint = OutHitResult.Location;
					OutHitResult.bBlockingHit = true;
					bFoundHit = true;
				}
			}
		}

		return bFoundHit;
	}

	bool TMTraceLoadoutPreviewWeaponUnderCursor(APlayerController* PlayerController, AGun* Gun)
	{
		if (!PlayerController || !IsValid(Gun))
		{
			return false;
		}

		UWorld* World = Gun->GetWorld();
		if (!World)
		{
			return false;
		}

		FVector CursorWorldLocation;
		FVector CursorWorldDirection;
		if (!PlayerController->DeprojectMousePositionToWorld(CursorWorldLocation, CursorWorldDirection))
		{
			return false;
		}

		const FVector TraceDirection = CursorWorldDirection.GetSafeNormal();
		if (TraceDirection.IsNearlyZero())
		{
			return false;
		}

		const FVector TraceEnd = CursorWorldLocation + TraceDirection * TMLoadoutPreviewShootTraceDistance;
		FCollisionQueryParams WorldTraceParams(SCENE_QUERY_STAT(TMLoadoutPreviewShoot_WorldTrace), true);
		WorldTraceParams.bReturnPhysicalMaterial = true;

		FHitResult WorldHitResult;
		const bool bWorldHit = World->LineTraceSingleByChannel(
			WorldHitResult,
			CursorWorldLocation,
			TraceEnd,
			ECC_Visibility,
			WorldTraceParams);
		if (bWorldHit && TMIsHitPartOfLoadoutWeapon(WorldHitResult, Gun))
		{
			return true;
		}

		FHitResult ComponentHitResult;
		if (!TMTraceLoadoutWeaponComponents(Gun, CursorWorldLocation, TraceEnd, ComponentHitResult))
		{
			return false;
		}

		if (bWorldHit && !TMIsHitPartOfLoadoutWeapon(WorldHitResult, Gun))
		{
			const float WorldHitDistance = TMResolveTraceHitDistance(WorldHitResult, CursorWorldLocation);
			const float ComponentHitDistance = TMResolveTraceHitDistance(ComponentHitResult, CursorWorldLocation);
			if (WorldHitDistance + TMLoadoutPreviewShootOcclusionTolerance < ComponentHitDistance)
			{
				return false;
			}
		}

		return true;
	}

	bool TMInvokeLoadoutPreviewShoot(AGun* Gun, APlayerController* PlayerController)
	{
		return IsValid(Gun) && Gun->TriggerLoadoutShoot(PlayerController);
	}

	bool TMTryLoadoutPreviewShoot(FTMLoadoutPreviewShootState& State, const bool bClickJustPressed)
	{
		UUserWidget* LoadoutWidget = State.LoadoutWidget.Get();
		if (!TMIsWidgetVisibleForLoadoutPreviewShoot(LoadoutWidget)
			|| !TMIsLoadoutPreviewWidget(LoadoutWidget))
		{
			return false;
		}

		AGun* Gun = Cast<AGun>(TMGetLoadoutActiveWeapon(LoadoutWidget));
		if (!IsValid(Gun) || !Gun->CanLoadoutShoot())
		{
			return false;
		}

		UWorld* World = Gun->GetWorld();
		if (!World)
		{
			return false;
		}

		const double CurrentTime = World->GetTimeSeconds();
		if (CurrentTime < State.NextAllowedShootTime)
		{
			return false;
		}

		APlayerController* PlayerController = TMResolveLoadoutPlayerController(LoadoutWidget);
		if (!PlayerController || !bClickJustPressed)
		{
			return false;
		}

		if (TMIsLoadoutAttachmentsWidgetVisible(LoadoutWidget))
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMLoadoutPreviewShoot] Click detected on loadout weapon %s, but attachments widget is active."),
				*Gun->GetName());
			return false;
		}

		if (!TMTraceLoadoutPreviewWeaponUnderCursor(PlayerController, Gun))
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[TMLoadoutPreviewShoot] Click detected, but cursor trace did not hit %s."),
				*Gun->GetName());
			return false;
		}

		if (!TMInvokeLoadoutPreviewShoot(Gun, PlayerController))
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[TMLoadoutPreviewShoot] %s can loadout shoot, but no real Shoot/StartShooting function was found."),
				*Gun->GetName());
			return false;
		}

		State.NextAllowedShootTime = CurrentTime + TMLoadoutPreviewShootCooldownSeconds;
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMLoadoutPreviewShoot] Fired loadout weapon %s. NextAllowedShootTime=%.2f"),
			*Gun->GetName(),
			State.NextAllowedShootTime);
		return true;
	}

	bool TMTickLoadoutPreviewShoot(float DeltaTime)
	{
		APlayerController* InputPlayerController = nullptr;
		for (int32 Index = TMLoadoutPreviewShootStates.Num() - 1; Index >= 0; --Index)
		{
			FTMLoadoutPreviewShootState& State = TMLoadoutPreviewShootStates[Index];
			if (!State.LoadoutWidget.IsValid())
			{
				TMLoadoutPreviewShootStates.RemoveAtSwap(Index);
				continue;
			}

			if (!InputPlayerController)
			{
				InputPlayerController = TMResolveLoadoutPlayerController(State.LoadoutWidget.Get());
			}
		}

		const bool bLeftMouseDown = TMIsLoadoutPreviewLeftMouseDown(InputPlayerController);
		const bool bClickJustPressed = bLeftMouseDown && !bTMLoadoutPreviewLeftMouseWasDown;
		bTMLoadoutPreviewLeftMouseWasDown = bLeftMouseDown;
		if (!bClickJustPressed)
		{
			if (TMLoadoutPreviewShootStates.Num() == 0)
			{
				TMLoadoutPreviewShootTickerHandle.Reset();
				return false;
			}

			return true;
		}

		for (FTMLoadoutPreviewShootState& State : TMLoadoutPreviewShootStates)
		{
			if (TMTryLoadoutPreviewShoot(State, bClickJustPressed))
			{
				break;
			}
		}

		if (TMLoadoutPreviewShootStates.Num() == 0)
		{
			TMLoadoutPreviewShootTickerHandle.Reset();
			return false;
		}

		return true;
	}

	void TMRegisterLoadoutPreviewShoot(UUserWidget* OwnerWidget)
	{
		TArray<UUserWidget*> LoadoutWidgets;
		TMCollectLoadoutPreviewWidgets(OwnerWidget, LoadoutWidgets);
		for (UUserWidget* LoadoutWidget : LoadoutWidgets)
		{
			if (!LoadoutWidget)
			{
				continue;
			}

			const bool bAlreadyRegistered = TMLoadoutPreviewShootStates.ContainsByPredicate(
				[LoadoutWidget](const FTMLoadoutPreviewShootState& State)
				{
					return State.LoadoutWidget.Get() == LoadoutWidget;
				});
			if (!bAlreadyRegistered)
			{
				FTMLoadoutPreviewShootState& State = TMLoadoutPreviewShootStates.AddDefaulted_GetRef();
				State.LoadoutWidget = LoadoutWidget;
			}
		}

		if (TMLoadoutPreviewShootStates.Num() > 0 && !TMLoadoutPreviewShootTickerHandle.IsValid())
		{
			TMLoadoutPreviewShootTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateStatic(&TMTickLoadoutPreviewShoot));
		}
	}
}

void UTMGameplayStatics::CleanupLoadoutPreview(UUserWidget* OwnerWidget)
{
	TMRegisterLoadoutPreviewShoot(OwnerWidget);

	TArray<UUserWidget*> LoadoutWidgets;
	TMCollectLoadoutPreviewWidgets(OwnerWidget, LoadoutWidgets);
	for (UUserWidget* LoadoutWidget : LoadoutWidgets)
	{
		TMCleanupLoadoutPreviewWidget(LoadoutWidget);
	}
}

bool UTMGameplayStatics::ApplyLoadoutWeaponLayerIcon(UUserWidget* WeaponLayerWidget)
{
	if (!WeaponLayerWidget)
	{
		return false;
	}

	TMRegisterLoadoutPreviewShoot(WeaponLayerWidget);

	if (TMGameplayStatics::ShouldHideLoadoutWeaponLayer(WeaponLayerWidget))
	{
		WeaponLayerWidget->SetVisibility(ESlateVisibility::Collapsed);
		return true;
	}

	UTexture2D* IconTexture = TMGameplayStatics::GetLoadoutWeaponLayerIconTexture(WeaponLayerWidget);
	const bool bHasIconTexture = IconTexture != nullptr;

	UButton* WeaponButton = Cast<UButton>(WeaponLayerWidget->GetWidgetFromName(TEXT("B_Weapon")));
	UTextBlock* NameText = Cast<UTextBlock>(WeaponLayerWidget->GetWidgetFromName(TEXT("NameText")));
	if (!WeaponButton)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[TMLoadoutWeaponIcon] %s has no B_Weapon button."),
			*WeaponLayerWidget->GetName());
		return false;
	}

	UImage* IconImage = Cast<UImage>(WeaponLayerWidget->GetWidgetFromName(TEXT("TM_LoadoutWeaponIcon")));
	if (!IconImage && WeaponLayerWidget->WidgetTree)
	{
		IconImage = WeaponLayerWidget->WidgetTree->ConstructWidget<UImage>(
			UImage::StaticClass(),
			TEXT("TM_LoadoutWeaponIcon"));
	}

	if (!IconImage)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[TMLoadoutWeaponIcon] Failed to create icon image for %s."),
			*WeaponLayerWidget->GetName());
		return false;
	}

	USizeBox* IconBox = Cast<USizeBox>(WeaponLayerWidget->GetWidgetFromName(TEXT("TM_LoadoutWeaponIconBox")));
	if (!IconBox && WeaponLayerWidget->WidgetTree)
	{
		IconBox = WeaponLayerWidget->WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(),
			TEXT("TM_LoadoutWeaponIconBox"));
	}

	if (!IconBox)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[TMLoadoutWeaponIcon] Failed to create icon size box for %s."),
			*WeaponLayerWidget->GetName());
		return false;
	}

	FSlateBrush IconBrush = TMGameplayStatics::MakeLoadoutWeaponLayerIconBrush(IconTexture);
	IconImage->SetBrush(IconBrush);
	IconImage->SetDesiredSizeOverride(FVector2D(
		TMGameplayStatics::LoadoutWeaponLayerIconWidth,
		TMGameplayStatics::LoadoutWeaponLayerIconHeight));
	IconImage->SetColorAndOpacity(FLinearColor::White);
	IconImage->SetVisibility(ESlateVisibility::HitTestInvisible);

	IconBox->SetWidthOverride(TMGameplayStatics::LoadoutWeaponLayerIconWidth);
	IconBox->SetHeightOverride(TMGameplayStatics::LoadoutWeaponLayerIconHeight);
	IconBox->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (IconBox->GetContent() != IconImage)
	{
		IconBox->SetContent(IconImage);
	}

	TMGameplayStatics::ApplyLoadoutWeaponLayerSlotSize(WeaponLayerWidget);
	TMGameplayStatics::ApplyLoadoutWeaponLayerSlotSize(WeaponButton);
	TMGameplayStatics::ApplyLoadoutWeaponLayerSlotSize(IconBox);
	TMGameplayStatics::RegisterLoadoutWeaponLayerIconHover(WeaponButton, IconImage);

	if (WeaponButton->GetContent() != IconBox)
	{
		WeaponButton->SetContent(IconBox);
	}

	if (NameText)
	{
		NameText->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (WeaponLayerWidget->WidgetTree)
	{
		WeaponLayerWidget->WidgetTree->ForEachWidget(
			[](UWidget* Widget)
			{
				if (UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
				{
					TextBlock->SetVisibility(ESlateVisibility::Collapsed);
				}
			});
	}

	return bHasIconTexture;
}

void UTMGameplayStatics::StartLoadoutGearShimmer(UUserWidget* OwnerWidget)
{
	if (!OwnerWidget)
	{
		return;
	}

	TMRegisterLoadoutPreviewShoot(OwnerWidget);

	if (UButton* GearButton = Cast<UButton>(OwnerWidget->GetWidgetFromName(TEXT("B_Gear"))))
	{
		TMGameplayStatics::RegisterLoadoutGearShimmer(GearButton);
	}

	static const FName ReturnButtonNames[] = { TEXT("B_Return"), TEXT("B_Return_1") };
	for (const FName& ReturnButtonName : ReturnButtonNames)
	{
		if (UButton* ReturnButton = Cast<UButton>(OwnerWidget->GetWidgetFromName(ReturnButtonName)))
		{
			TMGameplayStatics::RegisterLoadoutGearShimmer(
				ReturnButton,
				TMGameplayStatics::LoadoutReturnShimmerPeriod,
				TMGameplayStatics::LoadoutReturnShimmerDuration,
				TMGameplayStatics::LoadoutReturnShimmerFirstDelay);
		}
	}

	struct FReturnImageHoverBinding
	{
		FName ImageName;
		FName HoverButtonName;
	};

	static const FReturnImageHoverBinding ReturnImageHoverBindings[] =
	{
		{ TEXT("I_Return"), TEXT("B_Return") },
		{ TEXT("I_Return_1"), TEXT("B_Return_1") }
	};
	for (const FReturnImageHoverBinding& Binding : ReturnImageHoverBindings)
	{
		TMGameplayStatics::RegisterLoadoutImageShimmerDeferred(
			OwnerWidget,
			Binding.ImageName,
			Binding.HoverButtonName,
			TMGameplayStatics::LoadoutReturnShimmerPeriod,
			TMGameplayStatics::LoadoutReturnShimmerDuration,
			TMGameplayStatics::LoadoutReturnShimmerFirstDelay);
	}
}

bool UTMGameplayStatics::AttachActiveLoadoutWeaponToTransformator(AActor* WeaponActor)
{
	static const FName TransformatorName(TEXT("ActiveWeaponTransformator"));

	if (!IsValid(WeaponActor))
	{
		UE_LOG(LogTemp, Warning, TEXT("[TMLoadoutTransformator] WeaponActor is invalid."));
		return false;
	}

	UWorld* World = WeaponActor->GetWorld();
	if (!World)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[TMLoadoutTransformator] Weapon=%s has no world."),
			*WeaponActor->GetName());
		return false;
	}

	AActor* Transformator = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Candidate = *It;
		if (!IsValid(Candidate) || Candidate == WeaponActor)
		{
			continue;
		}

		const bool bHasTransformatorTag = Candidate->ActorHasTag(TransformatorName);
		const bool bHasTransformatorName = Candidate->GetFName() == TransformatorName
			|| Candidate->GetName().Contains(TransformatorName.ToString());
#if WITH_EDITOR
		const bool bHasTransformatorLabel = Candidate->GetActorLabel() == TransformatorName.ToString();
#else
		const bool bHasTransformatorLabel = false;
#endif
		if (bHasTransformatorTag || bHasTransformatorName || bHasTransformatorLabel)
		{
			Transformator = Candidate;
			break;
		}
	}

	if (!IsValid(Transformator))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[TMLoadoutTransformator] Transformator actor not found for Weapon=%s World=%s."),
			*WeaponActor->GetName(),
			*GetNameSafe(World));
		return false;
	}

	const FAttachmentTransformRules AttachRules(
		EAttachmentRule::SnapToTarget,
		EAttachmentRule::SnapToTarget,
		EAttachmentRule::KeepWorld,
		false);
	const bool bAttached = WeaponActor->AttachToActor(Transformator, AttachRules);
	if (bAttached)
	{
		WeaponActor->SetActorRelativeLocation(FVector::ZeroVector, false, nullptr, ETeleportType::TeleportPhysics);
		WeaponActor->SetActorRelativeRotation(FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
	}

	const USceneComponent* WeaponRoot = WeaponActor->GetRootComponent();
	const FString WeaponRelativeLocation = WeaponRoot
		? WeaponRoot->GetRelativeLocation().ToString()
		: FString(TEXT("None"));
	const FString WeaponRelativeRotation = WeaponRoot
		? WeaponRoot->GetRelativeRotation().ToString()
		: FString(TEXT("None"));

	UE_LOG(
		LogTemp,
		Display,
		TEXT("[TMLoadoutTransformator] Attach Weapon=%s To=%s Result=%d WeaponLocation=%s WeaponRotation=%s WeaponRelativeLocation=%s WeaponRelativeRotation=%s TransformatorLocation=%s TransformatorRotation=%s"),
		*WeaponActor->GetName(),
		*Transformator->GetName(),
		bAttached ? 1 : 0,
		*WeaponActor->GetActorLocation().ToString(),
		*WeaponActor->GetActorRotation().ToString(),
		*WeaponRelativeLocation,
		*WeaponRelativeRotation,
		*Transformator->GetActorLocation().ToString(),
		*Transformator->GetActorRotation().ToString());

	return bAttached;
}

void UTMGameplayStatics::MarketSoundRoom(bool enable)
{
	
}

bool UTMGameplayStatics::ApplyMPSOverlayPose(ACharacter* Character, UObject* ActiveWeapon)
{
	if (!Character)
	{
		if (IsTouchMeRuntimeTraceEnabled())
		{
			UE_LOG(
				LogTouchMeRuntimeTrace,
				Warning,
				TEXT("[ApplyMPSOverlayPose] Character=None ActiveWeapon=%s"),
				ActiveWeapon ? *ActiveWeapon->GetPathName() : TEXT("None"));
		}
		return false;
	}

	int64 PoseValue = 0;
	FString PoseDisplayName;
	TMGameplayStatics::GetEnumLikePropertyValue(ActiveWeapon, TEXT("DT_OverlayPose"), PoseValue, PoseDisplayName);
	if (IsTouchMeRuntimeTraceEnabled())
	{
		UE_LOG(
			LogTouchMeRuntimeTrace,
			Warning,
			TEXT("[ApplyMPSOverlayPose] Character=%s CharacterClass=%s ActiveWeapon=%s ActiveWeaponClass=%s PoseValue=%lld PoseDisplayName=%s"),
			*Character->GetPathName(),
			*Character->GetClass()->GetPathName(),
			ActiveWeapon ? *ActiveWeapon->GetPathName() : TEXT("None"),
			ActiveWeapon ? *ActiveWeapon->GetClass()->GetPathName() : TEXT("None"),
			static_cast<long long>(PoseValue),
			*PoseDisplayName);
	}
	TMGameplayStatics::SetEnumLikePropertyValue(Character, TEXT("OverlayPose"), PoseValue);
	const TArray<FString> OverlayStateAliases = TMGameplayStatics::BuildOverlayStateAliases(PoseValue, PoseDisplayName);
	TMGameplayStatics::SetEnumLikePropertyValueByAliases(Character, TEXT("OverlayState"), OverlayStateAliases);

	USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (!Mesh)
	{
		return false;
	}

	const TSubclassOf<UAnimInstance> OverlayAnimClass = TMGameplayStatics::LoadMPSOverlayAnimClass(PoseValue, PoseDisplayName);
	if (!OverlayAnimClass)
	{
		if (IsTouchMeRuntimeTraceEnabled())
		{
			UE_LOG(
				LogTouchMeRuntimeTrace,
				Warning,
				TEXT("[ApplyMPSOverlayPose] OverlayAnimClass=None PoseValue=%lld PoseDisplayName=%s"),
				static_cast<long long>(PoseValue),
				*PoseDisplayName);
		}
		return false;
	}

	if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
	{
		TMGameplayStatics::SetEnumLikePropertyValueByAliases(AnimInstance, TEXT("OverlayState"), OverlayStateAliases);
		if (IsTouchMeRuntimeTraceEnabled())
		{
			UE_LOG(
				LogTouchMeRuntimeTrace,
				Warning,
				TEXT("[ApplyMPSOverlayPose] AnimInstance=%s Class=%s OverlayAnimClass=%s"),
				*AnimInstance->GetPathName(),
				*AnimInstance->GetClass()->GetPathName(),
				*OverlayAnimClass->GetPathName());
		}
	}

	Mesh->LinkAnimClassLayers(OverlayAnimClass);
	TMGameplayStatics::PatchLinkedOverlayAnimMasters(Mesh, Mesh->GetAnimInstance());
	if (IsTouchMeRuntimeTraceEnabled())
	{
		UE_LOG(
			LogTouchMeRuntimeTrace,
			Warning,
			TEXT("[ApplyMPSOverlayPose] LinkAnimClassLayers done OverlayAnimClass=%s"),
			*OverlayAnimClass->GetPathName());
	}
	return true;
}

bool UTMGameplayStatics::ApplyALSAimState(ACharacter* Character, const bool bAiming)
{
	if (!Character)
	{
		return false;
	}

	const TArray<FString> RotationModeAliases = TMGameplayStatics::BuildRotationModeAliasesForAimState(Character, bAiming);
	bool bChanged = TMGameplayStatics::CallSetRotationModeFunction(Character, RotationModeAliases);
	bChanged |= TMGameplayStatics::SetEnumLikePropertyValueByAliases(Character, TEXT("RotationMode"), RotationModeAliases);
	bChanged |= TMGameplayStatics::SetBoolPropertyByName(Character, TEXT("bIsAiming"), bAiming);
	bChanged |= TMGameplayStatics::SetBoolPropertyByName(Character, TEXT("bAiming"), bAiming);
	bChanged |= TMGameplayStatics::SetBoolPropertyByName(Character, TEXT("IsAiming"), bAiming);
	bChanged |= TMGameplayStatics::SetBoolPropertyByName(Character, TEXT("bADS"), bAiming);
	bChanged |= TMGameplayStatics::SetBoolPropertyByName(Character, TEXT("IsADS"), bAiming);
	bChanged |= TMGameplayStatics::ApplyRotationModeAliasesToMesh(Character->GetMesh(), RotationModeAliases);

	if (IsTouchMeRuntimeTraceEnabled())
	{
		UE_LOG(
			LogTouchMeRuntimeTrace,
			Warning,
			TEXT("[ApplyALSAimState] Character=%s bAiming=%d Changed=%d Aliases=%s"),
			*Character->GetPathName(),
			bAiming ? 1 : 0,
			bChanged ? 1 : 0,
			*FString::Join(RotationModeAliases, TEXT(",")));
	}

	return bChanged;
}

bool UTMGameplayStatics::ApplyALSTurnInPlaceState(ACharacter* Character, const float DeltaSeconds)
{
	if (!Character)
	{
		return false;
	}

	const FRotator ActorRotation = Character->GetActorRotation();
	const FRotator BaseAimRotation = Character->GetBaseAimRotation();

	double YawOffset = 0.0;
	if (!TMGameplayStatics::ReadTurnYawOffset(Character, YawOffset))
	{
		YawOffset = FRotator::NormalizeAxis(BaseAimRotation.Yaw - ActorRotation.Yaw);
	}

	const double SourceYaw = FRotator::NormalizeAxis(ActorRotation.Yaw + YawOffset);
	const double Pitch = TMGameplayStatics::ReadALSTurnPitch(Character, BaseAimRotation);
	const FRotator AimingRotation(Pitch, SourceYaw, 0.0);
	const FVector2D AimingAngle(YawOffset, Pitch);

	TMGameplayStatics::CleanupTurnBridgeStatesIfNeeded();
	const TWeakObjectPtr<ACharacter> CharacterKey(Character);
	TMGameplayStatics::FALSTurnInPlaceBridgeState& BridgeState = TMGameplayStatics::GALSTurnInPlaceBridgeStates.FindOrAdd(CharacterKey);
	double AimYawRate = 0.0;
	if (BridgeState.bHasSourceYaw && DeltaSeconds > SMALL_NUMBER)
	{
		const double DeltaYaw = FMath::Abs(FRotator::NormalizeAxis(SourceYaw - BridgeState.SourceYaw));
		AimYawRate = FMath::Min(DeltaYaw / static_cast<double>(DeltaSeconds), 720.0);
	}
	BridgeState.bHasSourceYaw = true;
	BridgeState.SourceYaw = SourceYaw;

	bool bAiming = false;
	const bool bApplyLookingDirection = !TMGameplayStatics::ReadALSAimBool(Character, bAiming) || !bAiming;
	const TArray<FString> ViewModeAliases = TMGameplayStatics::BuildThirdPersonViewModeAliases();
	const TArray<FString> RotationModeAliases = TMGameplayStatics::BuildLookingDirectionAliases();

	bool bChanged = TMGameplayStatics::ApplyALSTurnValuesToObject(Character, AimingRotation, AimingAngle, AimYawRate);
	bChanged |= TMGameplayStatics::SetEnumLikePropertyValueByAliasesOrValue(Character, TEXT("ViewMode"), ViewModeAliases, 0);
	if (bApplyLookingDirection)
	{
		bChanged |= TMGameplayStatics::SetEnumLikePropertyValueByAliasesOrValue(Character, TEXT("RotationMode"), RotationModeAliases, 1);
	}
	bChanged |= TMGameplayStatics::ApplyALSTurnValuesToMesh(Character->GetMesh(), AimingRotation, AimingAngle, AimYawRate, bApplyLookingDirection);

	if (bChanged && IsTouchMeRuntimeTraceEnabled())
	{
		UE_LOG(
			LogTouchMeRuntimeTrace,
			Verbose,
			TEXT("[ALSTurnInPlaceBridge] Character=%s YawOffset=%.2f SourceYaw=%.2f AimYawRate=%.2f"),
			*Character->GetPathName(),
			YawOffset,
			SourceYaw,
			AimYawRate);
	}

	return bChanged;
}

bool UTMGameplayStatics::IsHeadHitBone(const FName HitBone)
{
	return TMGameplayStatics::IsHeadHitBoneName(HitBone);
}

bool UTMGameplayStatics::IsHeadHit(const FHitResult& HitResult)
{
	return IsHeadHitBone(HitResult.BoneName);
}

float UTMGameplayStatics::GetBoneDamageMultiplier(
	const FName HitBone,
	const float HeadMultiplier,
	const float DefaultMultiplier)
{
	return IsHeadHitBone(HitBone) ? HeadMultiplier : DefaultMultiplier;
}

bool UTMGameplayStatics::DumpAnimBlueprintGraphLinks()
{
#if WITH_EDITOR
	static const TCHAR* TargetAnimBlueprintPaths[] =
	{
		TEXT("/Game/MP_System_V3/Game/Blueprints/Core/AnimBP_MPS_Master.AnimBP_MPS_Master"),
		TEXT("/Game/Test/MPVS_SkeletonProbe/ImportedOnALS/ABP_UE5_MPSBones_OnALS.ABP_UE5_MPSBones_OnALS")
	};

	FString Dump;
	for (const TCHAR* TargetAnimBlueprintPath : TargetAnimBlueprintPaths)
	{
		TMGameplayStatics::AppendAnimBlueprintGraphDump(TargetAnimBlueprintPath, Dump);
		Dump += TEXT("\n");
	}

	const FString OutPath = FPaths::ProjectSavedDir() / TEXT("Codex/anim_graph_dump_cpp.txt");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutPath), true);
	const bool bSaved = FFileHelper::SaveStringToFile(Dump, *OutPath);
	UE_LOG(LogTemp, Display, TEXT("[TMAnimGraphDump] Saved=%d Path=%s"), bSaved ? 1 : 0, *OutPath);
	return bSaved;
#else
	return false;
#endif
}

bool UTMGameplayStatics::FixMPSBonesAimTargetGraph()
{
#if WITH_EDITOR
	static const TCHAR* TargetAnimBlueprintPath =
		TEXT("/Game/Test/MPVS_SkeletonProbe/ImportedOnALS/ABP_UE5_MPSBones_OnALS.ABP_UE5_MPSBones_OnALS");

	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, TargetAnimBlueprintPath);
	if (!AnimBlueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("[TMFixAimTargetGraph] Failed to load %s"), TargetAnimBlueprintPath);
		return false;
	}

	bool bChanged = false;
	TArray<UEdGraph*> Graphs;
	AnimBlueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph || Graph->GetFName() != TEXT("Pose_AimTarget"))
		{
			continue;
		}

		for (UEdGraphNode* GraphNode : Graph->Nodes)
		{
			UAnimGraphNode_ModifyBone* ModifyBoneNode = Cast<UAnimGraphNode_ModifyBone>(GraphNode);
			if (!ModifyBoneNode
				|| ModifyBoneNode->Node.BoneToModify.BoneName != TEXT("VB Control")
				|| !FMath::IsNearlyEqual(ModifyBoneNode->Node.Translation.Y, 16.0))
			{
				continue;
			}

			const FRotator MPRotation(-90.0, 0.0, 0.0);
			if (!ModifyBoneNode->Node.Rotation.Equals(MPRotation, KINDA_SMALL_NUMBER))
			{
				AnimBlueprint->Modify();
				Graph->Modify();
				ModifyBoneNode->Modify();
				ModifyBoneNode->Node.Rotation = MPRotation;
				ModifyBoneNode->ReconstructNode();
				bChanged = true;
			}
		}
	}

	if (!bChanged)
	{
		UE_LOG(LogTemp, Display, TEXT("[TMFixAimTargetGraph] No changes needed for %s"), TargetAnimBlueprintPath);
		return true;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
	FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);

	UPackage* Package = AnimBlueprint->GetOutermost();
	const FString PackageFilename =
		FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	const bool bSaved = UPackage::SavePackage(Package, AnimBlueprint, *PackageFilename, SaveArgs);
	UE_LOG(
		LogTemp,
		Display,
		TEXT("[TMFixAimTargetGraph] Changed=1 Saved=%d Asset=%s"),
		bSaved ? 1 : 0,
		*AnimBlueprint->GetPathName());
	return bSaved;
#else
	return false;
#endif
}

bool UTMGameplayStatics::PatchMenuViewerNoReinitPose()
{
#if WITH_EDITOR
	static const TCHAR* TargetBlueprintPath =
		TEXT("/Game/MP_System_V3/Game/Blueprints/Core/MainMenuPawn/BP_MenuViewer.BP_MenuViewer");

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, TargetBlueprintPath);
	if (!Blueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("[TMMenuViewerNoReinitPose] Failed to load %s"), TargetBlueprintPath);
		return false;
	}

	bool bChanged = false;
	int32 PatchedPinCount = 0;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph || Graph->GetFName() != TEXT("DefineMesh"))
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node);
			if (!CallFunctionNode || CallFunctionNode->GetFunctionName() != TEXT("SetSkinnedAssetAndUpdate"))
			{
				continue;
			}

			UEdGraphPin* ReinitPosePin = CallFunctionNode->FindPin(TEXT("bReinitPose"), EGPD_Input);
			if (!ReinitPosePin || ReinitPosePin->LinkedTo.Num() > 0)
			{
				continue;
			}

			++PatchedPinCount;
			if (!ReinitPosePin->DefaultValue.Equals(TEXT("false"), ESearchCase::IgnoreCase))
			{
				Blueprint->Modify();
				Graph->Modify();
				CallFunctionNode->Modify();
				ReinitPosePin->Modify();
				ReinitPosePin->DefaultValue = TEXT("false");
				CallFunctionNode->PinDefaultValueChanged(ReinitPosePin);
				bChanged = true;

				UE_LOG(
					LogTemp,
					Display,
					TEXT("[TMMenuViewerNoReinitPose] Patched %s.%s bReinitPose=false"),
					*Graph->GetName(),
					*CallFunctionNode->GetName());
			}
		}
	}

	if (PatchedPinCount != 2)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[TMMenuViewerNoReinitPose] Expected 2 SetSkinnedAssetAndUpdate pins, found %d in %s"),
			PatchedPinCount,
			*Blueprint->GetPathName());
	}

	if (!bChanged)
	{
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[TMMenuViewerNoReinitPose] No changes needed for %s. PinsFound=%d"),
			*Blueprint->GetPathName(),
			PatchedPinCount);
		return PatchedPinCount == 2;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	UPackage* Package = Blueprint->GetOutermost();
	const FString PackageFilename =
		FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	const bool bSaved = UPackage::SavePackage(Package, Blueprint, *PackageFilename, SaveArgs);
	UE_LOG(
		LogTemp,
		Display,
		TEXT("[TMMenuViewerNoReinitPose] Changed=1 Saved=%d Asset=%s PinsFound=%d"),
		bSaved ? 1 : 0,
		*Blueprint->GetPathName(),
		PatchedPinCount);
	return bSaved;
#else
	return false;
#endif
}

AActor* UTMGameplayStatics::Shoot(
	const UObject* WorldContextObject,
	TSubclassOf<AActor> ProjectileClass,
	FVector Start,
	FVector Direction,
	float Distance,
	float ProjectileSpeed,
	TEnumAsByte<ECollisionChannel> TraceChannel,
	AActor* Owner,
	APawn* Instigator,
	USoundBase* ShootSound,
	USoundAttenuation* AttenuationSettings,
	USoundConcurrency* ConcurrencySettings)
{
	if (!WorldContextObject || !ProjectileClass || Distance <= 0.f)
	{
		return nullptr;
	}

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (!World)
	{
		return nullptr;
	}

	const FVector TraceDirection = Direction.GetSafeNormal();
	if (TraceDirection.IsNearlyZero())
	{
		return nullptr;
	}

	const FRotator SpawnRotation = TraceDirection.Rotation();

	if (ShootSound)
	{
		SpawnSoundAtLocationDistanced(
			WorldContextObject,
			ShootSound,
			Start,
			SpawnRotation,
			1.f,
			1.f,
			0.f,
			AttenuationSettings,
			ConcurrencySettings,
			true);
	}

	const FVector End = Start + TraceDirection * Distance;

	FHitResult HitResult;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(UTMGameplayStatics_Shoot), true);
	QueryParams.AddIgnoredActor(Owner);
	QueryParams.AddIgnoredActor(Instigator);

	if (World->LineTraceSingleByChannel(HitResult, Start, End, TraceChannel, QueryParams))
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	SpawnParams.Instigator = Instigator;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* Projectile = World->SpawnActor<AActor>(ProjectileClass, End, SpawnRotation, SpawnParams);
	if (!Projectile)
	{
		return nullptr;
	}

	if (UProjectileMovementComponent* ProjectileMovement = Projectile->FindComponentByClass<UProjectileMovementComponent>())
	{
		if (ProjectileSpeed > 0.f)
		{
			ProjectileMovement->InitialSpeed = ProjectileSpeed;
			ProjectileMovement->MaxSpeed = FMath::Max(ProjectileMovement->MaxSpeed, ProjectileSpeed);
		}

		ProjectileMovement->Velocity = TraceDirection * (ProjectileSpeed > 0.f ? ProjectileSpeed : ProjectileMovement->InitialSpeed);
		ProjectileMovement->Activate(true);
	}

	return Projectile;
}

#undef LOCTEXT_NAMESPACE

