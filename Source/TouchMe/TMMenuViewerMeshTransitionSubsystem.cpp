#include "TMMenuViewerMeshTransitionSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/ActorComponent.h"
#include "Components/Button.h"
#include "Components/ContentWidget.h"
#include "Components/Image.h"
#include "Components/LightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/PrimitiveComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/ScaleBox.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/DataTable.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gun/Gun.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "TMAudioEnvelopeFollower.h"
#include "TouchMe.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogTMMenuViewerMeshTransition, Log, All);

namespace
{
static TAutoConsoleVariable<int32> CVarMenuViewerMeshTransition(
	TEXT("tm.MenuViewerMeshTransition"),
	1,
	TEXT("Enables BP_MenuViewer mesh tracking and automatic body mesh cycling."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarMenuViewerMeshTransitionAutoCycle(
	TEXT("tm.MenuViewerMeshTransition.AutoCycle"),
	1,
	TEXT("Automatically cycles BP_MenuViewer body meshes so the transition is visible in the main menu."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMenuViewerMeshTransitionAutoCycleInterval(
	TEXT("tm.MenuViewerMeshTransition.AutoCycleInterval"),
	15.0f,
	TEXT("Minimum seconds between automatic BP_MenuViewer body mesh changes."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarMenuViewerMeshTransitionBeatSync(
	TEXT("tm.MenuViewerMeshTransition.BeatSync"),
	1,
	TEXT("Snaps automatic BP_MenuViewer body mesh changes to the active main menu soundtrack beat."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarMenuViewerMeshTransitionBeatSyncDownbeat(
	TEXT("tm.MenuViewerMeshTransition.BeatSyncDownbeat"),
	1,
	TEXT("When beat sync is enabled, use downbeats instead of every beat for menu body mesh changes."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarMenuViewerMeshTransitionBeatSyncPhraseEnd(
	TEXT("tm.MenuViewerMeshTransition.BeatSyncPhraseEnd"),
	0,
	TEXT("When beat sync is enabled, wait for the last beat of a musical phrase before menu body mesh changes."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarMenuViewerMeshTransitionBeatSyncPhraseStart(
	TEXT("tm.MenuViewerMeshTransition.BeatSyncPhraseStart"),
	1,
	TEXT("When beat sync is enabled, wait for the first beat of a musical phrase before menu body mesh changes."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarMenuViewerMeshTransitionBeatSyncPhraseBars(
	TEXT("tm.MenuViewerMeshTransition.BeatSyncPhraseBars"),
	4,
	TEXT("Bars per menu beat-sync phrase when tm.MenuViewerMeshTransition.BeatSyncPhraseEnd is enabled."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMenuViewerMeshTransitionBeatSyncWindow(
	TEXT("tm.MenuViewerMeshTransition.BeatSyncWindow"),
	0.75f,
	TEXT("Seconds after a detected beat that may be used for a beat-synced menu body mesh change."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMenuViewerMeshTransitionBeatSyncPulseThreshold(
	TEXT("tm.MenuViewerMeshTransition.BeatSyncPulseThreshold"),
	1.0f,
	TEXT("Normalized envelope threshold used as a secondary rhythm pulse when beat events are sparse."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarLoadoutFOV(
	TEXT("tm.LoadoutFOV"),
	1,
	TEXT("Locks the player camera FOV while W_Loadout or W_Attachments is visible."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutFOVAngle(
	TEXT("tm.LoadoutFOV.Angle"),
	85.0f,
	TEXT("Player camera FOV used while the loadout UI is visible."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMenuFOVAngle(
	TEXT("tm.MenuFOV.Angle"),
	90.0f,
	TEXT("Player camera FOV used by default while the main menu UI is visible."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarLoadoutPostProcess(
	TEXT("tm.LoadoutPostProcess"),
	1,
	TEXT("Enables cinematic main menu and loadout camera post process profiles."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutPostProcessBlendWeight(
	TEXT("tm.LoadoutPostProcess.BlendWeight"),
	1.0f,
	TEXT("Blend weight for the cinematic main menu post process."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutPostProcessEdgeBlur(
	TEXT("tm.LoadoutPostProcess.EdgeBlur"),
	82.0f,
	TEXT("Depth-of-field vignette size for main menu edge blur. Lower values blur more of the edge."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutPostProcessPetzval(
	TEXT("tm.LoadoutPostProcess.Petzval"),
	0.8f,
	TEXT("Petzval bokeh strength for the cinematic main menu edge blur."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutPostProcessVignette(
	TEXT("tm.LoadoutPostProcess.Vignette"),
	0.42f,
	TEXT("Dark vignette strength for the cinematic main menu post process."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsPreviewBrightness(
	TEXT("tm.AttachmentsPreviewBrightness"),
	0.22f,
	TEXT("Delayed exposure and color lift used only while W_Attachments is visible."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarLoadoutBackGlowDucking(
	TEXT("tm.LoadoutBackGlowDucking"),
	1,
	TEXT("Enables the delayed bright rear glow profile while the loadout UI is visible."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutBrightBackGlowDelay(
	TEXT("tm.LoadoutBackGlow.Delay"),
	1.0f,
	TEXT("Seconds to wait after entering loadout before fading in the bright rear glow."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutBrightBackGlowFadeDuration(
	TEXT("tm.LoadoutBackGlow.FadeDuration"),
	2.0f,
	TEXT("Seconds used to fade the bright rear glow in while loadout is visible."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutBrightBackGlowLightIntensity(
	TEXT("tm.LoadoutBackGlow.LightIntensity"),
	4.0f,
	TEXT("Final rear RectLight intensity for the bright loadout glow."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutBrightBackGlowVisualScale(
	TEXT("tm.LoadoutBackGlow.VisualScale"),
	2.8f,
	TEXT("Final absolute P_Ambient_Glow scale for the bright loadout glow."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutBrightBackGlowAttenuationRadius(
	TEXT("tm.LoadoutBackGlow.AttenuationRadius"),
	300.0f,
	TEXT("Final rear RectLight attenuation radius for the bright loadout glow."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutBrightBackGlowSourceWidth(
	TEXT("tm.LoadoutBackGlow.SourceWidth"),
	240.0f,
	TEXT("Minimum rear RectLight source width for the bright loadout glow."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarLoadoutBrightBackGlowSourceHeight(
	TEXT("tm.LoadoutBackGlow.SourceHeight"),
	120.0f,
	TEXT("Minimum rear RectLight source height for the bright loadout glow."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarMainMenuBackGlow(
	TEXT("tm.MainMenuBackGlow"),
	1,
	TEXT("Fades in a subtle white rear glow behind the main menu character after a delay."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMainMenuBackGlowDelay(
	TEXT("tm.MainMenuBackGlow.Delay"),
	1.0f,
	TEXT("Seconds to keep the rear main menu glow black before fading it in."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMainMenuBackGlowFadeDuration(
	TEXT("tm.MainMenuBackGlow.FadeDuration"),
	1.5f,
	TEXT("Seconds used for the smooth neon fade-in behind the main menu character."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMainMenuBackGlowScale(
	TEXT("tm.MainMenuBackGlow.Scale"),
	1.25f,
	TEXT("Final RectLight intensity scale for the delayed main menu rear glow."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMainMenuBackGlowVisualScale(
	TEXT("tm.MainMenuBackGlow.VisualScale"),
	1.18f,
	TEXT("Final P_Ambient_Glow scale for the delayed main menu rear glow."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMainMenuBackGlowEmissive(
	TEXT("tm.MainMenuBackGlow.Emissive"),
	280.0f,
	TEXT("Final emissive multiplier for delayed main menu rear glow materials."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarMainMenuCameraDrift(
	TEXT("tm.MainMenuCameraDrift"),
	1,
	TEXT("Enables a subtle closed-loop main menu camera drift."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMainMenuCameraDriftPeriod(
	TEXT("tm.MainMenuCameraDrift.Period"),
	36.0f,
	TEXT("Seconds for one closed main menu camera drift loop."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMainMenuCameraDriftLocationAmplitude(
	TEXT("tm.MainMenuCameraDrift.LocationAmplitude"),
	2.0f,
	TEXT("Maximum main menu camera drift position amplitude in centimeters."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarMainMenuCameraDriftRotationAmplitude(
	TEXT("tm.MainMenuCameraDrift.RotationAmplitude"),
	0.18f,
	TEXT("Maximum main menu camera drift rotation amplitude in degrees."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarAttachmentsCameraFocus(
	TEXT("tm.AttachmentsCameraFocus"),
	1,
	TEXT("Enables delayed smooth camera focus moves for W_Attachments slot tabs."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusDelay(
	TEXT("tm.AttachmentsCameraFocus.Delay"),
	0.5f,
	TEXT("Seconds to hold the current W_Attachments camera after selecting a new attachment slot tab."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusDuration(
	TEXT("tm.AttachmentsCameraFocus.Duration"),
	0.38f,
	TEXT("Seconds used for the final W_Attachments socket-centering move."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusTargetScreenX(
	TEXT("tm.AttachmentsCameraFocus.TargetScreenX"),
	0.60f,
	TEXT("Normalized screen X target for the active W_Attachments socket, biased into the empty weapon preview area."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusTargetScreenY(
	TEXT("tm.AttachmentsCameraFocus.TargetScreenY"),
	0.56f,
	TEXT("Normalized screen Y target for the active W_Attachments socket, biased into the empty weapon preview area."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusCloseDepthScale(
	TEXT("tm.AttachmentsCameraFocus.CloseDepthScale"),
	0.82f,
	TEXT("Depth scale for W_Attachments focus moves; lower values move the camera closer to the weapon."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusTurnYawMax(
	TEXT("tm.AttachmentsCameraFocus.TurnYawMax"),
	30.0f,
	TEXT("Maximum deterministic yaw turn, in degrees, used to angle the installed attachment toward the camera."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusLocationScale(
	TEXT("tm.AttachmentsCameraFocus.LocationScale"),
	1.0f,
	TEXT("Global scale for W_Attachments camera focus position offsets."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusRotationScale(
	TEXT("tm.AttachmentsCameraFocus.RotationScale"),
	1.0f,
	TEXT("Global scale for W_Attachments camera focus rotation offsets."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusOpticsPanY(
	TEXT("tm.AttachmentsCameraFocus.Optics.PanY"),
	0.0f,
	TEXT("Camera local Y offset for W_Attachments Optics focus."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusOpticsHeightZ(
	TEXT("tm.AttachmentsCameraFocus.Optics.HeightZ"),
	3.0f,
	TEXT("Camera local Z offset for W_Attachments Optics focus."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusOpticsPitch(
	TEXT("tm.AttachmentsCameraFocus.Optics.Pitch"),
	-0.2f,
	TEXT("Camera pitch offset for W_Attachments Optics focus."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusOpticsYaw(
	TEXT("tm.AttachmentsCameraFocus.Optics.Yaw"),
	0.15f,
	TEXT("Camera yaw offset for W_Attachments Optics focus."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusSideRailPanY(
	TEXT("tm.AttachmentsCameraFocus.SideRail.PanY"),
	-8.0f,
	TEXT("Camera local Y offset for W_Attachments Side Rail focus."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusSideRailHeightZ(
	TEXT("tm.AttachmentsCameraFocus.SideRail.HeightZ"),
	1.5f,
	TEXT("Camera local Z offset for W_Attachments Side Rail focus."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusSideRailPitch(
	TEXT("tm.AttachmentsCameraFocus.SideRail.Pitch"),
	-0.15f,
	TEXT("Camera pitch offset for W_Attachments Side Rail focus."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusSideRailYaw(
	TEXT("tm.AttachmentsCameraFocus.SideRail.Yaw"),
	-0.85f,
	TEXT("Camera yaw offset for W_Attachments Side Rail focus."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusUnderbarrelPanY(
	TEXT("tm.AttachmentsCameraFocus.Underbarrel.PanY"),
	-13.0f,
	TEXT("Camera local Y offset for W_Attachments Underbarrel focus."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusUnderbarrelHeightZ(
	TEXT("tm.AttachmentsCameraFocus.Underbarrel.HeightZ"),
	-3.0f,
	TEXT("Camera local Z offset for W_Attachments Underbarrel focus."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusUnderbarrelPitch(
	TEXT("tm.AttachmentsCameraFocus.Underbarrel.Pitch"),
	-0.45f,
	TEXT("Camera pitch offset for W_Attachments Underbarrel focus."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusUnderbarrelYaw(
	TEXT("tm.AttachmentsCameraFocus.Underbarrel.Yaw"),
	-1.15f,
	TEXT("Camera yaw offset for W_Attachments Underbarrel focus."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusMuzzlePanY(
	TEXT("tm.AttachmentsCameraFocus.Muzzle.PanY"),
	-26.0f,
	TEXT("Camera local Y offset for W_Attachments Muzzle focus."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusMuzzleHeightZ(
	TEXT("tm.AttachmentsCameraFocus.Muzzle.HeightZ"),
	0.0f,
	TEXT("Camera local Z offset for W_Attachments Muzzle focus."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusMuzzlePitch(
	TEXT("tm.AttachmentsCameraFocus.Muzzle.Pitch"),
	-0.1f,
	TEXT("Camera pitch offset for W_Attachments Muzzle focus."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarAttachmentsCameraFocusMuzzleYaw(
	TEXT("tm.AttachmentsCameraFocus.Muzzle.Yaw"),
	-2.35f,
	TEXT("Camera yaw offset for W_Attachments Muzzle focus."),
	ECVF_Default);

static TWeakObjectPtr<USkeletalMesh> GLastMenuViewerVestMesh;

float GetMenuFOVAngle()
{
	return FMath::Clamp(CVarMenuFOVAngle.GetValueOnGameThread(), 5.0f, 170.0f);
}

void ApplyFixedMenuExposure(FPostProcessSettings& Settings, const float ExposureBias)
{
	Settings.bOverride_AutoExposureMethod = true;
	Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Settings.AutoExposureApplyPhysicalCameraExposure = false;
	Settings.bOverride_AutoExposureBias = true;
	Settings.AutoExposureBias = ExposureBias;
	Settings.bOverride_AutoExposureMinBrightness = true;
	Settings.AutoExposureMinBrightness = 0.0f;
	Settings.bOverride_AutoExposureMaxBrightness = true;
	Settings.AutoExposureMaxBrightness = 0.0f;
	Settings.bOverride_AutoExposureSpeedUp = true;
	Settings.AutoExposureSpeedUp = 100.0f;
	Settings.bOverride_AutoExposureSpeedDown = true;
	Settings.AutoExposureSpeedDown = 100.0f;
}

FVector4 LerpVector4(const FVector4& From, const FVector4& To, const float Alpha)
{
	return FVector4(
		FMath::Lerp(From.X, To.X, Alpha),
		FMath::Lerp(From.Y, To.Y, Alpha),
		FMath::Lerp(From.Z, To.Z, Alpha),
		FMath::Lerp(From.W, To.W, Alpha));
}

FLinearColor LerpLinearColor(const FLinearColor& From, const FLinearColor& To, const float Alpha)
{
	return FLinearColor(
		FMath::Lerp(From.R, To.R, Alpha),
		FMath::Lerp(From.G, To.G, Alpha),
		FMath::Lerp(From.B, To.B, Alpha),
		FMath::Lerp(From.A, To.A, Alpha));
}

float SmoothStep01(const float Alpha)
{
	const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	return ClampedAlpha * ClampedAlpha * (3.0f - 2.0f * ClampedAlpha);
}

bool IsVisibleWidgetInstance(const UUserWidget* Widget)
{
	if (!IsValid(Widget) || !Widget->IsVisible())
	{
		return false;
	}

	const ESlateVisibility Visibility = Widget->GetVisibility();
	if (Visibility == ESlateVisibility::Collapsed || Visibility == ESlateVisibility::Hidden)
	{
		return false;
	}

	if (Widget->IsInViewport())
	{
		return true;
	}

	const FVector2D DrawnSize = Widget->GetCachedGeometry().GetLocalSize();
	return DrawnSize.X > 16.0f && DrawnSize.Y > 16.0f;
}

void VisitWidgetTree(UWidget* Widget, TFunctionRef<void(UWidget*)> Visitor)
{
	if (!Widget)
	{
		return;
	}

	const ESlateVisibility Visibility = Widget->GetVisibility();
	if (Visibility == ESlateVisibility::Collapsed || Visibility == ESlateVisibility::Hidden)
	{
		return;
	}

	Visitor(Widget);

	if (UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
	{
		if (UWidget* RootWidget = UserWidget->GetRootWidget())
		{
			if (RootWidget != Widget)
			{
				VisitWidgetTree(RootWidget, Visitor);
			}
		}
	}

	if (UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget))
	{
		const int32 ChildCount = PanelWidget->GetChildrenCount();
		for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
		{
			VisitWidgetTree(PanelWidget->GetChildAt(ChildIndex), Visitor);
		}
		return;
	}

	if (UContentWidget* ContentWidget = Cast<UContentWidget>(Widget))
	{
		VisitWidgetTree(ContentWidget->GetContent(), Visitor);
	}
}

float ResolveViewportAspectRatio(UWorld* World)
{
	if (World)
	{
		if (const UGameViewportClient* GameViewportClient = World->GetGameViewport())
		{
			FVector2D ViewportSize = FVector2D::ZeroVector;
			GameViewportClient->GetViewportSize(ViewportSize);
			if (ViewportSize.X > 1.0f && ViewportSize.Y > 1.0f)
			{
				return ViewportSize.X / ViewportSize.Y;
			}
		}
	}

	return 16.0f / 9.0f;
}

FString NormalizeAttachmentFocusToken(FString Text)
{
	Text.TrimStartAndEndInline();
	Text.ReplaceInline(TEXT(" "), TEXT(""));
	Text.ReplaceInline(TEXT("_"), TEXT(""));
	Text.ReplaceInline(TEXT("-"), TEXT(""));
	Text.ReplaceInline(TEXT("::"), TEXT(""));
	return Text;
}

bool IsEmptyAttachmentFocusValue(const FString& Text)
{
	const FString Normalized = NormalizeAttachmentFocusToken(Text);
	return Normalized.IsEmpty()
		|| Normalized.Equals(TEXT("0"), ESearchCase::IgnoreCase)
		|| Normalized.Equals(TEXT("None"), ESearchCase::IgnoreCase)
		|| Normalized.Equals(TEXT("Null"), ESearchCase::IgnoreCase)
		|| Normalized.Equals(TEXT("Empty"), ESearchCase::IgnoreCase)
		|| Normalized.Equals(TEXT("ENUMAttachmentsNewEnumerator0"), ESearchCase::IgnoreCase)
		|| Normalized.Equals(TEXT("NewEnumerator0"), ESearchCase::IgnoreCase);
}

const FLinearColor& AttachmentSelectionHighlightColor()
{
	static const FLinearColor Color = FLinearColor::FromSRGBColor(FColor(210, 22, 18, 255));
	return Color;
}

const FLinearColor& GeneratedAttachmentIconSelectionFrameColor()
{
	static const FLinearColor Color = FLinearColor::FromSRGBColor(FColor(255, 212, 32, 255));
	return Color;
}

const FVector2D& GeneratedAttachmentIconSize()
{
	static const FVector2D Size(512.0f, 128.0f);
	return Size;
}

struct FGeneratedAttachmentIconDefinition
{
	FString IconObjectPath;
	TSet<FString> Tokens;
	mutable TWeakObjectPtr<UTexture2D> CachedIcon;
};

bool IsExcludedGeneratedAttachmentIconMesh(const UObject* Object)
{
	if (!Object)
	{
		return true;
	}

	const FString MeshName = Object->GetName();
	return MeshName.EndsWith(TEXT("_NR"), ESearchCase::IgnoreCase)
		|| MeshName.Contains(TEXT("NoRender"), ESearchCase::IgnoreCase)
		|| MeshName.Contains(TEXT("No_Render"), ESearchCase::IgnoreCase);
}

bool IsSupportedGeneratedAttachmentIconMesh(const UObject* Object)
{
	return Object
		&& !IsExcludedGeneratedAttachmentIconMesh(Object)
		&& (Object->IsA<UStaticMesh>() || Object->IsA<USkeletalMesh>());
}

void AddGeneratedAttachmentIconToken(TSet<FString>& Tokens, const FString& Token)
{
	const FString Normalized = NormalizeAttachmentFocusToken(Token);
	if (!Normalized.IsEmpty())
	{
		Tokens.Add(Normalized);
	}
}

void CollectGeneratedAttachmentIconMeshesFromProperty(const FProperty* Property, const void* ValuePtr, TArray<UObject*>& OutMeshes)
{
	if (!Property || !ValuePtr)
	{
		return;
	}

	if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		UObject* Object = ObjectProperty->GetObjectPropertyValue(ValuePtr);
		if (IsSupportedGeneratedAttachmentIconMesh(Object))
		{
			OutMeshes.AddUnique(Object);
		}
		return;
	}

	if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
	{
		const FSoftObjectPtr SoftObject = SoftObjectProperty->GetPropertyValue(ValuePtr);
		UObject* Object = SoftObject.LoadSynchronous();
		if (IsSupportedGeneratedAttachmentIconMesh(Object))
		{
			OutMeshes.AddUnique(Object);
		}
		return;
	}

	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
		for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
		{
			CollectGeneratedAttachmentIconMeshesFromProperty(ArrayProperty->Inner, ArrayHelper.GetRawPtr(Index), OutMeshes);
		}
	}
}

void CollectGeneratedAttachmentIconTokensFromProperty(const FProperty* Property, const void* ValuePtr, TSet<FString>& OutTokens)
{
	if (!Property || !ValuePtr)
	{
		return;
	}

	if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
	{
		AddGeneratedAttachmentIconToken(OutTokens, NameProperty->GetPropertyValue(ValuePtr).ToString());
		return;
	}

	if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
	{
		AddGeneratedAttachmentIconToken(OutTokens, StringProperty->GetPropertyValue(ValuePtr));
		return;
	}

	if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
	{
		AddGeneratedAttachmentIconToken(OutTokens, TextProperty->GetPropertyValue(ValuePtr).ToString());
		return;
	}

	if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		const int64 Value = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
		if (const UEnum* Enum = EnumProperty->GetEnum())
		{
			AddGeneratedAttachmentIconToken(OutTokens, Enum->GetNameStringByValue(Value));
		}
		return;
	}

	if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
	{
		if (const UEnum* Enum = ByteProperty->Enum)
		{
			AddGeneratedAttachmentIconToken(OutTokens, Enum->GetNameStringByValue(ByteProperty->GetPropertyValue(ValuePtr)));
		}
		return;
	}

	if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		if (const UObject* Object = ObjectProperty->GetObjectPropertyValue(ValuePtr))
		{
			AddGeneratedAttachmentIconToken(OutTokens, Object->GetName());
			AddGeneratedAttachmentIconToken(OutTokens, Object->GetPathName());
		}
		return;
	}

	if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
	{
		const FSoftObjectPath SoftObjectPath = SoftObjectProperty->GetPropertyValue(ValuePtr).ToSoftObjectPath();
		if (SoftObjectPath.IsValid())
		{
			AddGeneratedAttachmentIconToken(OutTokens, SoftObjectPath.GetAssetName());
			AddGeneratedAttachmentIconToken(OutTokens, SoftObjectPath.ToString());
		}
		return;
	}

	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
		for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
		{
			CollectGeneratedAttachmentIconTokensFromProperty(ArrayProperty->Inner, ArrayHelper.GetRawPtr(Index), OutTokens);
		}
	}
}

FString MakeGeneratedAttachmentIconObjectPath(const UObject* Mesh)
{
	if (!Mesh)
	{
		return FString();
	}

	const FString IconAssetName = FString::Printf(TEXT("T_%s_Icon"), *Mesh->GetName());
	return FString::Printf(TEXT("/Game/UI/Generated/Icons/%s.%s"), *IconAssetName, *IconAssetName);
}

TArray<FGeneratedAttachmentIconDefinition>& GeneratedAttachmentIconDefinitions()
{
	static bool bBuiltDefinitions = false;
	static TArray<FGeneratedAttachmentIconDefinition> Definitions;

	if (bBuiltDefinitions)
	{
		return Definitions;
	}
	bBuiltDefinitions = true;

	static const TCHAR* AttachmentDataTablePaths[] =
	{
		TEXT("/Game/MP_System_V3/Game/Blueprints/DataTables/DT_Optics.DT_Optics"),
		TEXT("/Game/MP_System_V3/Game/Blueprints/DataTables/DT_SideRail.DT_SideRail"),
		TEXT("/Game/MP_System_V3/Game/Blueprints/DataTables/DT_Underbarrel.DT_Underbarrel"),
		TEXT("/Game/MP_System_V3/Game/Blueprints/DataTables/DT_Muzzle.DT_Muzzle")
	};

	for (const TCHAR* DataTablePath : AttachmentDataTablePaths)
	{
		UDataTable* AttachmentTable = LoadObject<UDataTable>(nullptr, DataTablePath);
		if (!AttachmentTable || !AttachmentTable->GetRowStruct())
		{
			continue;
		}

		const UScriptStruct* RowStruct = AttachmentTable->GetRowStruct();
		for (const TPair<FName, uint8*>& RowPair : AttachmentTable->GetRowMap())
		{
			TSet<FString> RowTokens;
			AddGeneratedAttachmentIconToken(RowTokens, RowPair.Key.ToString());

			TArray<UObject*> RowMeshes;
			for (TFieldIterator<FProperty> PropertyIt(RowStruct); PropertyIt; ++PropertyIt)
			{
				const FProperty* Property = *PropertyIt;
				const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(RowPair.Value);
				CollectGeneratedAttachmentIconTokensFromProperty(Property, ValuePtr, RowTokens);
				CollectGeneratedAttachmentIconMeshesFromProperty(Property, ValuePtr, RowMeshes);
			}

			for (UObject* Mesh : RowMeshes)
			{
				FGeneratedAttachmentIconDefinition Definition;
				Definition.Tokens = RowTokens;
				AddGeneratedAttachmentIconToken(Definition.Tokens, Mesh->GetName());
				AddGeneratedAttachmentIconToken(Definition.Tokens, Mesh->GetPathName());
				Definition.IconObjectPath = MakeGeneratedAttachmentIconObjectPath(Mesh);
				if (!Definition.IconObjectPath.IsEmpty())
				{
					Definitions.Add(MoveTemp(Definition));
				}
			}
		}
	}

	return Definitions;
}

bool DoesGeneratedAttachmentIconDefinitionMatchToken(
	const FGeneratedAttachmentIconDefinition& Definition,
	const FString& Token)
{
	const FString NormalizedToken = NormalizeAttachmentFocusToken(Token);
	if (NormalizedToken.Len() < 3)
	{
		return false;
	}

	for (const FString& DefinitionToken : Definition.Tokens)
	{
		if (DefinitionToken.Equals(NormalizedToken, ESearchCase::IgnoreCase)
			|| (NormalizedToken.Len() >= 4 && DefinitionToken.Contains(NormalizedToken, ESearchCase::IgnoreCase))
			|| (DefinitionToken.Len() >= 4 && NormalizedToken.Contains(DefinitionToken, ESearchCase::IgnoreCase)))
		{
			return true;
		}
	}

	return false;
}

const FGeneratedAttachmentIconDefinition* FindGeneratedAttachmentIconDefinitionForText(const FString& Text)
{
	const FString NormalizedText = NormalizeAttachmentFocusToken(Text);
	if (NormalizedText.Len() < 3)
	{
		return nullptr;
	}

	for (const FGeneratedAttachmentIconDefinition& Definition : GeneratedAttachmentIconDefinitions())
	{
		if (DoesGeneratedAttachmentIconDefinitionMatchToken(Definition, NormalizedText))
		{
			return &Definition;
		}
	}

	return nullptr;
}

UButton* FindOwningButton(UWidget* Widget)
{
	UWidget* Current = Widget;
	for (int32 Depth = 0; Current && Depth < 8; ++Depth)
	{
		if (UButton* Button = Cast<UButton>(Current))
		{
			return Button;
		}

		if (UPanelWidget* Parent = Current->GetParent())
		{
			Current = Parent;
			continue;
		}

		break;
	}

	return nullptr;
}

UTexture2D* LoadGeneratedAttachmentIcon(const FGeneratedAttachmentIconDefinition& Definition)
{
	if (!Definition.CachedIcon.IsValid() && !Definition.IconObjectPath.IsEmpty())
	{
		Definition.CachedIcon = LoadObject<UTexture2D>(nullptr, *Definition.IconObjectPath);
	}
	return Definition.CachedIcon.Get();
}

float GeneratedAttachmentHoverScale(const bool bHovered)
{
	return bHovered ? 1.07f : 1.0f;
}

UTexture2D* LoadSlateWhiteSquareTexture()
{
	static TWeakObjectPtr<UTexture2D> CachedTexture;
	if (!CachedTexture.IsValid())
	{
		CachedTexture = LoadObject<UTexture2D>(
			nullptr,
			TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	}
	return CachedTexture.Get();
}

FSlateBrush MakeGeneratedAttachmentIconBrush(UTexture2D* Texture)
{
	FSlateBrush Brush;
	Brush.DrawAs = Texture ? ESlateBrushDrawType::Image : ESlateBrushDrawType::NoDrawType;
	Brush.ImageSize = GeneratedAttachmentIconSize();
	Brush.TintColor = FSlateColor(FLinearColor::White);
	if (Texture)
	{
		Brush.SetResourceObject(Texture);
	}
	return Brush;
}

FSlateBrush MakeGeneratedAttachmentFrameBrush(const bool bVisible)
{
	FSlateBrush Brush;
	Brush.DrawAs = bVisible ? ESlateBrushDrawType::Border : ESlateBrushDrawType::NoDrawType;
	Brush.ImageSize = GeneratedAttachmentIconSize();
	Brush.Margin = FMargin(0.004f, 0.016f);
	Brush.TintColor = FSlateColor(bVisible ? GeneratedAttachmentIconSelectionFrameColor() : FLinearColor::Transparent);
	if (bVisible)
	{
		Brush.SetResourceObject(LoadSlateWhiteSquareTexture());
	}
	return Brush;
}

bool IsGenericAttachmentHighlightToken(const FString& NormalizedToken)
{
	if (NormalizedToken.Len() < 3)
	{
		return true;
	}

	static const TCHAR* GenericTokens[] =
	{
		TEXT("Muzzle"),
		TEXT("MuzzleSilencerco"),
		TEXT("Optics"),
		TEXT("Optic"),
		TEXT("RearSight"),
		TEXT("AimTarget"),
		TEXT("ADSEye"),
		TEXT("ATBackup"),
		TEXT("SideRail"),
		TEXT("Canted"),
		TEXT("Backup"),
		TEXT("Underbarrel"),
		TEXT("Foregrip"),
		TEXT("Socket"),
		TEXT("StaticMesh"),
		TEXT("SkeletalMesh"),
		TEXT("ScriptEngineStaticMesh"),
		TEXT("ScriptCoreUObjectObject")
	};

	for (const TCHAR* GenericToken : GenericTokens)
	{
		if (NormalizedToken.Equals(GenericToken, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

void AddAttachmentHighlightToken(TSet<FString>& OutTokens, const FString& Text)
{
	FString CleanText = Text;
	CleanText.TrimStartAndEndInline();
	CleanText.RemoveFromStart(TEXT("Default__"));
	CleanText.RemoveFromEnd(TEXT("_C"));

	const auto AddOne = [&OutTokens](const FString& Candidate)
	{
		const FString Normalized = NormalizeAttachmentFocusToken(Candidate);
		if (!IsEmptyAttachmentFocusValue(Normalized) && !IsGenericAttachmentHighlightToken(Normalized))
		{
			OutTokens.Add(Normalized);
		}
	};

	AddOne(CleanText);

	FString AssetLeaf;
	if (CleanText.Split(TEXT("/"), nullptr, &AssetLeaf, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
	{
		FString AssetName;
		if (AssetLeaf.Split(TEXT("."), &AssetName, nullptr, ESearchCase::CaseSensitive, ESearchDir::FromStart))
		{
			AddOne(AssetName);
		}
		AddOne(AssetLeaf);
	}
}

bool IsAttachmentSelectionOptionText(const FString& Text)
{
	const FString Normalized = NormalizeAttachmentFocusToken(Text);
	if (Normalized.IsEmpty())
	{
		return false;
	}

	static const TCHAR* GroupHeaderTokens[] =
	{
		TEXT("Muzzle"),
		TEXT("Optics"),
		TEXT("SideRail"),
		TEXT("Underbarrel")
	};

	for (const TCHAR* GroupHeaderToken : GroupHeaderTokens)
	{
		if (Normalized.Equals(GroupHeaderToken, ESearchCase::IgnoreCase))
		{
			return false;
		}
	}

	return true;
}

int32 ScoreSelectionTextAgainstTokens(const FString& Text, const TSet<FString>& Tokens)
{
	const FString NormalizedText = NormalizeAttachmentFocusToken(Text);
	if (NormalizedText.IsEmpty())
	{
		return INDEX_NONE;
	}

	int32 BestScore = INDEX_NONE;
	for (const FString& Token : Tokens)
	{
		if (Token.Equals(NormalizedText, ESearchCase::IgnoreCase))
		{
			BestScore = FMath::Max(BestScore, 100000 + NormalizedText.Len());
		}
		else if (Token.Contains(NormalizedText, ESearchCase::IgnoreCase))
		{
			BestScore = FMath::Max(BestScore, 50000 + NormalizedText.Len());
		}
		else if (NormalizedText.Contains(Token, ESearchCase::IgnoreCase))
		{
			BestScore = FMath::Max(BestScore, 40000 + Token.Len());
		}
	}

	return BestScore;
}

int32 ScoreAttachmentSelectionTextAgainstTokens(const FString& Text, const TSet<FString>& Tokens)
{
	return IsAttachmentSelectionOptionText(Text)
		? ScoreSelectionTextAgainstTokens(Text, Tokens)
		: INDEX_NONE;
}

int32 ScoreAttachmentSelectionTextAgainstTokensWithGeneratedIconAlias(const FString& Text, const TSet<FString>& Tokens)
{
	int32 Score = ScoreAttachmentSelectionTextAgainstTokens(Text, Tokens);
	const FGeneratedAttachmentIconDefinition* Definition = FindGeneratedAttachmentIconDefinitionForText(Text);
	if (!Definition)
	{
		return Score;
	}

	for (const FString& Token : Tokens)
	{
		if (DoesGeneratedAttachmentIconDefinitionMatchToken(*Definition, Token))
		{
			Score = FMath::Max(Score, 90000);
		}
	}

	return Score;
}

bool IsInstalledAttachmentPropertyName(const FString& PropertyName)
{
	const FString Normalized = NormalizeAttachmentFocusToken(PropertyName);
	return Normalized.Contains(TEXT("ActiveAttachments"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("MuzzleItem"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("OpticsItem"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("OpticItem"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("SideRailItem"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("Underbarreltem"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("UnderbarrelItem"), ESearchCase::IgnoreCase);
}

bool IsCompatibleAttachmentListPropertyName(const FString& PropertyName)
{
	const FString Normalized = NormalizeAttachmentFocusToken(PropertyName);
	return Normalized.Contains(TEXT("Compatible"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("AttachmentList"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("AttachmentsList"), ESearchCase::IgnoreCase);
}

void AppendAttachmentFocusValueTexts(const FProperty* Property, const void* ValuePtr, TArray<FString>& OutTexts)
{
	if (!Property || !ValuePtr)
	{
		return;
	}

	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
		for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
		{
			AppendAttachmentFocusValueTexts(ArrayProperty->Inner, ArrayHelper.GetRawPtr(Index), OutTexts);
		}
		return;
	}

	if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		const int64 Value = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
		if (const UEnum* Enum = EnumProperty->GetEnum())
		{
			OutTexts.Add(Enum->GetNameStringByValue(Value));
			OutTexts.Add(Enum->GetDisplayNameTextByValue(Value).ToString());
		}
		return;
	}

	if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
	{
		const uint8 Value = ByteProperty->GetPropertyValue(ValuePtr);
		if (const UEnum* Enum = ByteProperty->Enum)
		{
			OutTexts.Add(Enum->GetNameStringByValue(Value));
			OutTexts.Add(Enum->GetDisplayNameTextByValue(Value).ToString());
		}
		else
		{
			OutTexts.Add(FString::FromInt(Value));
		}
		return;
	}

	if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
	{
		OutTexts.Add(NameProperty->GetPropertyValue(ValuePtr).ToString());
		return;
	}

	if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
	{
		OutTexts.Add(StringProperty->GetPropertyValue(ValuePtr));
		return;
	}

	if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
	{
		OutTexts.Add(TextProperty->GetPropertyValue(ValuePtr).ToString());
		return;
	}

	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		if (!StructProperty->Struct)
		{
			return;
		}

		for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
		{
			const FProperty* ChildProperty = *It;
			if (!ChildProperty)
			{
				continue;
			}

			AppendAttachmentFocusValueTexts(
				ChildProperty,
				ChildProperty->ContainerPtrToValuePtr<void>(ValuePtr),
				OutTexts);
		}
		return;
	}

	if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		if (const UObject* ObjectValue = ObjectProperty->GetObjectPropertyValue(ValuePtr))
		{
			OutTexts.Add(ObjectValue->GetName());
			OutTexts.Add(ObjectValue->GetPathName());
			if (const UClass* ObjectClass = ObjectValue->GetClass())
			{
				OutTexts.Add(ObjectClass->GetPathName());
			}
		}
	}
}

uint32 CombineAttachmentFocusSignature(uint32 Signature, const FString& Text)
{
	const uint32 ValueHash = GetTypeHash(NormalizeAttachmentFocusToken(Text));
	return HashCombine(Signature == 0 ? 0x811C9DC5u : Signature, ValueHash);
}
}

void UTMMenuViewerMeshTransitionSubsystem::Deinitialize()
{
	RestoreMainMenuCameraDrift();
	RestoreLoadoutPostProcess();
	RestoreMainMenuBackGlow();
	RestoreLoadoutBackGlow();
	RestoreLoadoutFOV();
	RestoreMenuFOV();
	RestoreAttachmentSelectionHighlight();
	RestoreWeaponSelectionHighlight();
	MenuViewerStates.Empty();
	Super::Deinitialize();
}

void UTMMenuViewerMeshTransitionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const bool bLoadoutFOVVisible =
		IsLoadoutFOVVisible(World);
	const bool bLoadoutFOVEnabled =
		CVarLoadoutFOV.GetValueOnGameThread() != 0 && bLoadoutFOVVisible;
	const bool bAttachmentsVisible = IsAttachmentsVisible(World);
	const bool bMainMenuVisible = IsMainMenuVisible(World);
	const bool bPostProcessVisible =
		CVarLoadoutPostProcess.GetValueOnGameThread() != 0
		&& (bMainMenuVisible || bLoadoutFOVVisible);
	UpdateMenuFOV(World, bMainMenuVisible, bLoadoutFOVVisible);
	UpdateLoadoutFOV(World, bLoadoutFOVEnabled);
	UpdateLoadoutBackGlowTiming(bLoadoutFOVVisible, DeltaTime);
	UpdateAttachmentsPreviewBrightnessTiming(bAttachmentsVisible, bLoadoutFOVVisible, DeltaTime);
	UpdateMainMenuCameraDrift(World, bMainMenuVisible || bLoadoutFOVVisible, bLoadoutFOVVisible, bAttachmentsVisible, DeltaTime);
	UpdateLoadoutPostProcess(World, bPostProcessVisible, bLoadoutFOVVisible, bAttachmentsVisible);
	UpdateAttachmentSelectionHighlight(World, bAttachmentsVisible);
	UpdateWeaponSelectionHighlight(World, bLoadoutFOVVisible && !bAttachmentsVisible);
	if (bLoadoutFOVVisible)
	{
		UpdateMainMenuBackGlow(World, false, DeltaTime);
		UpdateLoadoutBackGlow(World, true, DeltaTime);
	}
	else
	{
		UpdateLoadoutBackGlow(World, false, DeltaTime);
		UpdateMainMenuBackGlow(World, bMainMenuVisible, DeltaTime);
	}

	if (CVarMenuViewerMeshTransition.GetValueOnGameThread() == 0)
	{
		return;
	}

	const bool bLoadoutPreviewVisible = IsLoadoutPreviewVisible(World);

	for (auto It = MenuViewerStates.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (IsMenuViewerActor(Actor))
		{
			TrackMenuViewer(Actor, DeltaTime, bLoadoutPreviewVisible);
		}
	}
}

TStatId UTMMenuViewerMeshTransitionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTMMenuViewerMeshTransitionSubsystem, STATGROUP_Tickables);
}

bool UTMMenuViewerMeshTransitionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

void UTMMenuViewerMeshTransitionSubsystem::TrackMenuViewer(
	AActor* Actor,
	const float DeltaTime,
	const bool bLoadoutPreviewVisible)
{
	if (!Actor)
	{
		return;
	}

	FMenuViewerState& State = MenuViewerStates.FindOrAdd(Actor);
	State.Actor = Actor;
	UpdateAttachedWeaponVisibility(Actor, State, bLoadoutPreviewVisible);

	USkeletalMeshComponent* VestComponent = ReadMeshComponentProperty(Actor, TEXT("Vest"));
	if (!VestComponent || !VestComponent->IsRegistered())
	{
		return;
	}

	USkeletalMesh* CurrentMesh = VestComponent->GetSkeletalMeshAsset();
	if (!CurrentMesh)
	{
		return;
	}

	State.VestComponent = VestComponent;
	const FBeatSyncSnapshot BeatSync = FindActiveBeatSync(Actor->GetWorld());

	USkeletalMeshComponent* LeaderComponent = ResolveLeaderComponent(Actor, VestComponent);
	if (!LeaderComponent)
	{
		CaptureStableState(State, VestComponent);
		return;
	}

	const bool bHasTrackedMesh = State.LastMesh.IsValid();
	if (bHasTrackedMesh && State.LastMesh.Get() != CurrentMesh)
	{
		NoteMeshChanged(State, VestComponent, State.LastMesh.Get());
		CaptureStableState(State, VestComponent);
		return;
	}

	if (!bHasTrackedMesh)
	{
		if (GLastMenuViewerVestMesh.IsValid() && GLastMenuViewerVestMesh.Get() != CurrentMesh)
		{
			NoteMeshChanged(State, VestComponent, GLastMenuViewerVestMesh.Get());
		}
		CaptureStableState(State, VestComponent);
		return;
	}

	CaptureStableState(State, VestComponent);
	UpdateAutoCycle(State, VestComponent, LeaderComponent, DeltaTime, BeatSync);
}

void UTMMenuViewerMeshTransitionSubsystem::NoteMeshChanged(
	const FMenuViewerState& State,
	USkeletalMeshComponent* VestComponent,
	USkeletalMesh* PreviousMesh) const
{
	if (!VestComponent || !PreviousMesh)
	{
		return;
	}

	UE_LOG(
		LogTMMenuViewerMeshTransition,
		Display,
		TEXT("Menu mesh changed: %s -> %s on %s"),
		*GetNameSafe(PreviousMesh),
		*GetNameSafe(VestComponent->GetSkeletalMeshAsset()),
		*GetNameSafe(State.Actor.Get()));
}

void UTMMenuViewerMeshTransitionSubsystem::CaptureStableState(FMenuViewerState& State, USkeletalMeshComponent* VestComponent)
{
	if (!VestComponent)
	{
		return;
	}

	State.LastMesh = VestComponent->GetSkeletalMeshAsset();
	GLastMenuViewerVestMesh = State.LastMesh;
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateAutoCycle(
	FMenuViewerState& State,
	USkeletalMeshComponent* VestComponent,
	USkeletalMeshComponent* LeaderComponent,
	const float DeltaTime,
	const FBeatSyncSnapshot& BeatSync)
{
	if (!IsAutoCycleEnabled() || !VestComponent || !LeaderComponent)
	{
		return;
	}

	State.AutoCycleElapsedSeconds += FMath::Max(0.f, DeltaTime);
	if (State.AutoCycleElapsedSeconds < GetAutoCycleInterval())
	{
		UpdateRhythmPulseMemory(State, BeatSync);
		if (BeatSync.BeatIndex > State.LastObservedBeatIndex)
		{
			State.LastObservedBeatIndex = BeatSync.BeatIndex;
		}
		return;
	}

	UWorld* World = VestComponent->GetWorld();
	if (IsBeatSyncEnabled() && BeatSync.bHasActiveFollower)
	{
		const float WorldTimeSeconds = World ? World->GetTimeSeconds() : 0.0f;
		if (!IsReadyForBeatSyncedCycle(State, BeatSync, WorldTimeSeconds))
		{
			return;
		}
	}

	USkeletalMesh* PreviousMesh = VestComponent->GetSkeletalMeshAsset();
	USkeletalMesh* NextMesh = ChooseNextMenuMesh(PreviousMesh);
	if (!PreviousMesh || !NextMesh || PreviousMesh == NextMesh)
	{
		return;
	}

	VestComponent->SetSkeletalMeshAsset(NextMesh);
	State.AutoCycleElapsedSeconds = 0.f;
	NoteMeshChanged(State, VestComponent, PreviousMesh);
	CaptureStableState(State, VestComponent);

	UE_LOG(
		LogTMMenuViewerMeshTransition,
		Display,
		TEXT("Auto-cycled menu mesh: %s -> %s Beat=%d PhraseBars=%d Strength=%.2f Env=%.2f"),
		*GetNameSafe(PreviousMesh),
		*GetNameSafe(NextMesh),
		BeatSync.BeatIndex,
		GetBeatSyncPhraseBars(),
		BeatSync.LastBeatStrength,
		BeatSync.NormalizedEnvelopeValue);
}

bool UTMMenuViewerMeshTransitionSubsystem::IsMenuViewerActor(const AActor* Actor)
{
	const UClass* ActorClass = Actor ? Actor->GetClass() : nullptr;
	return ActorClass && ActorClass->GetPathName().Contains(TEXT("BP_MenuViewer"));
}

bool UTMMenuViewerMeshTransitionSubsystem::IsAttachedWeaponActor(const AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}

	if (Actor->IsA(AGun::StaticClass()))
	{
		return true;
	}

	const UClass* ActorClass = Actor->GetClass();
	const FString ActorPath = Actor->GetPathName();
	const FString ClassPath = ActorClass ? ActorClass->GetPathName() : FString();
	return ActorPath.Contains(TEXT("/Weapons/")) || ClassPath.Contains(TEXT("/Weapons/"));
}

bool UTMMenuViewerMeshTransitionSubsystem::IsLoadoutFOVVisible(UWorld* World)
{
	if (!World)
	{
		return false;
	}

	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->GetWorld() != World)
		{
			continue;
		}

		const UClass* WidgetClass = Widget->GetClass();
		const FString ClassName = WidgetClass ? WidgetClass->GetName() : FString();
		if (!ClassName.Contains(TEXT("W_Loadout")) && !ClassName.Contains(TEXT("W_Attachments")))
		{
			continue;
		}

		if (!Widget->IsVisible())
		{
			continue;
		}

		const ESlateVisibility Visibility = Widget->GetVisibility();
		if (Visibility == ESlateVisibility::Collapsed || Visibility == ESlateVisibility::Hidden)
		{
			continue;
		}

		if (Widget->IsInViewport())
		{
			return true;
		}

		const FVector2D DrawnSize = Widget->GetCachedGeometry().GetLocalSize();
		if (DrawnSize.X > 16.0f && DrawnSize.Y > 16.0f)
		{
			return true;
		}
	}

	return false;
}

bool UTMMenuViewerMeshTransitionSubsystem::IsAttachmentsVisible(UWorld* World)
{
	if (!World)
	{
		return false;
	}

	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->GetWorld() != World)
		{
			continue;
		}

		const UClass* WidgetClass = Widget->GetClass();
		const FString ClassPath = WidgetClass ? WidgetClass->GetPathName() : FString();
		const FString ClassName = WidgetClass ? WidgetClass->GetName() : FString();
		if (!ClassName.Contains(TEXT("W_Attachments")) && !ClassPath.Contains(TEXT("W_Attachments")))
		{
			continue;
		}

		if (!Widget->IsVisible())
		{
			continue;
		}

		const ESlateVisibility Visibility = Widget->GetVisibility();
		if (Visibility == ESlateVisibility::Collapsed || Visibility == ESlateVisibility::Hidden)
		{
			continue;
		}

		if (Widget->IsInViewport())
		{
			return true;
		}

		const FVector2D DrawnSize = Widget->GetCachedGeometry().GetLocalSize();
		if (DrawnSize.X > 16.0f && DrawnSize.Y > 16.0f)
		{
			return true;
		}
	}

	return false;
}

bool UTMMenuViewerMeshTransitionSubsystem::IsMainMenuVisible(UWorld* World)
{
	if (!World)
	{
		return false;
	}

	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->GetWorld() != World)
		{
			continue;
		}

		const UClass* WidgetClass = Widget->GetClass();
		if (!WidgetClass || !WidgetClass->GetPathName().Contains(TEXT("W_MainMenu")))
		{
			continue;
		}

		if (!Widget->IsVisible())
		{
			continue;
		}

		const ESlateVisibility Visibility = Widget->GetVisibility();
		if (Visibility == ESlateVisibility::Collapsed || Visibility == ESlateVisibility::Hidden)
		{
			continue;
		}

		if (Widget->IsInViewport())
		{
			return true;
		}

		const FVector2D DrawnSize = Widget->GetCachedGeometry().GetLocalSize();
		if (DrawnSize.X > 16.0f && DrawnSize.Y > 16.0f)
		{
			return true;
		}
	}

	return false;
}

bool UTMMenuViewerMeshTransitionSubsystem::IsLoadoutPreviewVisible(UWorld* World)
{
	if (!World)
	{
		return false;
	}

	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!Widget || Widget->HasAnyFlags(RF_ClassDefaultObject))
		{
			continue;
		}

		const UClass* WidgetClass = Widget->GetClass();
		if (!WidgetClass || !WidgetClass->GetPathName().Contains(TEXT("W_Loadout")))
		{
			continue;
		}

		if (Widget->GetWorld() != World)
		{
			continue;
		}

		const ESlateVisibility Visibility = Widget->GetVisibility();
		if (Visibility == ESlateVisibility::Collapsed || Visibility == ESlateVisibility::Hidden)
		{
			continue;
		}

		const FObjectPropertyBase* ActiveWeaponProperty =
			FindFProperty<FObjectPropertyBase>(WidgetClass, TEXT("ActiveWeapon"));
		if (!ActiveWeaponProperty)
		{
			continue;
		}

		if (IsValid(Cast<AActor>(ActiveWeaponProperty->GetObjectPropertyValue_InContainer(Widget))))
		{
			return true;
		}
	}

	return false;
}

bool UTMMenuViewerMeshTransitionSubsystem::IsVisibleLoadoutPreviewWeaponActor(AActor* Actor)
{
	if (!IsValid(Actor)
		|| Actor->HasAnyFlags(RF_ClassDefaultObject)
		|| Actor->IsHidden()
		|| !IsAttachedWeaponActor(Actor)
		|| Actor->GetAttachParentActor())
	{
		return false;
	}

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(Actor);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (IsValid(PrimitiveComponent) && PrimitiveComponent->IsVisible())
		{
			return true;
		}
	}

	return false;
}

bool UTMMenuViewerMeshTransitionSubsystem::IsLoadoutBackGlowLight(const ULightComponent* LightComponent)
{
	if (!IsValid(LightComponent))
	{
		return false;
	}

	const AActor* Owner = LightComponent->GetOwner();
	if (!IsValid(Owner))
	{
		return false;
	}

	const UClass* ComponentClass = LightComponent->GetClass();
	const FString ComponentClassName = ComponentClass ? ComponentClass->GetName() : FString();
	if (!ComponentClassName.Contains(TEXT("RectLight")))
	{
		return false;
	}

	const FString OwnerName = Owner->GetName();
	if (!OwnerName.Contains(TEXT("RectLight")))
	{
		return false;
	}

	return Owner->GetActorLocation().X <= -250.0f;
}

bool UTMMenuViewerMeshTransitionSubsystem::IsLoadoutBackGlowVisual(const UPrimitiveComponent* PrimitiveComponent)
{
	if (!IsValid(PrimitiveComponent))
	{
		return false;
	}

	if (PrimitiveComponent->ComponentHasTag(TEXT("TMSpawnedLoadoutBackGlowVisual")))
	{
		return false;
	}

	const AActor* Owner = PrimitiveComponent->GetOwner();
	if (!IsValid(Owner))
	{
		return false;
	}

	const UParticleSystemComponent* ParticleComponent = Cast<UParticleSystemComponent>(PrimitiveComponent);
	if (!ParticleComponent || !ParticleComponent->Template)
	{
		return false;
	}

	const FString OwnerName = Owner->GetName();
	const FString TemplatePath = ParticleComponent->Template->GetPathName();
	return OwnerName.Contains(TEXT("P_Ambient_Glow"))
		|| TemplatePath.Contains(TEXT("P_Ambient_Glow"));
}

AActor* UTMMenuViewerMeshTransitionSubsystem::ResolveLoadoutPreviewWeaponActor(
	UWorld* World,
	const FVector& CameraLocation) const
{
	if (!World)
	{
		return nullptr;
	}

	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->GetWorld() != World)
		{
			continue;
		}

		const UClass* WidgetClass = Widget->GetClass();
		if (!WidgetClass || !WidgetClass->GetPathName().Contains(TEXT("W_Loadout")))
		{
			continue;
		}

		const ESlateVisibility Visibility = Widget->GetVisibility();
		if (Visibility == ESlateVisibility::Collapsed || Visibility == ESlateVisibility::Hidden)
		{
			continue;
		}

		const FObjectPropertyBase* ActiveWeaponProperty =
			FindFProperty<FObjectPropertyBase>(WidgetClass, TEXT("ActiveWeapon"));
		if (!ActiveWeaponProperty)
		{
			continue;
		}

		AActor* ActiveWeapon = Cast<AActor>(ActiveWeaponProperty->GetObjectPropertyValue_InContainer(Widget));
		if (IsVisibleLoadoutPreviewWeaponActor(ActiveWeapon))
		{
			return ActiveWeapon;
		}
	}

	AActor* BestActor = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsVisibleLoadoutPreviewWeaponActor(Actor))
		{
			continue;
		}

		const float DistanceSq = FVector::DistSquared(CameraLocation, Actor->GetActorLocation());
		if (FMath::IsFinite(DistanceSq) && DistanceSq < BestDistanceSq)
		{
			BestActor = Actor;
			BestDistanceSq = DistanceSq;
		}
	}

	return BestActor;
}

AActor* UTMMenuViewerMeshTransitionSubsystem::ResolveAttachmentsPreviewWeaponActor(
	UWorld* World,
	const FVector& CameraLocation) const
{
	if (!World)
	{
		return nullptr;
	}

	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->GetWorld() != World)
		{
			continue;
		}

		const UClass* WidgetClass = Widget->GetClass();
		if (!WidgetClass || !WidgetClass->GetPathName().Contains(TEXT("W_Attachments")))
		{
			continue;
		}

		if (!IsVisibleWidgetInstance(Widget))
		{
			continue;
		}

		const FObjectPropertyBase* ActiveWeaponProperty =
			FindFProperty<FObjectPropertyBase>(WidgetClass, TEXT("ActiveWeapon"));
		if (!ActiveWeaponProperty)
		{
			continue;
		}

		AActor* ActiveWeapon = Cast<AActor>(ActiveWeaponProperty->GetObjectPropertyValue_InContainer(Widget));
		if (IsValid(ActiveWeapon))
		{
			return ActiveWeapon;
		}
	}

	AActor* BestActor = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsVisibleLoadoutPreviewWeaponActor(Actor))
		{
			continue;
		}

		const float DistanceSq = FVector::DistSquared(CameraLocation, Actor->GetActorLocation());
		if (FMath::IsFinite(DistanceSq) && DistanceSq < BestDistanceSq)
		{
			BestActor = Actor;
			BestDistanceSq = DistanceSq;
		}
	}

	return BestActor;
}

APlayerCameraManager* UTMMenuViewerMeshTransitionSubsystem::ResolvePlayerCameraManager(UWorld* World)
{
	return World ? UGameplayStatics::GetPlayerCameraManager(World, 0) : nullptr;
}

UCameraComponent* UTMMenuViewerMeshTransitionSubsystem::ResolveActiveCameraComponent(UWorld* World)
{
	APlayerCameraManager* PlayerCameraManager = ResolvePlayerCameraManager(World);
	AActor* ViewTarget = PlayerCameraManager ? PlayerCameraManager->GetViewTarget() : nullptr;
	if (ViewTarget)
	{
		TInlineComponentArray<UCameraComponent*> CameraComponents(ViewTarget);
		for (UCameraComponent* CameraComponent : CameraComponents)
		{
			if (CameraComponent && CameraComponent->IsActive())
			{
				return CameraComponent;
			}
		}

		if (CameraComponents.Num() > 0)
		{
			return CameraComponents[0];
		}
	}

	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsMenuViewerActor(Actor))
		{
			continue;
		}

		if (UCameraComponent* CameraComponent = Actor->FindComponentByClass<UCameraComponent>())
		{
			return CameraComponent;
		}
	}

	return nullptr;
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateAttachedWeaponVisibility(
	AActor* Actor,
	FMenuViewerState& State,
	const bool bHide)
{
	if (bHide)
	{
		HideAttachedWeaponActors(Actor, State);
	}
	else
	{
		RestoreAttachedWeaponActors(State);
	}
}

void UTMMenuViewerMeshTransitionSubsystem::HideAttachedWeaponActors(AActor* Actor, FMenuViewerState& State)
{
	if (!Actor)
	{
		return;
	}

	TArray<AActor*> AttachedActors;
	Actor->GetAttachedActors(AttachedActors, true, true);
	for (AActor* AttachedActor : AttachedActors)
	{
		if (!IsAttachedWeaponActor(AttachedActor))
		{
			continue;
		}

		if (!State.AttachedWeaponActorHiddenStates.Contains(AttachedActor))
		{
			State.AttachedWeaponActorHiddenStates.Add(AttachedActor, AttachedActor->IsHidden());
		}
		AttachedActor->SetActorHiddenInGame(true);

		TArray<UPrimitiveComponent*> PrimitiveComponents;
		AttachedActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
		for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			if (!PrimitiveComponent)
			{
				continue;
			}

			if (!State.AttachedWeaponComponentVisibilityStates.Contains(PrimitiveComponent))
			{
				FComponentVisibilityState VisibilityState;
				VisibilityState.bVisible = PrimitiveComponent->IsVisible();
				VisibilityState.bHiddenInGame = PrimitiveComponent->bHiddenInGame;
				State.AttachedWeaponComponentVisibilityStates.Add(PrimitiveComponent, VisibilityState);
			}

			PrimitiveComponent->SetVisibility(false, true);
			PrimitiveComponent->SetHiddenInGame(true, true);
		}
	}
}

void UTMMenuViewerMeshTransitionSubsystem::RestoreAttachedWeaponActors(FMenuViewerState& State)
{
	for (const TPair<TWeakObjectPtr<AActor>, bool>& ActorState : State.AttachedWeaponActorHiddenStates)
	{
		if (AActor* Actor = ActorState.Key.Get())
		{
			Actor->SetActorHiddenInGame(ActorState.Value);
		}
	}
	State.AttachedWeaponActorHiddenStates.Empty();

	for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, FComponentVisibilityState>& ComponentState :
		State.AttachedWeaponComponentVisibilityStates)
	{
		if (UPrimitiveComponent* PrimitiveComponent = ComponentState.Key.Get())
		{
			PrimitiveComponent->SetVisibility(ComponentState.Value.bVisible, true);
			PrimitiveComponent->SetHiddenInGame(ComponentState.Value.bHiddenInGame, true);
		}
	}
	State.AttachedWeaponComponentVisibilityStates.Empty();
}

USkeletalMeshComponent* UTMMenuViewerMeshTransitionSubsystem::ReadMeshComponentProperty(UObject* Object, const FName PropertyName)
{
	if (!Object)
	{
		return nullptr;
	}

	const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Object->GetClass(), PropertyName);
	if (!Property)
	{
		return nullptr;
	}

	return Cast<USkeletalMeshComponent>(Property->GetObjectPropertyValue_InContainer(Object));
}

USkeletalMeshComponent* UTMMenuViewerMeshTransitionSubsystem::ResolveLeaderComponent(AActor* Actor, USkeletalMeshComponent* VestComponent)
{
	if (USkeletalMeshComponent* LeaderComponent = VestComponent ? Cast<USkeletalMeshComponent>(VestComponent->LeaderPoseComponent.Get()) : nullptr)
	{
		return LeaderComponent;
	}

	return ReadMeshComponentProperty(Actor, TEXT("Character"));
}

bool UTMMenuViewerMeshTransitionSubsystem::IsAutoCycleEnabled()
{
	return CVarMenuViewerMeshTransitionAutoCycle.GetValueOnGameThread() != 0;
}

bool UTMMenuViewerMeshTransitionSubsystem::IsBeatSyncEnabled()
{
	return CVarMenuViewerMeshTransitionBeatSync.GetValueOnGameThread() != 0;
}

bool UTMMenuViewerMeshTransitionSubsystem::ShouldUseDownbeat()
{
	return CVarMenuViewerMeshTransitionBeatSyncDownbeat.GetValueOnGameThread() != 0;
}

bool UTMMenuViewerMeshTransitionSubsystem::ShouldUsePhraseEnd()
{
	return CVarMenuViewerMeshTransitionBeatSyncPhraseEnd.GetValueOnGameThread() != 0;
}

bool UTMMenuViewerMeshTransitionSubsystem::ShouldUsePhraseStart()
{
	return CVarMenuViewerMeshTransitionBeatSyncPhraseStart.GetValueOnGameThread() != 0;
}

float UTMMenuViewerMeshTransitionSubsystem::GetAutoCycleInterval()
{
	return FMath::Max(1.f, CVarMenuViewerMeshTransitionAutoCycleInterval.GetValueOnGameThread());
}

float UTMMenuViewerMeshTransitionSubsystem::GetBeatSyncWindow()
{
	return FMath::Max(0.01f, CVarMenuViewerMeshTransitionBeatSyncWindow.GetValueOnGameThread());
}

float UTMMenuViewerMeshTransitionSubsystem::GetBeatSyncPulseThreshold()
{
	return FMath::Max(0.01f, CVarMenuViewerMeshTransitionBeatSyncPulseThreshold.GetValueOnGameThread());
}

int32 UTMMenuViewerMeshTransitionSubsystem::GetBeatSyncPhraseBars()
{
	return FMath::Max(1, CVarMenuViewerMeshTransitionBeatSyncPhraseBars.GetValueOnGameThread());
}

UTMMenuViewerMeshTransitionSubsystem::FBeatSyncSnapshot UTMMenuViewerMeshTransitionSubsystem::MakeBeatSyncSnapshot(
	const UTMAudioEnvelopeFollower* Follower)
{
	FBeatSyncSnapshot Snapshot;
	if (!Follower || Follower->HasAnyFlags(RF_ClassDefaultObject) || !Follower->bIsAnalyzing)
	{
		return Snapshot;
	}

	Snapshot.bHasActiveFollower = true;
	Snapshot.BeatIndex = Follower->GetBeatCounter();
	Snapshot.BeatsPerBar = FMath::Max(1, Follower->BeatsPerBar);
	Snapshot.LastBeatTimeSeconds = Follower->GetLastBeatTimeSeconds();
	Snapshot.LastBeatStrength = Follower->GetLastBeatStrength();
	Snapshot.NormalizedEnvelopeValue = Follower->NormalizedEnvelopeValue;
	return Snapshot;
}

bool UTMMenuViewerMeshTransitionSubsystem::IsBeatSyncCycleBeat(const FBeatSyncSnapshot& BeatSync)
{
	const int32 BeatsPerBar = FMath::Max(1, BeatSync.BeatsPerBar);
	if (ShouldUsePhraseStart())
	{
		const int32 BeatsPerPhrase = BeatsPerBar * GetBeatSyncPhraseBars();
		return BeatsPerPhrase > 0 && BeatSync.BeatIndex > 0 && ((BeatSync.BeatIndex - 1) % BeatsPerPhrase) == 0;
	}

	if (ShouldUsePhraseEnd())
	{
		const int32 BeatsPerPhrase = BeatsPerBar * GetBeatSyncPhraseBars();
		return BeatsPerPhrase > 0 && BeatSync.BeatIndex > 0 && (BeatSync.BeatIndex % BeatsPerPhrase) == 0;
	}

	return !ShouldUseDownbeat() || ((BeatSync.BeatIndex - 1) % BeatsPerBar) == 0;
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateLoadoutFOV(UWorld* World, const bool bLoadoutVisible)
{
	if (!bLoadoutVisible)
	{
		RestoreLoadoutFOV();
		return;
	}

	APlayerCameraManager* PlayerCameraManager = ResolvePlayerCameraManager(World);
	if (!PlayerCameraManager)
	{
		return;
	}

	if (bLoadoutFOVApplied && LoadoutFOVCameraManager.IsValid() && LoadoutFOVCameraManager.Get() != PlayerCameraManager)
	{
		RestoreLoadoutFOV();
	}

	const float TargetFOV = FMath::Clamp(CVarLoadoutFOVAngle.GetValueOnGameThread(), 5.0f, 170.0f);
	if (!bLoadoutFOVApplied)
	{
		SavedLoadoutPreviousFOV = PlayerCameraManager->GetFOVAngle();
		if (!FMath::IsFinite(SavedLoadoutPreviousFOV))
		{
			SavedLoadoutPreviousFOV = PlayerCameraManager->DefaultFOV;
		}

		LoadoutFOVCameraManager = PlayerCameraManager;
		bLoadoutFOVApplied = true;
		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Display,
			TEXT("[TMLoadoutFOV] Applied loadout FOV %.1f; saved previous FOV %.1f."),
			TargetFOV,
			SavedLoadoutPreviousFOV);
	}

	if (!FMath::IsNearlyEqual(PlayerCameraManager->GetFOVAngle(), TargetFOV, 0.05f))
	{
		PlayerCameraManager->SetFOV(TargetFOV);
	}
}

void UTMMenuViewerMeshTransitionSubsystem::RestoreLoadoutFOV()
{
	if (!bLoadoutFOVApplied)
	{
		return;
	}

	if (APlayerCameraManager* PlayerCameraManager = LoadoutFOVCameraManager.Get())
	{
		const bool bRestoreMenuFOV = bMenuFOVApplied && MenuFOVCameraManager.Get() == PlayerCameraManager;
		const float RestoreFOV = bRestoreMenuFOV
			? GetMenuFOVAngle()
			: FMath::Clamp(SavedLoadoutPreviousFOV, 5.0f, 170.0f);
		if (bRestoreMenuFOV)
		{
			PlayerCameraManager->DefaultFOV = RestoreFOV;
		}
		PlayerCameraManager->SetFOV(RestoreFOV);
		if (!bRestoreMenuFOV)
		{
			PlayerCameraManager->UnlockFOV();
		}
		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Display,
			TEXT("[TMLoadoutFOV] Restored %s FOV %.1f after leaving loadout."),
			bRestoreMenuFOV ? TEXT("menu") : TEXT("previous"),
			RestoreFOV);
	}

	LoadoutFOVCameraManager.Reset();
	bLoadoutFOVApplied = false;
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateLoadoutPostProcess(
	UWorld* World,
	const bool bPostProcessVisible,
	const bool bLoadoutVisible,
	const bool bAttachmentsVisible)
{
	if (!bPostProcessVisible)
	{
		RestoreLoadoutPostProcess();
		return;
	}

	UCameraComponent* CameraComponent = ResolveActiveCameraComponent(World);
	if (!CameraComponent)
	{
		RestoreLoadoutPostProcess();
		return;
	}

	if (bLoadoutPostProcessApplied
		&& LoadoutPostProcessCamera.IsValid()
		&& LoadoutPostProcessCamera.Get() != CameraComponent)
	{
		RestoreLoadoutPostProcess();
	}

	if (!bLoadoutPostProcessApplied)
	{
		SavedLoadoutPostProcessSettings = CameraComponent->PostProcessSettings;
		SavedLoadoutPostProcessBlendWeight = CameraComponent->PostProcessBlendWeight;
		LoadoutPostProcessCamera = CameraComponent;
		bLoadoutPostProcessApplied = true;
		bLoadoutPostProcessLastLoadoutMode = bLoadoutVisible;
		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Display,
			TEXT("[TMMenuPP] Applied cinematic %s post process on %s. SavedBlendWeight=%.2f"),
			bLoadoutVisible ? TEXT("loadout") : TEXT("main menu"),
			*GetNameSafe(CameraComponent),
			SavedLoadoutPostProcessBlendWeight);
	}
	else if (bLoadoutPostProcessLastLoadoutMode != bLoadoutVisible)
	{
		bLoadoutPostProcessLastLoadoutMode = bLoadoutVisible;
		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Display,
			TEXT("[TMMenuPP] Switched cinematic post process to %s profile on %s."),
			bLoadoutVisible ? TEXT("loadout") : TEXT("main menu"),
			*GetNameSafe(CameraComponent));
	}

	ApplyLoadoutPostProcess(World, CameraComponent, bLoadoutVisible, bAttachmentsVisible);
}

void UTMMenuViewerMeshTransitionSubsystem::ApplyLoadoutPostProcess(
	UWorld* World,
	UCameraComponent* CameraComponent,
	const bool bLoadoutVisible,
	const bool bAttachmentsVisible)
{
	if (!CameraComponent)
	{
		return;
	}

	FPostProcessSettings Settings = SavedLoadoutPostProcessSettings;
	const FLoadoutPostProcessFocus Focus = ResolveLoadoutPostProcessFocus(World, CameraComponent);
	const float AttachmentsBrightness = bLoadoutVisible
		? FMath::Clamp(CVarAttachmentsPreviewBrightness.GetValueOnGameThread(), 0.0f, 0.5f)
			* FMath::Clamp(AttachmentsPreviewBrightnessCurrentAlpha, 0.0f, 1.0f)
		: 0.0f;

	if (bLoadoutVisible)
	{
		const float GlowAlpha = FMath::Clamp(LoadoutBackGlowCurrentAlpha, 0.0f, 1.0f);
		Settings.bOverride_DepthOfFieldFstop = true;
		Settings.DepthOfFieldFstop = FMath::Lerp(2.6f, 8.0f, GlowAlpha);
		Settings.bOverride_DepthOfFieldMinFstop = true;
		Settings.DepthOfFieldMinFstop = FMath::Lerp(1.8f, 8.0f, GlowAlpha);
		Settings.bOverride_DepthOfFieldFocalDistance = true;
		Settings.DepthOfFieldFocalDistance = Focus.FocalDistance;
		Settings.bOverride_DepthOfFieldFocalRegion = true;
		Settings.DepthOfFieldFocalRegion = FMath::Lerp(FMath::Max(Focus.FocalRegion, 210.0f), 1000.0f, GlowAlpha);
		Settings.bOverride_DepthOfFieldNearTransitionRegion = true;
		Settings.DepthOfFieldNearTransitionRegion = FMath::Lerp(130.0f, 1000.0f, GlowAlpha);
		Settings.bOverride_DepthOfFieldFarTransitionRegion = true;
		Settings.DepthOfFieldFarTransitionRegion = FMath::Lerp(190.0f, 1000.0f, GlowAlpha);
		Settings.bOverride_DepthOfFieldScale = true;
		Settings.DepthOfFieldScale = FMath::Lerp(0.55f, 0.0f, GlowAlpha);
		Settings.bOverride_DepthOfFieldNearBlurSize = true;
		Settings.DepthOfFieldNearBlurSize = FMath::Lerp(1.5f, 0.0f, GlowAlpha);
		Settings.bOverride_DepthOfFieldFarBlurSize = true;
		Settings.DepthOfFieldFarBlurSize = FMath::Lerp(6.0f, 0.0f, GlowAlpha);
		Settings.bOverride_DepthOfFieldPetzvalBokeh = true;
		Settings.DepthOfFieldPetzvalBokeh = FMath::Lerp(0.28f, 0.0f, GlowAlpha);
		Settings.bOverride_DepthOfFieldPetzvalBokehFalloff = true;
		Settings.DepthOfFieldPetzvalBokehFalloff = FMath::Lerp(2.8f, 0.0f, GlowAlpha);
		Settings.bOverride_DepthOfFieldPetzvalExclusionBoxExtents = true;
		Settings.DepthOfFieldPetzvalExclusionBoxExtents = FVector2f(
			FMath::Lerp(0.72f, 1.0f, GlowAlpha),
			FMath::Lerp(0.52f, 1.0f, GlowAlpha));
		Settings.bOverride_DepthOfFieldVignetteSize = true;
		Settings.DepthOfFieldVignetteSize = FMath::Lerp(94.0f, 100.0f, GlowAlpha);

		Settings.bOverride_BloomIntensity = true;
		Settings.BloomIntensity = FMath::Lerp(0.82f, 0.35f, GlowAlpha);
		Settings.bOverride_BloomGaussianIntensity = true;
		Settings.BloomGaussianIntensity = FMath::Lerp(0.55f, 0.35f, GlowAlpha);
		Settings.bOverride_BloomThreshold = true;
		Settings.BloomThreshold = FMath::Lerp(1.25f, 0.4f, GlowAlpha);
		Settings.bOverride_BloomSizeScale = true;
		Settings.BloomSizeScale = FMath::Lerp(1.3f, 1.6f, GlowAlpha);
		Settings.bOverride_Bloom1Tint = true;
		Settings.Bloom1Tint = LerpLinearColor(
			FLinearColor(0.52f, 0.52f, 0.52f, 1.0f),
			FLinearColor::White,
			GlowAlpha);
		Settings.bOverride_Bloom3Tint = true;
		Settings.Bloom3Tint = LerpLinearColor(
			FLinearColor(0.36f, 0.36f, 0.36f, 1.0f),
			FLinearColor::White,
			GlowAlpha);
		Settings.bOverride_Bloom5Tint = true;
		Settings.Bloom5Tint = LerpLinearColor(
			FLinearColor(0.24f, 0.24f, 0.24f, 1.0f),
			FLinearColor::White,
			GlowAlpha);

		ApplyFixedMenuExposure(Settings, FMath::Lerp(-1.18f, -0.55f, GlowAlpha) + AttachmentsBrightness);
		Settings.bOverride_VignetteIntensity = true;
		Settings.VignetteIntensity = FMath::Clamp(
			FMath::Lerp(0.74f, 0.58f, GlowAlpha) - AttachmentsBrightness * 0.35f,
			0.0f,
			1.0f);
		Settings.bOverride_SceneFringeIntensity = true;
		Settings.SceneFringeIntensity = FMath::Lerp(0.045f, 0.0f, GlowAlpha);
		Settings.bOverride_ChromaticAberrationStartOffset = true;
		Settings.ChromaticAberrationStartOffset = FMath::Lerp(0.62f, 0.0f, GlowAlpha);
		Settings.bOverride_FilmGrainIntensity = true;
		Settings.FilmGrainIntensity = FMath::Lerp(0.075f, 0.0f, GlowAlpha);

		Settings.bOverride_SceneColorTint = true;
		Settings.SceneColorTint = LerpLinearColor(
			FLinearColor(0.86f, 0.86f, 0.86f, 1.0f),
			FLinearColor(0.9f, 0.9f, 0.9f, 1.0f),
			GlowAlpha);
		Settings.bOverride_ColorSaturation = true;
		Settings.ColorSaturation = LerpVector4(
			FVector4(0.62f, 0.62f, 0.62f, 1.0f),
			FVector4(0.82f, 0.82f, 0.82f, 1.0f),
			GlowAlpha);
		Settings.bOverride_ColorContrast = true;
		Settings.ColorContrast = LerpVector4(
			FVector4(1.12f, 1.12f, 1.12f, 1.0f),
			FVector4(1.0f, 1.0f, 1.0f, 1.0f),
			GlowAlpha);
		Settings.bOverride_ColorGamma = true;
		Settings.ColorGamma = LerpVector4(
			FVector4(0.94f, 0.94f, 0.94f, 1.0f),
			FVector4(1.0f, 1.0f, 1.0f, 1.0f),
			GlowAlpha);
		Settings.bOverride_ColorGain = true;
		Settings.ColorGain = LerpVector4(
			FVector4(0.68f, 0.68f, 0.68f, 1.0f),
			FVector4(
				0.78f + AttachmentsBrightness * 0.35f,
				0.78f + AttachmentsBrightness * 0.35f,
				0.78f + AttachmentsBrightness * 0.35f,
				1.0f),
			GlowAlpha);
		Settings.bOverride_ColorOffset = true;
		Settings.ColorOffset = LerpVector4(
			FVector4(-0.04f, -0.04f, -0.04f, 0.0f),
			FVector4(0.0f, 0.0f, 0.0f, 0.0f),
			GlowAlpha);
	}
	else
	{
		Settings.bOverride_DepthOfFieldFstop = true;
		Settings.DepthOfFieldFstop = 1.05f;
		Settings.bOverride_DepthOfFieldMinFstop = true;
		Settings.DepthOfFieldMinFstop = 1.0f;
		Settings.bOverride_DepthOfFieldFocalDistance = true;
		Settings.DepthOfFieldFocalDistance = Focus.FocalDistance;
		Settings.bOverride_DepthOfFieldFocalRegion = true;
		Settings.DepthOfFieldFocalRegion = Focus.FocalRegion;
		Settings.bOverride_DepthOfFieldNearTransitionRegion = true;
		Settings.DepthOfFieldNearTransitionRegion = 45.0f;
		Settings.bOverride_DepthOfFieldFarTransitionRegion = true;
		Settings.DepthOfFieldFarTransitionRegion = 70.0f;
		Settings.bOverride_DepthOfFieldScale = true;
		Settings.DepthOfFieldScale = 1.15f;
		Settings.bOverride_DepthOfFieldNearBlurSize = true;
		Settings.DepthOfFieldNearBlurSize = 5.0f;
		Settings.bOverride_DepthOfFieldFarBlurSize = true;
		Settings.DepthOfFieldFarBlurSize = 10.0f;
		Settings.bOverride_DepthOfFieldPetzvalBokeh = true;
		Settings.DepthOfFieldPetzvalBokeh = FMath::Clamp(
			CVarLoadoutPostProcessPetzval.GetValueOnGameThread(),
			-10.0f,
			10.0f);
		Settings.bOverride_DepthOfFieldPetzvalBokehFalloff = true;
		Settings.DepthOfFieldPetzvalBokehFalloff = 4.0f;
		Settings.bOverride_DepthOfFieldPetzvalExclusionBoxExtents = true;
		Settings.DepthOfFieldPetzvalExclusionBoxExtents = FVector2f(0.62f, 0.42f);
		Settings.bOverride_DepthOfFieldVignetteSize = true;
		Settings.DepthOfFieldVignetteSize = FMath::Clamp(
			CVarLoadoutPostProcessEdgeBlur.GetValueOnGameThread(),
			5.0f,
			100.0f);

		ApplyFixedMenuExposure(Settings, -0.985f);
		Settings.bOverride_VignetteIntensity = true;
		Settings.VignetteIntensity = FMath::Clamp(
			CVarLoadoutPostProcessVignette.GetValueOnGameThread(),
			0.0f,
			1.0f);
		Settings.bOverride_SceneFringeIntensity = true;
		Settings.SceneFringeIntensity = 0.25f;
		Settings.bOverride_FilmGrainIntensity = true;
		Settings.FilmGrainIntensity = 0.04f;
	}

	CameraComponent->PostProcessSettings = Settings;
	CameraComponent->SetPostProcessBlendWeight(FMath::Clamp(
		CVarLoadoutPostProcessBlendWeight.GetValueOnGameThread(),
		0.0f,
		1.0f));
}

void UTMMenuViewerMeshTransitionSubsystem::RestoreLoadoutPostProcess()
{
	if (!bLoadoutPostProcessApplied)
	{
		return;
	}

	if (UCameraComponent* CameraComponent = LoadoutPostProcessCamera.Get())
	{
		CameraComponent->PostProcessSettings = SavedLoadoutPostProcessSettings;
		CameraComponent->SetPostProcessBlendWeight(SavedLoadoutPostProcessBlendWeight);
		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Display,
			TEXT("[TMMenuPP] Restored original post process on %s."),
			*GetNameSafe(CameraComponent));
	}

	LoadoutPostProcessCamera.Reset();
	SavedLoadoutPostProcessSettings = FPostProcessSettings();
	SavedLoadoutPostProcessBlendWeight = 0.0f;
	bLoadoutPostProcessApplied = false;
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateAttachmentsPreviewBrightnessTiming(
	const bool bAttachmentsVisible,
	const bool bLoadoutVisible,
	const float DeltaTime)
{
	if (!bLoadoutVisible)
	{
		bAttachmentsPreviewBrightnessActive = false;
		bAttachmentsPreviewBrightnessTargetVisible = false;
		AttachmentsPreviewBrightnessElapsedSeconds = 0.0f;
		AttachmentsPreviewBrightnessTransitionStartAlpha = 0.0f;
		AttachmentsPreviewBrightnessCurrentAlpha = 0.0f;
		return;
	}

	if (!bAttachmentsPreviewBrightnessActive)
	{
		if (!bAttachmentsVisible)
		{
			AttachmentsPreviewBrightnessElapsedSeconds = 0.0f;
			AttachmentsPreviewBrightnessTransitionStartAlpha = 0.0f;
			AttachmentsPreviewBrightnessCurrentAlpha = 0.0f;
			return;
		}

		bAttachmentsPreviewBrightnessActive = true;
		bAttachmentsPreviewBrightnessTargetVisible = true;
		AttachmentsPreviewBrightnessElapsedSeconds = 0.0f;
		AttachmentsPreviewBrightnessTransitionStartAlpha = AttachmentsPreviewBrightnessCurrentAlpha;
		AttachmentsPreviewBrightnessCurrentAlpha = 0.0f;
		return;
	}

	if (bAttachmentsPreviewBrightnessTargetVisible != bAttachmentsVisible)
	{
		bAttachmentsPreviewBrightnessTargetVisible = bAttachmentsVisible;
		AttachmentsPreviewBrightnessElapsedSeconds = 0.0f;
		AttachmentsPreviewBrightnessTransitionStartAlpha = AttachmentsPreviewBrightnessCurrentAlpha;
		return;
	}

	AttachmentsPreviewBrightnessElapsedSeconds += FMath::Max(0.0f, DeltaTime);
	const float Delay = FMath::Max(0.0f, CVarLoadoutBrightBackGlowDelay.GetValueOnGameThread());
	const float FadeDuration = FMath::Max(0.01f, CVarLoadoutBrightBackGlowFadeDuration.GetValueOnGameThread());
	const float FadeAlpha = FMath::Clamp(
		(AttachmentsPreviewBrightnessElapsedSeconds - Delay) / FadeDuration,
		0.0f,
		1.0f);
	const float SmoothAlpha = FadeAlpha * FadeAlpha * (3.0f - 2.0f * FadeAlpha);
	const float TargetAlpha = bAttachmentsPreviewBrightnessTargetVisible ? 1.0f : 0.0f;
	AttachmentsPreviewBrightnessCurrentAlpha = FMath::Lerp(
		AttachmentsPreviewBrightnessTransitionStartAlpha,
		TargetAlpha,
		SmoothAlpha);

	if (!bAttachmentsPreviewBrightnessTargetVisible && FadeAlpha >= 1.0f)
	{
		bAttachmentsPreviewBrightnessActive = false;
		AttachmentsPreviewBrightnessElapsedSeconds = 0.0f;
		AttachmentsPreviewBrightnessTransitionStartAlpha = 0.0f;
		AttachmentsPreviewBrightnessCurrentAlpha = 0.0f;
	}
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateLoadoutBackGlowTiming(
	const bool bLoadoutVisible,
	const float DeltaTime)
{
	if (!bLoadoutVisible)
	{
		if (bLoadoutBackGlowActive)
		{
			bMainMenuBackGlowForceComplete = true;
		}

		bLoadoutBackGlowActive = false;
		bLoadoutBackGlowTargetVisible = false;
		LoadoutBackGlowElapsedSeconds = 0.0f;
		LoadoutBackGlowCurrentAlpha = 0.0f;
		return;
	}

	if (!bLoadoutBackGlowActive)
	{
		bLoadoutBackGlowActive = true;
		bLoadoutBackGlowTargetVisible = true;
		LoadoutBackGlowElapsedSeconds = 0.0f;
		LoadoutBackGlowCurrentAlpha = 0.0f;
		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Display,
			TEXT("[TMLoadoutBackGlow] Holding loadout glow for %.2fs, then fading to bright profile over %.2fs."),
			FMath::Max(0.0f, CVarLoadoutBrightBackGlowDelay.GetValueOnGameThread()),
			FMath::Max(0.01f, CVarLoadoutBrightBackGlowFadeDuration.GetValueOnGameThread()));
		return;
	}

	LoadoutBackGlowElapsedSeconds += FMath::Max(0.0f, DeltaTime);
	const float Delay = FMath::Max(0.0f, CVarLoadoutBrightBackGlowDelay.GetValueOnGameThread());
	const float FadeDuration = FMath::Max(0.01f, CVarLoadoutBrightBackGlowFadeDuration.GetValueOnGameThread());
	const float FadeAlpha = FMath::Clamp((LoadoutBackGlowElapsedSeconds - Delay) / FadeDuration, 0.0f, 1.0f);
	LoadoutBackGlowCurrentAlpha = FadeAlpha * FadeAlpha * (3.0f - 2.0f * FadeAlpha);
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateLoadoutBackGlow(
	UWorld* World,
	const bool bLoadoutVisible,
	const float DeltaTime)
{
	(void)DeltaTime;

	if (!World || CVarLoadoutBackGlowDucking.GetValueOnGameThread() == 0)
	{
		RestoreLoadoutBackGlow();
		return;
	}

	if (!bLoadoutVisible)
	{
		RestoreLoadoutBackGlow();
		return;
	}

	for (auto It = LoadoutBackGlowLightStates.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	for (auto It = LoadoutBackGlowVisualStates.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	const float GlowAlpha = FMath::Clamp(LoadoutBackGlowCurrentAlpha, 0.0f, 1.0f);
	const FVector TargetGlowLocation(-740.0f, 65.0f, -35.0f);
	const FRotator TargetGlowRotation(0.0f, -90.0f, 0.0f);
	const FVector TargetGlowScale(FMath::Max(0.0f, CVarLoadoutBrightBackGlowVisualScale.GetValueOnGameThread()));
	const float TargetLightIntensity = FMath::Max(0.0f, CVarLoadoutBrightBackGlowLightIntensity.GetValueOnGameThread());
	const float TargetAttenuationRadius =
		FMath::Max(0.0f, CVarLoadoutBrightBackGlowAttenuationRadius.GetValueOnGameThread());
	const float TargetSourceWidth = FMath::Max(0.0f, CVarLoadoutBrightBackGlowSourceWidth.GetValueOnGameThread());
	const float TargetSourceHeight = FMath::Max(0.0f, CVarLoadoutBrightBackGlowSourceHeight.GetValueOnGameThread());

	int32 AppliedLights = 0;
	int32 AppliedVisuals = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		TInlineComponentArray<ULightComponent*> LightComponents(Actor);
		for (ULightComponent* LightComponent : LightComponents)
		{
			if (!IsLoadoutBackGlowLight(LightComponent))
			{
				continue;
			}

			if (!LoadoutBackGlowLightStates.Contains(LightComponent))
			{
				FBackGlowLightState LightState;
				LightState.OriginalIntensity = LightComponent->Intensity;
				LightState.OriginalColor = LightComponent->GetLightColor();
				if (const ULocalLightComponent* LocalLightComponent = Cast<ULocalLightComponent>(LightComponent))
				{
					LightState.OriginalAttenuationRadius = LocalLightComponent->AttenuationRadius;
					LightState.bHasAttenuationRadius = true;
				}
				if (const URectLightComponent* RectLightComponent = Cast<URectLightComponent>(LightComponent))
				{
					LightState.OriginalSourceWidth = RectLightComponent->SourceWidth;
					LightState.OriginalSourceHeight = RectLightComponent->SourceHeight;
					LightState.bHasRectSourceSize = true;
				}
				LoadoutBackGlowLightStates.Add(LightComponent, LightState);
			}

			const FBackGlowLightState LightState = LoadoutBackGlowLightStates.FindRef(LightComponent);
			LightComponent->SetLightColor(LerpLinearColor(LightState.OriginalColor, FLinearColor::White, GlowAlpha));
			LightComponent->SetIntensity(FMath::Lerp(
				LightState.OriginalIntensity,
				TargetLightIntensity,
				GlowAlpha));
			if (ULocalLightComponent* LocalLightComponent = Cast<ULocalLightComponent>(LightComponent);
				LocalLightComponent && LightState.bHasAttenuationRadius)
			{
				LocalLightComponent->SetAttenuationRadius(FMath::Lerp(
					LightState.OriginalAttenuationRadius,
					TargetAttenuationRadius,
					GlowAlpha));
			}
			if (URectLightComponent* RectLightComponent = Cast<URectLightComponent>(LightComponent);
				RectLightComponent && LightState.bHasRectSourceSize)
			{
				RectLightComponent->SetSourceWidth(FMath::Lerp(
					LightState.OriginalSourceWidth,
					TargetSourceWidth,
					GlowAlpha));
				RectLightComponent->SetSourceHeight(FMath::Lerp(
					LightState.OriginalSourceHeight,
					TargetSourceHeight,
					GlowAlpha));
			}
			++AppliedLights;
		}

		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(Actor);
		for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			if (!IsLoadoutBackGlowVisual(PrimitiveComponent))
			{
				continue;
			}

			if (!LoadoutBackGlowVisualStates.Contains(PrimitiveComponent))
			{
				FBackGlowVisualState VisualState;
				VisualState.OriginalLocation = PrimitiveComponent->GetComponentLocation();
				VisualState.OriginalRotation = PrimitiveComponent->GetComponentRotation();
				VisualState.OriginalScale = PrimitiveComponent->GetComponentScale();
				VisualState.bVisible = PrimitiveComponent->IsVisible();
				VisualState.bHiddenInGame = PrimitiveComponent->bHiddenInGame;
				const int32 MaterialCount = PrimitiveComponent->GetNumMaterials();
				VisualState.OriginalMaterials.Reserve(MaterialCount);
				for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
				{
					VisualState.OriginalMaterials.Add(PrimitiveComponent->GetMaterial(MaterialIndex));
				}
				LoadoutBackGlowVisualStates.Add(PrimitiveComponent, VisualState);
			}

			const FBackGlowVisualState VisualState = LoadoutBackGlowVisualStates.FindRef(PrimitiveComponent);
			PrimitiveComponent->SetWorldLocation(FMath::Lerp(
				VisualState.OriginalLocation,
				TargetGlowLocation,
				GlowAlpha));
			PrimitiveComponent->SetWorldRotation(FQuat::Slerp(
				VisualState.OriginalRotation.Quaternion(),
				TargetGlowRotation.Quaternion(),
				GlowAlpha).Rotator());
			PrimitiveComponent->SetWorldScale3D(FMath::Lerp(
				VisualState.OriginalScale,
				TargetGlowScale,
				GlowAlpha));
			PrimitiveComponent->SetVisibility(true, true);
			PrimitiveComponent->SetHiddenInGame(false, true);

			if (UParticleSystemComponent* ParticleComponent = Cast<UParticleSystemComponent>(PrimitiveComponent))
			{
				ParticleComponent->SetFloatParameter(TEXT("Alpha"), GlowAlpha);
				ParticleComponent->SetFloatParameter(TEXT("Opacity"), GlowAlpha);
				ParticleComponent->SetFloatParameter(TEXT("Intensity"), GlowAlpha);
				ParticleComponent->SetFloatParameter(TEXT("Brightness"), GlowAlpha);
				ParticleComponent->SetColorParameter(TEXT("Color"), FLinearColor::White);
				ParticleComponent->SetColorParameter(TEXT("Tint"), FLinearColor::White);
			}

			++AppliedVisuals;
		}
	}

	if (!LoadoutBackGlowSpawnedVisual.IsValid())
	{
		UParticleSystem* GlowTemplate = LoadObject<UParticleSystem>(
			nullptr,
			TEXT("/Game/MP_System_V3/Game/Commons/Particles/P_Ambient_Glow.P_Ambient_Glow"));
		if (GlowTemplate)
		{
			LoadoutBackGlowSpawnedVisual = UGameplayStatics::SpawnEmitterAtLocation(
				World,
				GlowTemplate,
				TargetGlowLocation,
				TargetGlowRotation,
				FVector::OneVector,
				false,
				EPSCPoolMethod::None,
				false);
			if (UParticleSystemComponent* SpawnedVisual = LoadoutBackGlowSpawnedVisual.Get())
			{
				SpawnedVisual->ComponentTags.AddUnique(TEXT("TMSpawnedLoadoutBackGlowVisual"));
			}
		}
	}

	if (UParticleSystemComponent* SpawnedVisual = LoadoutBackGlowSpawnedVisual.Get())
	{
		const bool bVisualVisible = GlowAlpha > 0.001f;
		SpawnedVisual->SetWorldLocation(TargetGlowLocation);
		SpawnedVisual->SetWorldRotation(TargetGlowRotation);
		SpawnedVisual->SetWorldScale3D(TargetGlowScale * GlowAlpha);
		SpawnedVisual->SetVisibility(bVisualVisible, true);
		SpawnedVisual->SetHiddenInGame(!bVisualVisible, true);
		if (!SpawnedVisual->IsActive())
		{
			SpawnedVisual->ActivateSystem(true);
		}
		SpawnedVisual->SetFloatParameter(TEXT("Alpha"), GlowAlpha);
		SpawnedVisual->SetFloatParameter(TEXT("Opacity"), GlowAlpha);
		SpawnedVisual->SetFloatParameter(TEXT("Intensity"), GlowAlpha);
		SpawnedVisual->SetFloatParameter(TEXT("Brightness"), GlowAlpha);
		SpawnedVisual->SetColorParameter(TEXT("Color"), FLinearColor::White);
		SpawnedVisual->SetColorParameter(TEXT("Tint"), FLinearColor::White);
		++AppliedVisuals;
	}

	if (AppliedLights == 0 && AppliedVisuals == 0 && bLoadoutVisible)
	{
		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Verbose,
			TEXT("[TMLoadoutBackGlow] No rear RectLight glow components found for bright loadout profile."));
	}
}

void UTMMenuViewerMeshTransitionSubsystem::RestoreLoadoutBackGlow()
{
	if (UParticleSystemComponent* SpawnedVisual = LoadoutBackGlowSpawnedVisual.Get())
	{
		SpawnedVisual->DeactivateSystem();
		SpawnedVisual->DestroyComponent();
	}
	LoadoutBackGlowSpawnedVisual.Reset();

	for (const TPair<TWeakObjectPtr<ULightComponent>, FBackGlowLightState>& LightState : LoadoutBackGlowLightStates)
	{
		if (ULightComponent* LightComponent = LightState.Key.Get())
		{
			LightComponent->SetIntensity(LightState.Value.OriginalIntensity);
			LightComponent->SetLightColor(LightState.Value.OriginalColor);
			if (ULocalLightComponent* LocalLightComponent = Cast<ULocalLightComponent>(LightComponent);
				LocalLightComponent && LightState.Value.bHasAttenuationRadius)
			{
				LocalLightComponent->SetAttenuationRadius(LightState.Value.OriginalAttenuationRadius);
			}
			if (URectLightComponent* RectLightComponent = Cast<URectLightComponent>(LightComponent);
				RectLightComponent && LightState.Value.bHasRectSourceSize)
			{
				RectLightComponent->SetSourceWidth(LightState.Value.OriginalSourceWidth);
				RectLightComponent->SetSourceHeight(LightState.Value.OriginalSourceHeight);
			}
		}
	}

	for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, FBackGlowVisualState>& VisualState : LoadoutBackGlowVisualStates)
	{
		if (UPrimitiveComponent* PrimitiveComponent = VisualState.Key.Get())
		{
			for (int32 MaterialIndex = 0; MaterialIndex < VisualState.Value.OriginalMaterials.Num(); ++MaterialIndex)
			{
				if (UMaterialInterface* OriginalMaterial = VisualState.Value.OriginalMaterials[MaterialIndex].Get())
				{
					PrimitiveComponent->SetMaterial(MaterialIndex, OriginalMaterial);
				}
			}
			PrimitiveComponent->SetWorldLocation(VisualState.Value.OriginalLocation);
			PrimitiveComponent->SetWorldRotation(VisualState.Value.OriginalRotation);
			PrimitiveComponent->SetWorldScale3D(VisualState.Value.OriginalScale);
			PrimitiveComponent->SetVisibility(VisualState.Value.bVisible, true);
			PrimitiveComponent->SetHiddenInGame(VisualState.Value.bHiddenInGame, true);
			if (UParticleSystemComponent* ParticleComponent = Cast<UParticleSystemComponent>(PrimitiveComponent))
			{
				ParticleComponent->SetFloatParameter(TEXT("Alpha"), 1.0f);
				ParticleComponent->SetFloatParameter(TEXT("Opacity"), 1.0f);
				ParticleComponent->SetFloatParameter(TEXT("Intensity"), 1.0f);
				ParticleComponent->SetFloatParameter(TEXT("Brightness"), 1.0f);
			}
		}
	}

	LoadoutBackGlowLightStates.Empty();
	LoadoutBackGlowVisualStates.Empty();
	LoadoutBackGlowElapsedSeconds = 0.0f;
	LoadoutBackGlowCurrentAlpha = 0.0f;
	bLoadoutBackGlowTargetVisible = false;
	bLoadoutBackGlowActive = false;
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateMainMenuBackGlow(
	UWorld* World,
	const bool bMainMenuGlowVisible,
	const float DeltaTime)
{
	if (!World || CVarMainMenuBackGlow.GetValueOnGameThread() == 0 || !bMainMenuGlowVisible)
	{
		RestoreMainMenuBackGlow();
		return;
	}

	const float Delay = FMath::Max(0.0f, CVarMainMenuBackGlowDelay.GetValueOnGameThread());
	const float FadeDuration = FMath::Max(0.01f, CVarMainMenuBackGlowFadeDuration.GetValueOnGameThread());
	if (!bMainMenuBackGlowActive)
	{
		bMainMenuBackGlowActive = true;
		if (bMainMenuBackGlowForceComplete)
		{
			MainMenuBackGlowElapsedSeconds = Delay + FadeDuration;
			MainMenuBackGlowCurrentScale = FMath::Clamp(CVarMainMenuBackGlowScale.GetValueOnGameThread(), 0.0f, 2.0f);
			bMainMenuBackGlowForceComplete = false;
			UE_LOG(
				LogTMMenuViewerMeshTransition,
				Display,
				TEXT("[TMMainMenuBackGlow] Restored final main menu rear glow immediately after leaving loadout."));
		}
		else
		{
			MainMenuBackGlowElapsedSeconds = 0.0f;
			MainMenuBackGlowCurrentScale = 0.0f;
			UE_LOG(
				LogTMMenuViewerMeshTransition,
				Display,
				TEXT("[TMMainMenuBackGlow] Holding rear glow black for %.2fs before neon fade-in."),
				Delay);
		}
	}
	else
	{
		MainMenuBackGlowElapsedSeconds += FMath::Max(0.0f, DeltaTime);
		if (bMainMenuBackGlowForceComplete)
		{
			MainMenuBackGlowElapsedSeconds = Delay + FadeDuration;
			bMainMenuBackGlowForceComplete = false;
		}
	}

	const float FadeAlpha = FMath::Clamp((MainMenuBackGlowElapsedSeconds - Delay) / FadeDuration, 0.0f, 1.0f);
	const float SmoothAlpha = FadeAlpha * FadeAlpha * (3.0f - 2.0f * FadeAlpha);
	MainMenuBackGlowCurrentScale =
		FMath::Clamp(CVarMainMenuBackGlowScale.GetValueOnGameThread(), 0.0f, 2.0f) * SmoothAlpha;

	for (auto It = MainMenuBackGlowLightStates.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	for (auto It = MainMenuBackGlowVisualStates.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	int32 AppliedLights = 0;
	int32 AppliedVisuals = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		TInlineComponentArray<ULightComponent*> LightComponents(Actor);
		for (ULightComponent* LightComponent : LightComponents)
		{
			if (!IsLoadoutBackGlowLight(LightComponent))
			{
				continue;
			}

			if (!MainMenuBackGlowLightStates.Contains(LightComponent))
			{
				FBackGlowLightState LightState;
				LightState.OriginalIntensity = LightComponent->Intensity;
				LightState.OriginalColor = LightComponent->GetLightColor();
				MainMenuBackGlowLightStates.Add(LightComponent, LightState);
			}

			const FBackGlowLightState LightState = MainMenuBackGlowLightStates.FindRef(LightComponent);
			LightComponent->SetLightColor(FLinearColor::White);
			LightComponent->SetIntensity(LightState.OriginalIntensity * MainMenuBackGlowCurrentScale);
			++AppliedLights;
		}

		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(Actor);
		for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			if (!IsLoadoutBackGlowVisual(PrimitiveComponent))
			{
				continue;
			}

			if (!MainMenuBackGlowVisualStates.Contains(PrimitiveComponent))
			{
				FBackGlowVisualState VisualState;
				VisualState.OriginalLocation = PrimitiveComponent->GetComponentLocation();
				VisualState.OriginalRotation = PrimitiveComponent->GetComponentRotation();
				VisualState.OriginalScale = PrimitiveComponent->GetComponentScale();
				VisualState.bVisible = PrimitiveComponent->IsVisible();
				VisualState.bHiddenInGame = PrimitiveComponent->bHiddenInGame;
				const int32 MaterialCount = PrimitiveComponent->GetNumMaterials();
				VisualState.OriginalMaterials.Reserve(MaterialCount);
				for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
				{
					VisualState.OriginalMaterials.Add(PrimitiveComponent->GetMaterial(MaterialIndex));
				}
				MainMenuBackGlowVisualStates.Add(PrimitiveComponent, VisualState);
			}

			const FBackGlowVisualState VisualState = MainMenuBackGlowVisualStates.FindRef(PrimitiveComponent);
			const float VisualScale =
				FMath::Clamp(CVarMainMenuBackGlowVisualScale.GetValueOnGameThread(), 0.0f, 2.0f) * SmoothAlpha;
			PrimitiveComponent->SetVisibility(VisualState.bVisible, true);
			PrimitiveComponent->SetHiddenInGame(VisualState.bHiddenInGame, true);
			PrimitiveComponent->SetWorldScale3D(VisualState.OriginalScale * VisualScale);

			for (int32 MaterialIndex = 0; MaterialIndex < VisualState.OriginalMaterials.Num(); ++MaterialIndex)
			{
				UMaterialInterface* SourceMaterial = VisualState.OriginalMaterials[MaterialIndex].Get();
				UMaterialInstanceDynamic* DynamicMaterial =
					Cast<UMaterialInstanceDynamic>(PrimitiveComponent->GetMaterial(MaterialIndex));
				if (!DynamicMaterial)
				{
					DynamicMaterial = PrimitiveComponent->CreateDynamicMaterialInstance(MaterialIndex, SourceMaterial);
				}
				if (DynamicMaterial)
				{
					const float Emissive = FMath::Max(0.0f, CVarMainMenuBackGlowEmissive.GetValueOnGameThread()) * SmoothAlpha;
					DynamicMaterial->SetScalarParameterValue(TEXT("Emissive"), Emissive);
					DynamicMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor::White);
					DynamicMaterial->SetVectorParameterValue(TEXT("EmissiveColor"), FLinearColor::White);
					DynamicMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor::White);
				}
			}

			if (UParticleSystemComponent* ParticleComponent = Cast<UParticleSystemComponent>(PrimitiveComponent))
			{
				ParticleComponent->SetFloatParameter(TEXT("Alpha"), SmoothAlpha);
				ParticleComponent->SetFloatParameter(TEXT("Opacity"), SmoothAlpha);
				ParticleComponent->SetFloatParameter(TEXT("Intensity"), SmoothAlpha);
				ParticleComponent->SetFloatParameter(TEXT("Brightness"), SmoothAlpha);
				ParticleComponent->SetColorParameter(TEXT("Color"), FLinearColor::White);
				ParticleComponent->SetColorParameter(TEXT("Tint"), FLinearColor::White);
			}

			++AppliedVisuals;
		}
	}

	if (AppliedLights == 0 && AppliedVisuals == 0)
	{
		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Verbose,
			TEXT("[TMMainMenuBackGlow] No rear glow components found."));
	}
}

void UTMMenuViewerMeshTransitionSubsystem::RestoreMainMenuBackGlow()
{
	for (const TPair<TWeakObjectPtr<ULightComponent>, FBackGlowLightState>& LightState : MainMenuBackGlowLightStates)
	{
		if (ULightComponent* LightComponent = LightState.Key.Get())
		{
			LightComponent->SetIntensity(LightState.Value.OriginalIntensity);
			LightComponent->SetLightColor(LightState.Value.OriginalColor);
		}
	}

	for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, FBackGlowVisualState>& VisualState : MainMenuBackGlowVisualStates)
	{
		if (UPrimitiveComponent* PrimitiveComponent = VisualState.Key.Get())
		{
			for (int32 MaterialIndex = 0; MaterialIndex < VisualState.Value.OriginalMaterials.Num(); ++MaterialIndex)
			{
				if (UMaterialInterface* OriginalMaterial = VisualState.Value.OriginalMaterials[MaterialIndex].Get())
				{
					PrimitiveComponent->SetMaterial(MaterialIndex, OriginalMaterial);
				}
			}
			PrimitiveComponent->SetWorldLocation(VisualState.Value.OriginalLocation);
			PrimitiveComponent->SetWorldRotation(VisualState.Value.OriginalRotation);
			PrimitiveComponent->SetWorldScale3D(VisualState.Value.OriginalScale);
			PrimitiveComponent->SetVisibility(VisualState.Value.bVisible, true);
			PrimitiveComponent->SetHiddenInGame(VisualState.Value.bHiddenInGame, true);
			if (UParticleSystemComponent* ParticleComponent = Cast<UParticleSystemComponent>(PrimitiveComponent))
			{
				ParticleComponent->SetFloatParameter(TEXT("Alpha"), 1.0f);
				ParticleComponent->SetFloatParameter(TEXT("Opacity"), 1.0f);
				ParticleComponent->SetFloatParameter(TEXT("Intensity"), 1.0f);
				ParticleComponent->SetFloatParameter(TEXT("Brightness"), 1.0f);
			}
		}
	}

	MainMenuBackGlowLightStates.Empty();
	MainMenuBackGlowVisualStates.Empty();
	MainMenuBackGlowElapsedSeconds = 0.0f;
	MainMenuBackGlowCurrentScale = 0.0f;
	bMainMenuBackGlowActive = false;
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateMainMenuCameraDrift(
	UWorld* World,
	const bool bMenuDriftVisible,
	const bool bLoadoutMode,
	const bool bAttachmentsVisible,
	const float DeltaTime)
{
	if (!World || CVarMainMenuCameraDrift.GetValueOnGameThread() == 0 || !bMenuDriftVisible)
	{
		RestoreMainMenuCameraDrift();
		return;
	}

	UCameraComponent* CameraComponent = ResolveActiveCameraComponent(World);
	if (!CameraComponent)
	{
		RestoreMainMenuCameraDrift();
		return;
	}

	if (bMainMenuCameraDriftApplied && MainMenuCameraDriftCamera.Get() != CameraComponent)
	{
		RestoreMainMenuCameraDrift();
	}

	if (bMainMenuCameraDriftApplied && bMainMenuCameraDriftLoadoutMode != bLoadoutMode)
	{
		const FVector ExpectedLocation =
			MainMenuCameraDriftBaseRelativeLocation + MainMenuCameraDriftLastRelativeLocationOffset;
		const FRotator ExpectedRotation =
			MainMenuCameraDriftBaseRelativeRotation + MainMenuCameraDriftLastRelativeRotationOffset;
		if (CameraComponent->GetRelativeLocation().Equals(ExpectedLocation, 0.1f)
			&& CameraComponent->GetRelativeRotation().Equals(ExpectedRotation, 0.01f))
		{
			CameraComponent->SetRelativeLocationAndRotation(
				MainMenuCameraDriftBaseRelativeLocation,
				MainMenuCameraDriftBaseRelativeRotation);
		}

		MainMenuCameraDriftCamera.Reset();
		MainMenuCameraDriftBaseRelativeLocation = FVector::ZeroVector;
		MainMenuCameraDriftLastRelativeLocationOffset = FVector::ZeroVector;
		MainMenuCameraDriftBaseRelativeRotation = FRotator::ZeroRotator;
		MainMenuCameraDriftLastRelativeRotationOffset = FRotator::ZeroRotator;
		MainMenuCameraDriftElapsedSeconds = 0.0f;
		ResetAttachmentsCameraFocus();
		bMainMenuCameraDriftApplied = false;
		bMainMenuCameraDriftLoadoutMode = false;
	}

	if (!bMainMenuCameraDriftApplied)
	{
		MainMenuCameraDriftCamera = CameraComponent;
		MainMenuCameraDriftBaseRelativeLocation = CameraComponent->GetRelativeLocation();
		MainMenuCameraDriftBaseRelativeRotation = CameraComponent->GetRelativeRotation();
		MainMenuCameraDriftLastRelativeLocationOffset = FVector::ZeroVector;
		MainMenuCameraDriftLastRelativeRotationOffset = FRotator::ZeroRotator;
		MainMenuCameraDriftElapsedSeconds = 0.0f;
		bMainMenuCameraDriftApplied = true;
		bMainMenuCameraDriftLoadoutMode = bLoadoutMode;
		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Display,
			TEXT("[TMMenuCameraDrift] Captured %s base camera transform on %s."),
			bLoadoutMode ? TEXT("loadout") : TEXT("main menu"),
			*GetNameSafe(CameraComponent));
	}

	UpdateAttachmentsCameraFocus(World, bLoadoutMode && bAttachmentsVisible, DeltaTime, CameraComponent);

	const float Period = FMath::Max(4.0f, CVarMainMenuCameraDriftPeriod.GetValueOnGameThread());
	MainMenuCameraDriftElapsedSeconds = FMath::Fmod(
		MainMenuCameraDriftElapsedSeconds + FMath::Max(0.0f, DeltaTime),
		Period);

	const float Angle = (MainMenuCameraDriftElapsedSeconds / Period) * UE_TWO_PI;
	const float LocationAmplitude = FMath::Clamp(
		CVarMainMenuCameraDriftLocationAmplitude.GetValueOnGameThread(),
		0.0f,
		8.0f);
	const float RotationAmplitude = FMath::Clamp(
		CVarMainMenuCameraDriftRotationAmplitude.GetValueOnGameThread(),
		0.0f,
		0.65f);

	const FVector DriftLocationOffset(
		FMath::Sin(Angle) * LocationAmplitude * 0.35f,
		FMath::Sin(Angle * 2.0f) * LocationAmplitude,
		FMath::Sin(Angle * 3.0f) * LocationAmplitude * 0.28f);
	const FRotator DriftRotationOffset(
		FMath::Sin(Angle * 2.0f) * RotationAmplitude * 0.65f,
		FMath::Sin(Angle) * RotationAmplitude,
		FMath::Sin(Angle * 3.0f) * RotationAmplitude * 0.12f);
	const FVector RelativeLocationOffset = DriftLocationOffset + AttachmentsCameraFocusCurrentLocationOffset;
	const FRotator RelativeRotationOffset = DriftRotationOffset + AttachmentsCameraFocusCurrentRotationOffset;

	MainMenuCameraDriftLastRelativeLocationOffset = RelativeLocationOffset;
	MainMenuCameraDriftLastRelativeRotationOffset = RelativeRotationOffset;
	CameraComponent->SetRelativeLocationAndRotation(
		MainMenuCameraDriftBaseRelativeLocation + RelativeLocationOffset,
		MainMenuCameraDriftBaseRelativeRotation + RelativeRotationOffset);
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateAttachmentsCameraFocus(
	UWorld* World,
	const bool bAttachmentsVisible,
	const float DeltaTime,
	UCameraComponent* CameraComponent)
{
	if (!CameraComponent || CVarAttachmentsCameraFocus.GetValueOnGameThread() == 0)
	{
		ResetAttachmentsCameraFocus();
		return;
	}

	const EAttachmentCameraFocusGroup ObservedGroup = bAttachmentsVisible
		? ResolveActiveAttachmentCameraFocusGroup(World)
		: EAttachmentCameraFocusGroup::None;
	uint32 ObservedInstallSignature = 0;
	const bool bObservedInstalled = bAttachmentsVisible
		&& ObservedGroup != EAttachmentCameraFocusGroup::None
		&& ResolveAttachmentCameraFocusInstallSignature(World, ObservedGroup, ObservedInstallSignature);
	const EAttachmentCameraFocusGroup FocusGroup = bObservedInstalled
		? ObservedGroup
		: EAttachmentCameraFocusGroup::None;
	const bool bObservedStateChanged =
		ObservedGroup != AttachmentsCameraFocusObservedGroup
		|| bObservedInstalled != bAttachmentsCameraFocusObservedInstalled
		|| ObservedInstallSignature != AttachmentsCameraFocusObservedInstallSignature;
	const bool bReplacingInstalledAttachmentInSameGroup =
		ObservedGroup == AttachmentsCameraFocusObservedGroup
		&& ObservedGroup != EAttachmentCameraFocusGroup::None
		&& bObservedInstalled
		&& bAttachmentsCameraFocusObservedInstalled
		&& ObservedInstallSignature != AttachmentsCameraFocusObservedInstallSignature;

	if (bObservedStateChanged && bReplacingInstalledAttachmentInSameGroup)
	{
		AttachmentsCameraFocusObservedGroup = ObservedGroup;
		AttachmentsCameraFocusObservedInstallSignature = ObservedInstallSignature;
		bAttachmentsCameraFocusObservedInstalled = bObservedInstalled;

		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Display,
			TEXT("[TMAttachmentsCameraFocus] Active=%s replaced installed attachment in same group. Keeping current focus. Signature=%u"),
			*DescribeAttachmentCameraFocusGroup(ObservedGroup),
			ObservedInstallSignature);
	}
	else if (bObservedStateChanged)
	{
		const float TargetScreenX = FMath::Clamp(CVarAttachmentsCameraFocusTargetScreenX.GetValueOnGameThread(), 0.1f, 0.9f);
		const float TargetScreenY = FMath::Clamp(CVarAttachmentsCameraFocusTargetScreenY.GetValueOnGameThread(), 0.1f, 0.9f);
		const float DepthScale = FocusGroup != EAttachmentCameraFocusGroup::None
			? FMath::Clamp(CVarAttachmentsCameraFocusCloseDepthScale.GetValueOnGameThread(), 0.55f, 1.0f)
			: 1.0f;
		float AdditionalPitch = 0.0f;
		float AdditionalYaw = 0.0f;

		if (FocusGroup != EAttachmentCameraFocusGroup::None)
		{
			const float MaxYaw = FMath::Clamp(CVarAttachmentsCameraFocusTurnYawMax.GetValueOnGameThread(), 0.0f, 30.0f);
			AdditionalYaw = ResolveAttachmentCameraFocusTurnYaw(
				World,
				CameraComponent,
				FocusGroup,
				MaxYaw,
				AttachmentsCameraFocusCurrentRotationOffset.Yaw);
		}

		const FAttachmentCameraFocusPose CenterPose = ResolveAttachmentCameraFocusPose(
			World,
			CameraComponent,
			FocusGroup,
			AdditionalPitch,
			AdditionalYaw,
			TargetScreenX,
			TargetScreenY,
			DepthScale);
		AttachmentsCameraFocusObservedGroup = ObservedGroup;
		AttachmentsCameraFocusObservedInstallSignature = ObservedInstallSignature;
		bAttachmentsCameraFocusObservedInstalled = bObservedInstalled;
		AttachmentsCameraFocusTargetGroup = FocusGroup;
		AttachmentsCameraFocusStartLocationOffset = AttachmentsCameraFocusCurrentLocationOffset;
		AttachmentsCameraFocusStartRotationOffset = AttachmentsCameraFocusCurrentRotationOffset;
		AttachmentsCameraFocusTargetLocationOffset = CenterPose.LocationOffset;
		AttachmentsCameraFocusTargetRotationOffset = CenterPose.RotationOffset;
		AttachmentsCameraFocusDelayElapsedSeconds = 0.0f;
		AttachmentsCameraFocusBlendElapsedSeconds = 0.0f;
		bAttachmentsCameraFocusWaitingForDelay = FocusGroup != EAttachmentCameraFocusGroup::None
			&& CVarAttachmentsCameraFocusDelay.GetValueOnGameThread() > 0.0f;
		bAttachmentsCameraFocusTransitionActive = true;

		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Display,
			TEXT("[TMAttachmentsCameraFocus] Active=%s Installed=%s Signature=%u Target=%s Delay=%.2f Duration=%.2f AdditionalPitch=%.1f AdditionalYaw=%.1f TargetLoc=%s TargetRot=%s"),
			*DescribeAttachmentCameraFocusGroup(ObservedGroup),
			bObservedInstalled ? TEXT("true") : TEXT("false"),
			ObservedInstallSignature,
			*DescribeAttachmentCameraFocusGroup(FocusGroup),
			bAttachmentsCameraFocusWaitingForDelay ? FMath::Max(0.0f, CVarAttachmentsCameraFocusDelay.GetValueOnGameThread()) : 0.0f,
			FMath::Max(0.01f, CVarAttachmentsCameraFocusDuration.GetValueOnGameThread()),
			AdditionalPitch,
			AdditionalYaw,
			*AttachmentsCameraFocusTargetLocationOffset.ToString(),
			*AttachmentsCameraFocusTargetRotationOffset.ToString());
	}

	if (!bAttachmentsCameraFocusTransitionActive)
	{
		return;
	}

	if (bAttachmentsCameraFocusWaitingForDelay)
	{
		AttachmentsCameraFocusDelayElapsedSeconds += FMath::Max(0.0f, DeltaTime);
		if (AttachmentsCameraFocusDelayElapsedSeconds < FMath::Max(0.0f, CVarAttachmentsCameraFocusDelay.GetValueOnGameThread()))
		{
			return;
		}

		bAttachmentsCameraFocusWaitingForDelay = false;
		AttachmentsCameraFocusBlendElapsedSeconds = 0.0f;
	}

	AttachmentsCameraFocusBlendElapsedSeconds += FMath::Max(0.0f, DeltaTime);
	const float Duration = FMath::Max(0.01f, CVarAttachmentsCameraFocusDuration.GetValueOnGameThread());
	const float Alpha = SmoothStep01(AttachmentsCameraFocusBlendElapsedSeconds / Duration);
	AttachmentsCameraFocusCurrentLocationOffset = FMath::Lerp(
		AttachmentsCameraFocusStartLocationOffset,
		AttachmentsCameraFocusTargetLocationOffset,
		Alpha);
	AttachmentsCameraFocusCurrentRotationOffset = FMath::Lerp(
		AttachmentsCameraFocusStartRotationOffset,
		AttachmentsCameraFocusTargetRotationOffset,
		Alpha);

	if (Alpha >= 1.0f)
	{
		AttachmentsCameraFocusCurrentLocationOffset = AttachmentsCameraFocusTargetLocationOffset;
		AttachmentsCameraFocusCurrentRotationOffset = AttachmentsCameraFocusTargetRotationOffset;

		bAttachmentsCameraFocusTransitionActive = false;
	}
}

void UTMMenuViewerMeshTransitionSubsystem::ResetAttachmentsCameraFocus()
{
	AttachmentsCameraFocusCurrentLocationOffset = FVector::ZeroVector;
	AttachmentsCameraFocusStartLocationOffset = FVector::ZeroVector;
	AttachmentsCameraFocusTargetLocationOffset = FVector::ZeroVector;
	AttachmentsCameraFocusCurrentRotationOffset = FRotator::ZeroRotator;
	AttachmentsCameraFocusStartRotationOffset = FRotator::ZeroRotator;
	AttachmentsCameraFocusTargetRotationOffset = FRotator::ZeroRotator;
	AttachmentsCameraFocusDelayElapsedSeconds = 0.0f;
	AttachmentsCameraFocusBlendElapsedSeconds = 0.0f;
	AttachmentsCameraFocusObservedGroup = EAttachmentCameraFocusGroup::None;
	AttachmentsCameraFocusTargetGroup = EAttachmentCameraFocusGroup::None;
	AttachmentsCameraFocusObservedInstallSignature = 0;
	bAttachmentsCameraFocusObservedInstalled = false;
	bAttachmentsCameraFocusWaitingForDelay = false;
	bAttachmentsCameraFocusTransitionActive = false;
}

UTMMenuViewerMeshTransitionSubsystem::EAttachmentCameraFocusGroup
UTMMenuViewerMeshTransitionSubsystem::ResolveActiveAttachmentCameraFocusGroup(UWorld* World) const
{
	if (!World)
	{
		return EAttachmentCameraFocusGroup::None;
	}

	static const FName AttachmentListNames[] =
	{
		TEXT("AttachmentList"),
		TEXT("AttachmentList_1"),
		TEXT("AttachmentsList")
	};

	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->GetWorld() != World)
		{
			continue;
		}

		const UClass* WidgetClass = Widget->GetClass();
		if (!WidgetClass || !WidgetClass->GetPathName().Contains(TEXT("W_Attachments")))
		{
			continue;
		}

		if (!IsVisibleWidgetInstance(Widget))
		{
			continue;
		}

		for (const FName AttachmentListName : AttachmentListNames)
		{
			UWidget* AttachmentList = Widget->GetWidgetFromName(AttachmentListName);
			if (!AttachmentList)
			{
				continue;
			}

			EAttachmentCameraFocusGroup Result = EAttachmentCameraFocusGroup::None;
			VisitWidgetTree(AttachmentList, [&Result](UWidget* ChildWidget)
			{
				const UTextBlock* TextBlock = Cast<UTextBlock>(ChildWidget);
				if (!TextBlock)
				{
					return;
				}

				const EAttachmentCameraFocusGroup Candidate =
					InferAttachmentCameraFocusGroupFromText(TextBlock->GetText().ToString());
				if (Candidate != EAttachmentCameraFocusGroup::None)
				{
					Result = Candidate;
				}
			});

			if (Result != EAttachmentCameraFocusGroup::None)
			{
				return Result;
			}
		}
	}

	return EAttachmentCameraFocusGroup::None;
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateAttachmentSelectionHighlight(UWorld* World, const bool bAttachmentsVisible)
{
	if (!World || !bAttachmentsVisible)
	{
		RestoreAttachmentSelectionHighlight();
		return;
	}

	const EAttachmentCameraFocusGroup ActiveGroup = ResolveActiveAttachmentCameraFocusGroup(World);
	if (ActiveGroup == EAttachmentCameraFocusGroup::None)
	{
		RestoreAttachmentSelectionHighlight();
		return;
	}

	TSet<FString> HighlightTokens;
	if (!ResolveInstalledAttachmentHighlightTokens(World, ActiveGroup, HighlightTokens) || HighlightTokens.IsEmpty())
	{
		HighlightTokens.Add(NormalizeAttachmentFocusToken(TEXT("Empty")));
	}

	static const FName AttachmentListNames[] =
	{
		TEXT("AttachmentList"),
		TEXT("AttachmentList_1"),
		TEXT("AttachmentsList")
	};

	TSet<TWeakObjectPtr<UTextBlock>> SeenLabels;
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->GetWorld() != World)
		{
			continue;
		}

		const UClass* WidgetClass = Widget->GetClass();
		if (!WidgetClass || !WidgetClass->GetPathName().Contains(TEXT("W_Attachments")))
		{
			continue;
		}

		if (!IsVisibleWidgetInstance(Widget))
		{
			continue;
		}

		for (const FName AttachmentListName : AttachmentListNames)
		{
			UWidget* AttachmentList = Widget->GetWidgetFromName(AttachmentListName);
			if (!AttachmentList)
			{
				continue;
			}

			TArray<UTextBlock*> AttachmentLabels;
			VisitWidgetTree(AttachmentList, [&AttachmentLabels](UWidget* ChildWidget)
			{
				UTextBlock* TextBlock = Cast<UTextBlock>(ChildWidget);
				if (!IsValid(TextBlock) || !IsAttachmentSelectionOptionText(TextBlock->GetText().ToString()))
				{
					return;
				}

				AttachmentLabels.Add(TextBlock);
			});

			UTextBlock* BestLabel = nullptr;
			int32 BestScore = INDEX_NONE;
			for (UTextBlock* TextBlock : AttachmentLabels)
			{
				SeenLabels.Add(TextBlock);
				FAttachmentSelectionLabelStyle* StoredStyle = AttachmentSelectionLabelStyles.Find(TextBlock);
				if (!StoredStyle)
				{
					FAttachmentSelectionLabelStyle NewStyle;
					NewStyle.OriginalColorAndOpacity = TextBlock->GetColorAndOpacity();
					StoredStyle = &AttachmentSelectionLabelStyles.Add(TextBlock, NewStyle);
				}

				const int32 Score = ScoreAttachmentSelectionTextAgainstTokensWithGeneratedIconAlias(
					TextBlock->GetText().ToString(),
					HighlightTokens);
				if (Score > BestScore)
				{
					BestScore = Score;
					BestLabel = TextBlock;
				}
			}

			for (UTextBlock* TextBlock : AttachmentLabels)
			{
				const FAttachmentSelectionLabelStyle* StoredStyle = AttachmentSelectionLabelStyles.Find(TextBlock);
				if (!StoredStyle)
				{
					continue;
				}

				const bool bHighlighted = TextBlock == BestLabel && BestScore != INDEX_NONE;
				TextBlock->SetColorAndOpacity(bHighlighted
					? FSlateColor(AttachmentSelectionHighlightColor())
					: StoredStyle->OriginalColorAndOpacity);
				UpdateGeneratedAttachmentIcon(TextBlock, bHighlighted);
			}
		}
	}

	for (auto It = AttachmentSelectionLabelStyles.CreateIterator(); It; ++It)
	{
		UTextBlock* TextBlock = It.Key().Get();
		if (!IsValid(TextBlock))
		{
			It.RemoveCurrent();
			continue;
		}

		if (!SeenLabels.Contains(TextBlock))
		{
			TextBlock->SetColorAndOpacity(It.Value().OriginalColorAndOpacity);
			It.RemoveCurrent();
		}
	}

	TArray<UTextBlock*> GeneratedIconLabelsToRestore;
	for (auto It = GeneratedAttachmentIconStyles.CreateIterator(); It; ++It)
	{
		UTextBlock* TextBlock = It.Key().Get();
		if (!IsValid(TextBlock))
		{
			It.RemoveCurrent();
			continue;
		}

		if (!SeenLabels.Contains(TextBlock))
		{
			GeneratedIconLabelsToRestore.Add(TextBlock);
		}
	}

	for (UTextBlock* TextBlock : GeneratedIconLabelsToRestore)
	{
		RestoreGeneratedAttachmentIconStyle(TextBlock);
	}
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateGeneratedAttachmentIcon(UTextBlock* TextBlock, const bool bSelected)
{
	if (!IsValid(TextBlock))
	{
		return;
	}

	const FGeneratedAttachmentIconDefinition* Definition =
		FindGeneratedAttachmentIconDefinitionForText(TextBlock->GetText().ToString());
	if (!Definition)
	{
		return;
	}

	UTexture2D* RealIcon = LoadGeneratedAttachmentIcon(*Definition);
	if (!RealIcon)
	{
		return;
	}

	FGeneratedAttachmentIconStyle* StoredStyle = GeneratedAttachmentIconStyles.Find(TextBlock);
	if (!StoredStyle)
	{
		UButton* Button = FindOwningButton(TextBlock);
		if (!IsValid(Button))
		{
			return;
		}

		UOverlay* Overlay = NewObject<UOverlay>(Button);
		UScaleBox* IconScaleBox = NewObject<UScaleBox>(Overlay);
		UImage* IconImage = NewObject<UImage>(IconScaleBox);
		UImage* FrameImage = NewObject<UImage>(Overlay);
		if (!Overlay || !IconScaleBox || !IconImage || !FrameImage)
		{
			return;
		}

		Overlay->SetVisibility(ESlateVisibility::HitTestInvisible);
		Overlay->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		IconScaleBox->SetVisibility(ESlateVisibility::HitTestInvisible);
		IconScaleBox->SetStretch(EStretch::ScaleToFit);
		IconScaleBox->SetStretchDirection(EStretchDirection::Both);
		IconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		IconImage->SetDesiredSizeOverride(GeneratedAttachmentIconSize());
		FrameImage->SetVisibility(ESlateVisibility::HitTestInvisible);

		StoredStyle = &GeneratedAttachmentIconStyles.Add(TextBlock);
		StoredStyle->Button = Button;
		StoredStyle->Label = TextBlock;
		StoredStyle->Overlay = Overlay;
		StoredStyle->IconScaleBox = IconScaleBox;
		StoredStyle->IconImage = IconImage;
		StoredStyle->FrameImage = FrameImage;
		StoredStyle->OriginalLabelVisibility = TextBlock->GetVisibility();
		StoredStyle->OriginalLabelRenderOpacity = TextBlock->GetRenderOpacity();

		IconScaleBox->SetContent(IconImage);
		if (UOverlaySlot* IconSlot = Overlay->AddChildToOverlay(IconScaleBox))
		{
			IconSlot->SetHorizontalAlignment(HAlign_Fill);
			IconSlot->SetVerticalAlignment(VAlign_Fill);
		}

		if (UOverlaySlot* FrameSlot = Overlay->AddChildToOverlay(FrameImage))
		{
			FrameSlot->SetHorizontalAlignment(HAlign_Fill);
			FrameSlot->SetVerticalAlignment(VAlign_Fill);
		}

		TextBlock->SetRenderOpacity(0.0f);
		TextBlock->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UOverlaySlot* LabelSlot = Overlay->AddChildToOverlay(TextBlock))
		{
			LabelSlot->SetHorizontalAlignment(HAlign_Center);
			LabelSlot->SetVerticalAlignment(VAlign_Center);
		}

		Button->SetContent(Overlay);
	}

	UButton* Button = StoredStyle->Button.Get();
	UImage* IconImage = StoredStyle->IconImage.Get();
	UImage* FrameImage = StoredStyle->FrameImage.Get();
	if (!IsValid(Button) || !IsValid(IconImage) || !IsValid(FrameImage))
	{
		RestoreGeneratedAttachmentIconStyle(TextBlock);
		return;
	}

	IconImage->SetBrush(MakeGeneratedAttachmentIconBrush(RealIcon));
	IconImage->SetColorAndOpacity(FLinearColor::White);
	if (UOverlay* Overlay = StoredStyle->Overlay.Get())
	{
		const float HoverScale = GeneratedAttachmentHoverScale(Button->IsHovered());
		Overlay->SetRenderScale(FVector2D(HoverScale, HoverScale));
	}
	FrameImage->SetBrush(MakeGeneratedAttachmentFrameBrush(bSelected));
	FrameImage->SetColorAndOpacity(bSelected ? GeneratedAttachmentIconSelectionFrameColor() : FLinearColor::Transparent);
}

void UTMMenuViewerMeshTransitionSubsystem::RestoreGeneratedAttachmentIconStyle(UTextBlock* TextBlock)
{
	if (!TextBlock)
	{
		return;
	}

	FGeneratedAttachmentIconStyle* StoredStyle = GeneratedAttachmentIconStyles.Find(TextBlock);
	if (!StoredStyle)
	{
		return;
	}

	UTextBlock* Label = StoredStyle->Label.Get();
	UButton* Button = StoredStyle->Button.Get();
	UOverlay* Overlay = StoredStyle->Overlay.Get();
	if (IsValid(Label))
	{
		Label->SetRenderOpacity(StoredStyle->OriginalLabelRenderOpacity);
		Label->SetVisibility(StoredStyle->OriginalLabelVisibility);
	}

	if (IsValid(Button) && IsValid(Label))
	{
		if (IsValid(Overlay))
		{
			Overlay->RemoveChild(Label);
		}
		Button->SetContent(Label);
	}

	GeneratedAttachmentIconStyles.Remove(TextBlock);
}

void UTMMenuViewerMeshTransitionSubsystem::RestoreGeneratedAttachmentIconStyles()
{
	TArray<UTextBlock*> LabelsToRestore;
	for (auto It = GeneratedAttachmentIconStyles.CreateIterator(); It; ++It)
	{
		if (UTextBlock* TextBlock = It.Key().Get())
		{
			LabelsToRestore.Add(TextBlock);
		}
	}

	for (UTextBlock* TextBlock : LabelsToRestore)
	{
		RestoreGeneratedAttachmentIconStyle(TextBlock);
	}

	GeneratedAttachmentIconStyles.Empty();
}

void UTMMenuViewerMeshTransitionSubsystem::RestoreAttachmentSelectionHighlight()
{
	for (auto It = AttachmentSelectionLabelStyles.CreateIterator(); It; ++It)
	{
		UTextBlock* TextBlock = It.Key().Get();
		if (IsValid(TextBlock))
		{
			TextBlock->SetColorAndOpacity(It.Value().OriginalColorAndOpacity);
		}
		It.RemoveCurrent();
	}

	RestoreGeneratedAttachmentIconStyles();
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateWeaponSelectionHighlight(UWorld* World, const bool bLoadoutVisible)
{
	if (!World || !bLoadoutVisible)
	{
		RestoreWeaponSelectionHighlight();
		return;
	}

	TSet<FString> HighlightTokens;
	if (!ResolveActiveLoadoutWeaponHighlightTokens(World, HighlightTokens) || HighlightTokens.IsEmpty())
	{
		HighlightTokens.Add(NormalizeAttachmentFocusToken(TEXT("Empty")));
	}

	TSet<TWeakObjectPtr<UTextBlock>> SeenLabels;
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->GetWorld() != World)
		{
			continue;
		}

		const UClass* WidgetClass = Widget->GetClass();
		if (!WidgetClass || !WidgetClass->GetPathName().Contains(TEXT("W_Loadout")))
		{
			continue;
		}

		if (!IsVisibleWidgetInstance(Widget))
		{
			continue;
		}

		TArray<UTextBlock*> WeaponLabels;
		VisitWidgetTree(Widget, [&WeaponLabels](UWidget* ChildWidget)
		{
			UTextBlock* TextBlock = Cast<UTextBlock>(ChildWidget);
			if (!IsValid(TextBlock))
			{
				return;
			}

			const FString NormalizedText = NormalizeAttachmentFocusToken(TextBlock->GetText().ToString());
			if (!NormalizedText.IsEmpty())
			{
				WeaponLabels.Add(TextBlock);
			}
		});

		UTextBlock* BestLabel = nullptr;
		int32 BestScore = INDEX_NONE;
		for (UTextBlock* TextBlock : WeaponLabels)
		{
			SeenLabels.Add(TextBlock);
			FAttachmentSelectionLabelStyle* StoredStyle = WeaponSelectionLabelStyles.Find(TextBlock);
			if (!StoredStyle)
			{
				FAttachmentSelectionLabelStyle NewStyle;
				NewStyle.OriginalColorAndOpacity = TextBlock->GetColorAndOpacity();
				StoredStyle = &WeaponSelectionLabelStyles.Add(TextBlock, NewStyle);
			}

			const int32 Score = ScoreSelectionTextAgainstTokens(TextBlock->GetText().ToString(), HighlightTokens);
			if (Score > BestScore)
			{
				BestScore = Score;
				BestLabel = TextBlock;
			}
		}

		for (UTextBlock* TextBlock : WeaponLabels)
		{
			const FAttachmentSelectionLabelStyle* StoredStyle = WeaponSelectionLabelStyles.Find(TextBlock);
			if (!StoredStyle)
			{
				continue;
			}

			const bool bHighlighted = TextBlock == BestLabel && BestScore != INDEX_NONE;
			TextBlock->SetColorAndOpacity(bHighlighted
				? FSlateColor(AttachmentSelectionHighlightColor())
				: StoredStyle->OriginalColorAndOpacity);
		}
	}

	for (auto It = WeaponSelectionLabelStyles.CreateIterator(); It; ++It)
	{
		UTextBlock* TextBlock = It.Key().Get();
		if (!IsValid(TextBlock))
		{
			It.RemoveCurrent();
			continue;
		}

		if (!SeenLabels.Contains(TextBlock))
		{
			TextBlock->SetColorAndOpacity(It.Value().OriginalColorAndOpacity);
			It.RemoveCurrent();
		}
	}
}

void UTMMenuViewerMeshTransitionSubsystem::RestoreWeaponSelectionHighlight()
{
	for (auto It = WeaponSelectionLabelStyles.CreateIterator(); It; ++It)
	{
		UTextBlock* TextBlock = It.Key().Get();
		if (IsValid(TextBlock))
		{
			TextBlock->SetColorAndOpacity(It.Value().OriginalColorAndOpacity);
		}
		It.RemoveCurrent();
	}
}

bool UTMMenuViewerMeshTransitionSubsystem::ResolveActiveLoadoutWeaponHighlightTokens(
	UWorld* World,
	TSet<FString>& OutTokens) const
{
	OutTokens.Reset();
	if (!World)
	{
		return false;
	}

	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->GetWorld() != World)
		{
			continue;
		}

		const UClass* WidgetClass = Widget->GetClass();
		if (!WidgetClass || !WidgetClass->GetPathName().Contains(TEXT("W_Loadout")))
		{
			continue;
		}

		if (!IsVisibleWidgetInstance(Widget))
		{
			continue;
		}

		const FObjectPropertyBase* ActiveWeaponProperty =
			FindFProperty<FObjectPropertyBase>(WidgetClass, TEXT("ActiveWeapon"));
		if (!ActiveWeaponProperty)
		{
			continue;
		}

		const UObject* ActiveWeaponObject =
			ActiveWeaponProperty->GetObjectPropertyValue_InContainer(Widget);
		if (!IsValid(ActiveWeaponObject))
		{
			continue;
		}

		if (const AGun* ActiveGun = Cast<AGun>(ActiveWeaponObject))
		{
			AddAttachmentHighlightToken(OutTokens, ActiveGun->GetWeaponDisplayName().ToString());
		}

		AddAttachmentHighlightToken(OutTokens, ActiveWeaponObject->GetName());
		if (const UClass* ActiveWeaponClass = ActiveWeaponObject->GetClass())
		{
			AddAttachmentHighlightToken(OutTokens, ActiveWeaponClass->GetName());
		}

		if (!OutTokens.IsEmpty())
		{
			return true;
		}
	}

	return false;
}

bool UTMMenuViewerMeshTransitionSubsystem::ResolveInstalledAttachmentHighlightTokens(
	UWorld* World,
	const EAttachmentCameraFocusGroup Group,
	TSet<FString>& OutTokens) const
{
	OutTokens.Reset();
	if (!World || Group == EAttachmentCameraFocusGroup::None)
	{
		return false;
	}

	bool bFoundInstalledAttachment = false;
	const UCameraComponent* CameraComponent = ResolveActiveCameraComponent(World);
	const FVector CameraLocation = CameraComponent ? CameraComponent->GetComponentLocation() : FVector::ZeroVector;
	if (AActor* WeaponActor = ResolveAttachmentsPreviewWeaponActor(World, CameraLocation))
	{
		bFoundInstalledAttachment = ReadInstalledAttachmentHighlightTokensFromObject(
			WeaponActor,
			Group,
			OutTokens);
		if (bFoundInstalledAttachment && !OutTokens.IsEmpty())
		{
			return true;
		}
	}

	return ResolveAttachmentComponentHighlightTokens(World, Group, OutTokens) && !OutTokens.IsEmpty();
}

bool UTMMenuViewerMeshTransitionSubsystem::ReadInstalledAttachmentHighlightTokensFromObject(
	const UObject* Object,
	const EAttachmentCameraFocusGroup Group,
	TSet<FString>& OutTokens) const
{
	if (!IsValid(Object) || Group == EAttachmentCameraFocusGroup::None)
	{
		return false;
	}

	bool bFoundInstalledAttachment = false;
	for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
	{
		const FProperty* Property = *It;
		if (!Property)
		{
			continue;
		}

		const FString PropertyName = Property->GetName();
		if (IsCompatibleAttachmentListPropertyName(PropertyName)
			|| !IsInstalledAttachmentPropertyName(PropertyName))
		{
			continue;
		}

		const FString NormalizedPropertyName = NormalizeAttachmentFocusToken(PropertyName);
		const bool bActiveAttachmentArray =
			NormalizedPropertyName.Contains(TEXT("ActiveAttachments"), ESearchCase::IgnoreCase);
		const bool bGroupSpecificProperty =
			(Group == EAttachmentCameraFocusGroup::Muzzle
				&& NormalizedPropertyName.Contains(TEXT("Muzzle"), ESearchCase::IgnoreCase))
			|| (Group == EAttachmentCameraFocusGroup::Optics
				&& (NormalizedPropertyName.Contains(TEXT("Optic"), ESearchCase::IgnoreCase)
					|| NormalizedPropertyName.Contains(TEXT("Sight"), ESearchCase::IgnoreCase)))
			|| (Group == EAttachmentCameraFocusGroup::SideRail
				&& NormalizedPropertyName.Contains(TEXT("SideRail"), ESearchCase::IgnoreCase))
			|| (Group == EAttachmentCameraFocusGroup::Underbarrel
				&& NormalizedPropertyName.Contains(TEXT("Underbarrel"), ESearchCase::IgnoreCase));

		if (!bActiveAttachmentArray && !bGroupSpecificProperty)
		{
			continue;
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
		TArray<FString> ValueTexts;
		AppendAttachmentFocusValueTexts(Property, ValuePtr, ValueTexts);
		for (const FString& ValueText : ValueTexts)
		{
			if (IsEmptyAttachmentFocusValue(ValueText))
			{
				continue;
			}

			const EAttachmentCameraFocusGroup ValueGroup = InferAttachmentCameraFocusGroupFromText(ValueText);
			if (ValueGroup == Group || bGroupSpecificProperty)
			{
				const int32 PreviousTokenCount = OutTokens.Num();
				AddAttachmentHighlightToken(OutTokens, ValueText);
				bFoundInstalledAttachment |= OutTokens.Num() > PreviousTokenCount;
			}
		}
	}

	return bFoundInstalledAttachment;
}

bool UTMMenuViewerMeshTransitionSubsystem::ResolveAttachmentComponentHighlightTokens(
	UWorld* World,
	const EAttachmentCameraFocusGroup Group,
	TSet<FString>& OutTokens) const
{
	if (!World || Group == EAttachmentCameraFocusGroup::None)
	{
		return false;
	}

	const UCameraComponent* CameraComponent = ResolveActiveCameraComponent(World);
	const FVector CameraLocation = CameraComponent ? CameraComponent->GetComponentLocation() : FVector::ZeroVector;
	AActor* WeaponActor = ResolveAttachmentsPreviewWeaponActor(World, CameraLocation);
	if (!IsValid(WeaponActor))
	{
		return false;
	}

	TArray<FName, TInlineAllocator<6>> CandidateSockets;
	switch (Group)
	{
	case EAttachmentCameraFocusGroup::Optics:
		CandidateSockets.Append({ TEXT("Optics"), TEXT("RearSight"), TEXT("AimTarget"), TEXT("ADS_Eye") });
		break;
	case EAttachmentCameraFocusGroup::SideRail:
		CandidateSockets.Append({ TEXT("AT_Backup"), TEXT("SideRail"), TEXT("Canted"), TEXT("Backup") });
		break;
	case EAttachmentCameraFocusGroup::Underbarrel:
		CandidateSockets.Append({ TEXT("Underbarrel"), TEXT("Foregrip") });
		break;
	case EAttachmentCameraFocusGroup::Muzzle:
		CandidateSockets.Append({ TEXT("Muzzle"), TEXT("MuzzleSilencerco") });
		break;
	default:
		break;
	}

	bool bFoundInstalledAttachment = false;
	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(WeaponActor);
	for (const UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!IsValid(PrimitiveComponent)
			|| !PrimitiveComponent->IsRegistered()
			|| !PrimitiveComponent->IsVisible()
			|| PrimitiveComponent->bHiddenInGame)
		{
			continue;
		}

		const FString AttachSocketName = PrimitiveComponent->GetAttachSocketName().ToString();
		bool bMatchesSocket = false;
		for (const FName CandidateSocket : CandidateSockets)
		{
			if (AttachSocketName.Equals(CandidateSocket.ToString(), ESearchCase::IgnoreCase)
				|| AttachSocketName.Contains(CandidateSocket.ToString(), ESearchCase::IgnoreCase))
			{
				bMatchesSocket = true;
				break;
			}
		}

		if (!bMatchesSocket)
		{
			continue;
		}

		const int32 PreviousTokenCount = OutTokens.Num();
		AddAttachmentHighlightToken(OutTokens, PrimitiveComponent->GetName());
		AddAttachmentHighlightToken(OutTokens, PrimitiveComponent->GetPathName());
		if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(PrimitiveComponent))
		{
			if (const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
			{
				AddAttachmentHighlightToken(OutTokens, StaticMesh->GetName());
				AddAttachmentHighlightToken(OutTokens, StaticMesh->GetPathName());
			}
		}
		bFoundInstalledAttachment |= OutTokens.Num() > PreviousTokenCount;
	}

	return bFoundInstalledAttachment;
}

bool UTMMenuViewerMeshTransitionSubsystem::ResolveAttachmentCameraFocusInstallSignature(
	UWorld* World,
	const EAttachmentCameraFocusGroup Group,
	uint32& OutSignature) const
{
	OutSignature = 0;
	if (!World || Group == EAttachmentCameraFocusGroup::None)
	{
		return false;
	}

	bool bFoundInstalledAttachment = false;
	const UCameraComponent* CameraComponent = ResolveActiveCameraComponent(World);
	const FVector CameraLocation = CameraComponent ? CameraComponent->GetComponentLocation() : FVector::ZeroVector;
	if (AActor* WeaponActor = ResolveAttachmentsPreviewWeaponActor(World, CameraLocation))
	{
		bFoundInstalledAttachment |= ReadAttachmentCameraFocusInstallSignatureFromObject(
			WeaponActor,
			Group,
			OutSignature);
	}

	return bFoundInstalledAttachment && OutSignature != 0;
}

bool UTMMenuViewerMeshTransitionSubsystem::ReadAttachmentCameraFocusInstallSignatureFromObject(
	const UObject* Object,
	const EAttachmentCameraFocusGroup Group,
	uint32& OutSignature) const
{
	if (!IsValid(Object) || Group == EAttachmentCameraFocusGroup::None)
	{
		return false;
	}

	bool bFoundInstalledAttachment = false;
	for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
	{
		const FProperty* Property = *It;
		if (!Property)
		{
			continue;
		}

		const FString PropertyName = Property->GetName();
		if (IsCompatibleAttachmentListPropertyName(PropertyName)
			|| !IsInstalledAttachmentPropertyName(PropertyName))
		{
			continue;
		}

		const FString NormalizedPropertyName = NormalizeAttachmentFocusToken(PropertyName);
		const bool bActiveAttachmentArray =
			NormalizedPropertyName.Contains(TEXT("ActiveAttachments"), ESearchCase::IgnoreCase);
		const bool bGroupSpecificProperty =
			(Group == EAttachmentCameraFocusGroup::Muzzle
				&& NormalizedPropertyName.Contains(TEXT("Muzzle"), ESearchCase::IgnoreCase))
			|| (Group == EAttachmentCameraFocusGroup::Optics
				&& (NormalizedPropertyName.Contains(TEXT("Optic"), ESearchCase::IgnoreCase)
					|| NormalizedPropertyName.Contains(TEXT("Sight"), ESearchCase::IgnoreCase)))
			|| (Group == EAttachmentCameraFocusGroup::SideRail
				&& NormalizedPropertyName.Contains(TEXT("SideRail"), ESearchCase::IgnoreCase))
			|| (Group == EAttachmentCameraFocusGroup::Underbarrel
				&& NormalizedPropertyName.Contains(TEXT("Underbarrel"), ESearchCase::IgnoreCase));

		if (!bActiveAttachmentArray && !bGroupSpecificProperty)
		{
			continue;
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
		TArray<FString> ValueTexts;
		AppendAttachmentFocusValueTexts(Property, ValuePtr, ValueTexts);
		for (const FString& ValueText : ValueTexts)
		{
			if (IsEmptyAttachmentFocusValue(ValueText))
			{
				continue;
			}

			const EAttachmentCameraFocusGroup ValueGroup = InferAttachmentCameraFocusGroupFromText(ValueText);
			const bool bObjectValue = CastField<FObjectPropertyBase>(Property) != nullptr;
			if (ValueGroup == Group || (bGroupSpecificProperty && !bObjectValue))
			{
				OutSignature = CombineAttachmentFocusSignature(
					OutSignature,
					FString::Printf(TEXT("%s:%s"), *PropertyName, *ValueText));
				bFoundInstalledAttachment = true;
			}
		}
	}

	return bFoundInstalledAttachment;
}

bool UTMMenuViewerMeshTransitionSubsystem::ResolveAttachmentCameraFocusComponentInstallSignature(
	UWorld* World,
	const EAttachmentCameraFocusGroup Group,
	uint32& OutSignature) const
{
	if (!World || Group == EAttachmentCameraFocusGroup::None)
	{
		return false;
	}

	const UCameraComponent* CameraComponent = ResolveActiveCameraComponent(World);
	const FVector CameraLocation = CameraComponent ? CameraComponent->GetComponentLocation() : FVector::ZeroVector;
	AActor* WeaponActor = ResolveAttachmentsPreviewWeaponActor(World, CameraLocation);
	if (!IsValid(WeaponActor))
	{
		return false;
	}

	TArray<FName, TInlineAllocator<6>> CandidateSockets;
	switch (Group)
	{
	case EAttachmentCameraFocusGroup::Optics:
		CandidateSockets.Append({ TEXT("Optics"), TEXT("RearSight"), TEXT("AimTarget"), TEXT("ADS_Eye") });
		break;
	case EAttachmentCameraFocusGroup::SideRail:
		CandidateSockets.Append({ TEXT("AT_Backup"), TEXT("SideRail"), TEXT("Canted"), TEXT("Backup") });
		break;
	case EAttachmentCameraFocusGroup::Underbarrel:
		CandidateSockets.Append({ TEXT("Underbarrel"), TEXT("Foregrip") });
		break;
	case EAttachmentCameraFocusGroup::Muzzle:
		CandidateSockets.Append({ TEXT("Muzzle"), TEXT("MuzzleSilencerco") });
		break;
	default:
		break;
	}

	bool bFoundInstalledAttachment = false;
	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(WeaponActor);
	for (const UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!IsValid(PrimitiveComponent)
			|| !PrimitiveComponent->IsRegistered()
			|| !PrimitiveComponent->IsVisible()
			|| PrimitiveComponent->bHiddenInGame)
		{
			continue;
		}

		const FString AttachSocketName = PrimitiveComponent->GetAttachSocketName().ToString();
		for (const FName CandidateSocket : CandidateSockets)
		{
			if (AttachSocketName.Equals(CandidateSocket.ToString(), ESearchCase::IgnoreCase)
				|| AttachSocketName.Contains(CandidateSocket.ToString(), ESearchCase::IgnoreCase))
			{
				OutSignature = CombineAttachmentFocusSignature(
					OutSignature,
					FString::Printf(
						TEXT("%s:%s"),
						*PrimitiveComponent->GetPathName(),
						*AttachSocketName));
				bFoundInstalledAttachment = true;
				break;
			}
		}
	}

	return bFoundInstalledAttachment;
}

float UTMMenuViewerMeshTransitionSubsystem::ResolveAttachmentCameraFocusBaseYaw(
	const EAttachmentCameraFocusGroup Group)
{
	switch (Group)
	{
	case EAttachmentCameraFocusGroup::Optics:
		return CVarAttachmentsCameraFocusOpticsYaw.GetValueOnGameThread();
	case EAttachmentCameraFocusGroup::SideRail:
		return CVarAttachmentsCameraFocusSideRailYaw.GetValueOnGameThread();
	case EAttachmentCameraFocusGroup::Underbarrel:
		return CVarAttachmentsCameraFocusUnderbarrelYaw.GetValueOnGameThread();
	case EAttachmentCameraFocusGroup::Muzzle:
		return CVarAttachmentsCameraFocusMuzzleYaw.GetValueOnGameThread();
	default:
		return 0.0f;
	}
}

bool UTMMenuViewerMeshTransitionSubsystem::ResolveAttachmentCameraFocusTurnSubject(
	UWorld* World,
	const EAttachmentCameraFocusGroup Group,
	FVector& OutAttachmentLocation,
	FVector& OutWeaponCenter) const
{
	OutAttachmentLocation = FVector::ZeroVector;
	OutWeaponCenter = FVector::ZeroVector;
	if (!World || Group == EAttachmentCameraFocusGroup::None)
	{
		return false;
	}

	const UCameraComponent* CameraComponent = ResolveActiveCameraComponent(World);
	const FVector CameraLocation = CameraComponent ? CameraComponent->GetComponentLocation() : FVector::ZeroVector;
	AActor* WeaponActor = ResolveAttachmentsPreviewWeaponActor(World, CameraLocation);
	if (!IsValid(WeaponActor))
	{
		return false;
	}

	FVector WeaponExtent = FVector::ZeroVector;
	WeaponActor->GetActorBounds(false, OutWeaponCenter, WeaponExtent);

	TArray<FName, TInlineAllocator<6>> CandidateSockets;
	switch (Group)
	{
	case EAttachmentCameraFocusGroup::Optics:
		CandidateSockets.Append({ TEXT("Optics"), TEXT("RearSight"), TEXT("AimTarget"), TEXT("ADS_Eye") });
		break;
	case EAttachmentCameraFocusGroup::SideRail:
		CandidateSockets.Append({ TEXT("AT_Backup"), TEXT("SideRail"), TEXT("Canted"), TEXT("Backup") });
		break;
	case EAttachmentCameraFocusGroup::Underbarrel:
		CandidateSockets.Append({ TEXT("Underbarrel"), TEXT("Foregrip") });
		break;
	case EAttachmentCameraFocusGroup::Muzzle:
		CandidateSockets.Append({ TEXT("Muzzle"), TEXT("MuzzleSilencerco") });
		break;
	default:
		break;
	}

	FBox AttachmentBounds(ForceInit);
	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(WeaponActor);
	for (const UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!IsValid(PrimitiveComponent)
			|| !PrimitiveComponent->IsRegistered()
			|| !PrimitiveComponent->IsVisible()
			|| PrimitiveComponent->bHiddenInGame)
		{
			continue;
		}

		const FString AttachSocketName = PrimitiveComponent->GetAttachSocketName().ToString();
		bool bMatchesSocket = false;
		for (const FName CandidateSocket : CandidateSockets)
		{
			if (AttachSocketName.Equals(CandidateSocket.ToString(), ESearchCase::IgnoreCase)
				|| AttachSocketName.Contains(CandidateSocket.ToString(), ESearchCase::IgnoreCase))
			{
				bMatchesSocket = true;
				break;
			}
		}

		if (bMatchesSocket)
		{
			AttachmentBounds += FBox::BuildAABB(PrimitiveComponent->Bounds.Origin, PrimitiveComponent->Bounds.BoxExtent);
		}
	}

	if (AttachmentBounds.IsValid)
	{
		OutAttachmentLocation = AttachmentBounds.GetCenter();
		return !OutAttachmentLocation.Equals(OutWeaponCenter, 1.0f);
	}

	return ResolveAttachmentCameraFocusSocketWorldLocation(World, Group, OutAttachmentLocation)
		&& !OutAttachmentLocation.Equals(OutWeaponCenter, 1.0f);
}

float UTMMenuViewerMeshTransitionSubsystem::ResolveAttachmentCameraFocusTurnYaw(
	UWorld* World,
	const UCameraComponent* CameraComponent,
	const EAttachmentCameraFocusGroup Group,
	const float MaxYaw,
	const float CurrentYaw) const
{
	const float BaseYaw = ResolveAttachmentCameraFocusBaseYaw(Group);
	if (!World || !CameraComponent || Group == EAttachmentCameraFocusGroup::None || MaxYaw <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	FVector AttachmentLocation = FVector::ZeroVector;
	FVector WeaponCenter = FVector::ZeroVector;
	if (!ResolveAttachmentCameraFocusTurnSubject(World, Group, AttachmentLocation, WeaponCenter))
	{
		return FMath::Clamp(BaseYaw, -MaxYaw, MaxYaw) - BaseYaw;
	}

	const float NormalizedCurrentYaw = FMath::Clamp(FRotator::NormalizeAxis(CurrentYaw), -MaxYaw, MaxYaw);
	const float MinCandidateYaw = FMath::Max(-MaxYaw, NormalizedCurrentYaw - MaxYaw);
	const float MaxCandidateYaw = FMath::Min(MaxYaw, NormalizedCurrentYaw + MaxYaw);

	TArray<float, TInlineAllocator<4>> CandidateYaws;
	const auto AddCandidateYaw = [&CandidateYaws](const float CandidateYaw)
	{
		for (const float ExistingYaw : CandidateYaws)
		{
			if (FMath::IsNearlyEqual(ExistingYaw, CandidateYaw, 0.01f))
			{
				return;
			}
		}

		CandidateYaws.Add(CandidateYaw);
	};

	AddCandidateYaw(MinCandidateYaw);
	AddCandidateYaw(MaxCandidateYaw);
	AddCandidateYaw(FMath::Clamp(NormalizedCurrentYaw, MinCandidateYaw, MaxCandidateYaw));
	AddCandidateYaw(FMath::Clamp(BaseYaw, MinCandidateYaw, MaxCandidateYaw));

	const USceneComponent* ParentComponent = CameraComponent->GetAttachParent();
	const FTransform ParentWorldTransform = ParentComponent
		? ParentComponent->GetComponentTransform()
		: FTransform::Identity;
	const FVector AttachmentOutward = (AttachmentLocation - WeaponCenter).GetSafeNormal();
	if (AttachmentOutward.IsNearlyZero())
	{
		return FMath::Clamp(BaseYaw, -MaxYaw, MaxYaw) - BaseYaw;
	}

	float BestYaw = FMath::Clamp(BaseYaw, MinCandidateYaw, MaxCandidateYaw);
	float BestScore = -TNumericLimits<float>::Max();
	float BestDelta = TNumericLimits<float>::Max();
	for (const float CandidateYaw : CandidateYaws)
	{
		const FTransform CandidateRelativeTransform(
			MainMenuCameraDriftBaseRelativeRotation + FRotator(0.0f, CandidateYaw, 0.0f),
			MainMenuCameraDriftBaseRelativeLocation);
		const FTransform CandidateWorldTransform = CandidateRelativeTransform * ParentWorldTransform;
		const FVector CandidateForward = CandidateWorldTransform.GetRotation().GetForwardVector().GetSafeNormal();
		if (CandidateForward.IsNearlyZero())
		{
			continue;
		}

		const float CandidateScore = FVector::DotProduct(AttachmentOutward, -CandidateForward);
		const float CandidateDelta = FMath::Abs(CandidateYaw - NormalizedCurrentYaw);
		if (CandidateScore > BestScore + KINDA_SMALL_NUMBER
			|| (FMath::IsNearlyEqual(CandidateScore, BestScore, KINDA_SMALL_NUMBER) && CandidateDelta < BestDelta))
		{
			BestYaw = CandidateYaw;
			BestScore = CandidateScore;
			BestDelta = CandidateDelta;
		}
	}

	if (!FMath::IsFinite(BestScore))
	{
		return FMath::Clamp(BaseYaw, -MaxYaw, MaxYaw) - BaseYaw;
	}

	UE_LOG(
		LogTMMenuViewerMeshTransition,
		Display,
		TEXT("[TMAttachmentsCameraFocus] Turn Group=%s CurrentYaw=%.1f ChosenYaw=%.1f MaxYaw=%.1f Score=%.3f Attachment=%s Weapon=%s"),
		*DescribeAttachmentCameraFocusGroup(Group),
		NormalizedCurrentYaw,
		BestYaw,
		MaxYaw,
		BestScore,
		*AttachmentLocation.ToString(),
		*WeaponCenter.ToString());

	return BestYaw - BaseYaw;
}

UTMMenuViewerMeshTransitionSubsystem::FAttachmentCameraFocusPose
UTMMenuViewerMeshTransitionSubsystem::ResolveAttachmentCameraFocusPose(
	UWorld* World,
	const UCameraComponent* CameraComponent,
	const EAttachmentCameraFocusGroup Group,
	const float AdditionalPitch,
	const float AdditionalYaw,
	const float RequestedTargetScreenX,
	const float RequestedTargetScreenY,
	const float RequestedDepthScale) const
{
	float PanY = 0.0f;
	float HeightZ = 0.0f;
	float Pitch = 0.0f;
	float Yaw = 0.0f;

	switch (Group)
	{
	case EAttachmentCameraFocusGroup::Optics:
		PanY = CVarAttachmentsCameraFocusOpticsPanY.GetValueOnGameThread();
		HeightZ = CVarAttachmentsCameraFocusOpticsHeightZ.GetValueOnGameThread();
		Pitch = CVarAttachmentsCameraFocusOpticsPitch.GetValueOnGameThread();
		Yaw = CVarAttachmentsCameraFocusOpticsYaw.GetValueOnGameThread();
		break;
	case EAttachmentCameraFocusGroup::SideRail:
		PanY = CVarAttachmentsCameraFocusSideRailPanY.GetValueOnGameThread();
		HeightZ = CVarAttachmentsCameraFocusSideRailHeightZ.GetValueOnGameThread();
		Pitch = CVarAttachmentsCameraFocusSideRailPitch.GetValueOnGameThread();
		Yaw = CVarAttachmentsCameraFocusSideRailYaw.GetValueOnGameThread();
		break;
	case EAttachmentCameraFocusGroup::Underbarrel:
		PanY = CVarAttachmentsCameraFocusUnderbarrelPanY.GetValueOnGameThread();
		HeightZ = CVarAttachmentsCameraFocusUnderbarrelHeightZ.GetValueOnGameThread();
		Pitch = CVarAttachmentsCameraFocusUnderbarrelPitch.GetValueOnGameThread();
		Yaw = CVarAttachmentsCameraFocusUnderbarrelYaw.GetValueOnGameThread();
		break;
	case EAttachmentCameraFocusGroup::Muzzle:
		PanY = CVarAttachmentsCameraFocusMuzzlePanY.GetValueOnGameThread();
		HeightZ = CVarAttachmentsCameraFocusMuzzleHeightZ.GetValueOnGameThread();
		Pitch = CVarAttachmentsCameraFocusMuzzlePitch.GetValueOnGameThread();
		Yaw = CVarAttachmentsCameraFocusMuzzleYaw.GetValueOnGameThread();
		break;
	default:
		break;
	}

	const float LocationScale = FMath::Clamp(CVarAttachmentsCameraFocusLocationScale.GetValueOnGameThread(), 0.0f, 4.0f);
	const float RotationScale = FMath::Clamp(CVarAttachmentsCameraFocusRotationScale.GetValueOnGameThread(), 0.0f, 4.0f);
	FAttachmentCameraFocusPose Pose;
	Pose.LocationOffset = FVector(
		0.0f,
		FMath::Clamp(PanY, -80.0f, 80.0f) * LocationScale,
		FMath::Clamp(HeightZ, -40.0f, 40.0f) * LocationScale);
	Pose.RotationOffset = FRotator(
		FMath::Clamp(Pitch + AdditionalPitch, -35.0f, 35.0f) * RotationScale,
		FMath::Clamp(Yaw + AdditionalYaw, -45.0f, 45.0f) * RotationScale,
		0.0f);

	FVector SocketWorldLocation = FVector::ZeroVector;
	if (!CameraComponent
		|| Group == EAttachmentCameraFocusGroup::None
		|| !ResolveAttachmentCameraFocusSocketWorldLocation(World, Group, SocketWorldLocation))
	{
		return Pose;
	}

	const USceneComponent* ParentComponent = CameraComponent->GetAttachParent();
	const FTransform ParentWorldTransform = ParentComponent
		? ParentComponent->GetComponentTransform()
		: FTransform::Identity;
	const FTransform BaseRelativeTransform(MainMenuCameraDriftBaseRelativeRotation, MainMenuCameraDriftBaseRelativeLocation);
	const FTransform DesiredRelativeTransform(MainMenuCameraDriftBaseRelativeRotation + Pose.RotationOffset, MainMenuCameraDriftBaseRelativeLocation);
	const FTransform BaseWorldTransform = BaseRelativeTransform * ParentWorldTransform;
	const FTransform DesiredWorldTransform = DesiredRelativeTransform * ParentWorldTransform;

	const FVector DesiredForward = DesiredWorldTransform.GetRotation().GetForwardVector().GetSafeNormal();
	const FVector DesiredRight = DesiredWorldTransform.GetUnitAxis(EAxis::Y).GetSafeNormal();
	const FVector DesiredUp = DesiredWorldTransform.GetUnitAxis(EAxis::Z).GetSafeNormal();
	const float SocketDepth = FVector::DotProduct(SocketWorldLocation - BaseWorldTransform.GetLocation(), DesiredForward);
	if (!FMath::IsFinite(SocketDepth) || SocketDepth <= 10.0f)
	{
		return Pose;
	}

	const APlayerCameraManager* CameraManager = ResolvePlayerCameraManager(World);
	const float HorizontalFOV = FMath::Clamp(
		CameraManager ? CameraManager->GetFOVAngle() : CameraComponent->FieldOfView,
		5.0f,
		170.0f);
	const float AspectRatio = FMath::Clamp(ResolveViewportAspectRatio(World), 0.3f, 4.0f);
	const float TargetDepth = FMath::Clamp(SocketDepth * RequestedDepthScale, 25.0f, SocketDepth);
	const float HalfHorizontalSize = FMath::Tan(FMath::DegreesToRadians(HorizontalFOV * 0.5f)) * TargetDepth;
	const float HalfVerticalSize = HalfHorizontalSize / AspectRatio;
	const float TargetScreenX = FMath::Clamp(RequestedTargetScreenX, 0.1f, 0.9f);
	const float TargetScreenY = FMath::Clamp(RequestedTargetScreenY, 0.1f, 0.9f);
	const float DesiredPlaneY = (TargetScreenX - 0.5f) * 2.0f * HalfHorizontalSize;
	const float DesiredPlaneZ = (0.5f - TargetScreenY) * 2.0f * HalfVerticalSize;
	const FVector TargetWorldLocation = SocketWorldLocation
		- DesiredForward * TargetDepth
		- DesiredRight * DesiredPlaneY
		- DesiredUp * DesiredPlaneZ;
	const FVector TargetRelativeLocation = ParentComponent
		? ParentWorldTransform.InverseTransformPosition(TargetWorldLocation)
		: TargetWorldLocation;
	Pose.LocationOffset = (TargetRelativeLocation - MainMenuCameraDriftBaseRelativeLocation) * LocationScale;
	UE_LOG(
		LogTMMenuViewerMeshTransition,
		Display,
		TEXT("[TMAttachmentsCameraFocus] Socket=%s Screen=%.2f,%.2f Depth=%.1f TargetDepth=%.1f Aspect=%.2f Loc=%s Rot=%s"),
		*DescribeAttachmentCameraFocusGroup(Group),
		TargetScreenX,
		TargetScreenY,
		SocketDepth,
		TargetDepth,
		AspectRatio,
		*Pose.LocationOffset.ToString(),
		*Pose.RotationOffset.ToString());
	return Pose;
}

bool UTMMenuViewerMeshTransitionSubsystem::ResolveAttachmentCameraFocusSocketWorldLocation(
	UWorld* World,
	const EAttachmentCameraFocusGroup Group,
	FVector& OutLocation) const
{
	if (!World || Group == EAttachmentCameraFocusGroup::None)
	{
		return false;
	}

	const UCameraComponent* CameraComponent = ResolveActiveCameraComponent(World);
	const FVector CameraLocation = CameraComponent ? CameraComponent->GetComponentLocation() : FVector::ZeroVector;
	AActor* WeaponActor = ResolveAttachmentsPreviewWeaponActor(World, CameraLocation);
	if (!IsValid(WeaponActor))
	{
		return false;
	}

	TArray<FName, TInlineAllocator<6>> CandidateSockets;
	switch (Group)
	{
	case EAttachmentCameraFocusGroup::Optics:
		CandidateSockets.Append({ TEXT("Optics"), TEXT("RearSight"), TEXT("AimTarget"), TEXT("ADS_Eye") });
		break;
	case EAttachmentCameraFocusGroup::SideRail:
		CandidateSockets.Append({ TEXT("AT_Backup"), TEXT("SideRail"), TEXT("Canted"), TEXT("Backup") });
		break;
	case EAttachmentCameraFocusGroup::Underbarrel:
		CandidateSockets.Append({ TEXT("Underbarrel"), TEXT("Foregrip") });
		break;
	case EAttachmentCameraFocusGroup::Muzzle:
		CandidateSockets.Append({ TEXT("Muzzle"), TEXT("MuzzleSilencerco") });
		break;
	default:
		break;
	}

	TInlineComponentArray<USceneComponent*> SceneComponents(WeaponActor);
	for (const FName CandidateSocket : CandidateSockets)
	{
		for (USceneComponent* SceneComponent : SceneComponents)
		{
			if (!IsValid(SceneComponent) || !SceneComponent->IsRegistered())
			{
				continue;
			}

			if (SceneComponent->DoesSocketExist(CandidateSocket))
			{
				OutLocation = SceneComponent->GetSocketLocation(CandidateSocket);
				return true;
			}
		}
	}

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(WeaponActor);
	for (const FName CandidateSocket : CandidateSockets)
	{
		for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			if (!IsValid(PrimitiveComponent) || !PrimitiveComponent->IsVisible())
			{
				continue;
			}

			const FString AttachSocketName = PrimitiveComponent->GetAttachSocketName().ToString();
			if (AttachSocketName.Equals(CandidateSocket.ToString(), ESearchCase::IgnoreCase)
				|| AttachSocketName.Contains(CandidateSocket.ToString(), ESearchCase::IgnoreCase))
			{
				OutLocation = PrimitiveComponent->Bounds.Origin;
				return true;
			}
		}
	}

	return false;
}

UTMMenuViewerMeshTransitionSubsystem::EAttachmentCameraFocusGroup
UTMMenuViewerMeshTransitionSubsystem::InferAttachmentCameraFocusGroupFromText(const FString& Text)
{
	FString Normalized = Text.TrimStartAndEnd();
	Normalized.ReplaceInline(TEXT(" "), TEXT(""));
	Normalized.ReplaceInline(TEXT("_"), TEXT(""));
	Normalized.ReplaceInline(TEXT("-"), TEXT(""));

	if (Normalized.IsEmpty() || Normalized.Equals(TEXT("Empty"), ESearchCase::IgnoreCase))
	{
		return EAttachmentCameraFocusGroup::None;
	}

	if (Normalized.Contains(TEXT("Silencer"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("Compensator"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("Muzzle"), ESearchCase::IgnoreCase))
	{
		return EAttachmentCameraFocusGroup::Muzzle;
	}

	if (Normalized.Contains(TEXT("Foregrip"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("VGrip"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("Underbarrel"), ESearchCase::IgnoreCase))
	{
		return EAttachmentCameraFocusGroup::Underbarrel;
	}

	if (Normalized.Contains(TEXT("Laser"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("SideRail"), ESearchCase::IgnoreCase))
	{
		return EAttachmentCameraFocusGroup::SideRail;
	}

	if (Normalized.Contains(TEXT("IronSight"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("MiniSight"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("RDS"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("Canted"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("Scope"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("Holo"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("Acog"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("AimPoint"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("Specter"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("Blaze"), ESearchCase::IgnoreCase)
		|| Normalized.Contains(TEXT("Optic"), ESearchCase::IgnoreCase))
	{
		return EAttachmentCameraFocusGroup::Optics;
	}

	return EAttachmentCameraFocusGroup::None;
}

FString UTMMenuViewerMeshTransitionSubsystem::DescribeAttachmentCameraFocusGroup(const EAttachmentCameraFocusGroup Group)
{
	switch (Group)
	{
	case EAttachmentCameraFocusGroup::Optics:
		return TEXT("Optics");
	case EAttachmentCameraFocusGroup::SideRail:
		return TEXT("SideRail");
	case EAttachmentCameraFocusGroup::Underbarrel:
		return TEXT("Underbarrel");
	case EAttachmentCameraFocusGroup::Muzzle:
		return TEXT("Muzzle");
	default:
		return TEXT("None");
	}
}

void UTMMenuViewerMeshTransitionSubsystem::RestoreMainMenuCameraDrift()
{
	if (!bMainMenuCameraDriftApplied)
	{
		return;
	}

	if (UCameraComponent* CameraComponent = MainMenuCameraDriftCamera.Get())
	{
		CameraComponent->SetRelativeLocationAndRotation(
			MainMenuCameraDriftBaseRelativeLocation,
			MainMenuCameraDriftBaseRelativeRotation);
		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Display,
			TEXT("[TMMenuCameraDrift] Restored base camera transform on %s."),
			*GetNameSafe(CameraComponent));
	}

	MainMenuCameraDriftCamera.Reset();
	MainMenuCameraDriftBaseRelativeLocation = FVector::ZeroVector;
	MainMenuCameraDriftLastRelativeLocationOffset = FVector::ZeroVector;
	MainMenuCameraDriftBaseRelativeRotation = FRotator::ZeroRotator;
	MainMenuCameraDriftLastRelativeRotationOffset = FRotator::ZeroRotator;
	MainMenuCameraDriftElapsedSeconds = 0.0f;
	ResetAttachmentsCameraFocus();
	bMainMenuCameraDriftApplied = false;
	bMainMenuCameraDriftLoadoutMode = false;
}

UTMMenuViewerMeshTransitionSubsystem::FLoadoutPostProcessFocus
UTMMenuViewerMeshTransitionSubsystem::ResolveLoadoutPostProcessFocus(
	UWorld* World,
	const UCameraComponent* CameraComponent) const
{
	FLoadoutPostProcessFocus Focus;
	if (!World || !CameraComponent)
	{
		return Focus;
	}

	const FVector CameraLocation = CameraComponent->GetComponentLocation();
	const FVector CameraForward = CameraComponent->GetForwardVector().GetSafeNormal();
	if (AActor* PreviewWeaponActor = ResolveLoadoutPreviewWeaponActor(World, CameraLocation))
	{
		float MinWeaponDepth = TNumericLimits<float>::Max();
		float MaxWeaponDepth = -TNumericLimits<float>::Max();

		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(PreviewWeaponActor);
		for (const UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			if (!IsValid(PrimitiveComponent) || !PrimitiveComponent->IsVisible())
			{
				continue;
			}

			const FBoxSphereBounds& Bounds = PrimitiveComponent->Bounds;
			const float CenterDepth = FVector::DotProduct(Bounds.Origin - CameraLocation, CameraForward);
			const FVector BoxExtent = Bounds.BoxExtent;
			const float DepthRadius =
				FMath::Abs(CameraForward.X) * BoxExtent.X
				+ FMath::Abs(CameraForward.Y) * BoxExtent.Y
				+ FMath::Abs(CameraForward.Z) * BoxExtent.Z;
			const float ComponentMinDepth = FMath::Max(10.0f, CenterDepth - DepthRadius);
			const float ComponentMaxDepth = CenterDepth + DepthRadius;
			if (!FMath::IsFinite(ComponentMinDepth)
				|| !FMath::IsFinite(ComponentMaxDepth)
				|| ComponentMaxDepth <= 10.0f)
			{
				continue;
			}

			MinWeaponDepth = FMath::Min(MinWeaponDepth, ComponentMinDepth);
			MaxWeaponDepth = FMath::Max(MaxWeaponDepth, ComponentMaxDepth);
		}

		if (FMath::IsFinite(MinWeaponDepth)
			&& FMath::IsFinite(MaxWeaponDepth)
			&& MinWeaponDepth < MaxWeaponDepth)
		{
			const float WeaponDepthSpan = MaxWeaponDepth - MinWeaponDepth;
			Focus.FocalDistance = FMath::Clamp((MinWeaponDepth + MaxWeaponDepth) * 0.5f, 40.0f, 2000.0f);
			Focus.FocalRegion = FMath::Clamp(WeaponDepthSpan + 28.0f, 55.0f, 150.0f);
			return Focus;
		}

		const float PreviewWeaponDepth = FVector::DotProduct(
			PreviewWeaponActor->GetActorLocation() - CameraLocation,
			CameraForward);
		if (FMath::IsFinite(PreviewWeaponDepth) && PreviewWeaponDepth > 10.0f)
		{
			Focus.FocalDistance = FMath::Clamp(PreviewWeaponDepth, 40.0f, 2000.0f);
			Focus.FocalRegion = 70.0f;
			return Focus;
		}
	}

	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsMenuViewerActor(Actor))
		{
			continue;
		}

		FVector FocusLocation = Actor->GetActorLocation();
		if (const USkeletalMeshComponent* CharacterComponent = ReadMeshComponentProperty(Actor, TEXT("Character")))
		{
			FocusLocation = CharacterComponent->Bounds.Origin;
		}

		const float Distance = FVector::Dist(CameraLocation, FocusLocation);
		if (FMath::IsFinite(Distance) && Distance > 10.0f && Distance < BestDistance)
		{
			BestDistance = Distance;
		}
	}

	if (!FMath::IsFinite(BestDistance) || BestDistance == TNumericLimits<float>::Max())
	{
		return Focus;
	}

	Focus.FocalDistance = FMath::Clamp(BestDistance, 80.0f, 2000.0f);
	return Focus;
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateMenuFOV(
	UWorld* World,
	const bool bMainMenuVisible,
	const bool bLoadoutVisible)
{
	if (!bMainMenuVisible)
	{
		RestoreMenuFOV();
		return;
	}

	APlayerCameraManager* PlayerCameraManager = ResolvePlayerCameraManager(World);
	if (!PlayerCameraManager)
	{
		return;
	}

	if (bMenuFOVApplied && MenuFOVCameraManager.IsValid() && MenuFOVCameraManager.Get() != PlayerCameraManager)
	{
		RestoreMenuFOV();
	}

	const float TargetFOV = GetMenuFOVAngle();
	if (!bMenuFOVApplied)
	{
		SavedMenuPreviousDefaultFOV = PlayerCameraManager->DefaultFOV;
		if (!FMath::IsFinite(SavedMenuPreviousDefaultFOV))
		{
			SavedMenuPreviousDefaultFOV = 90.0f;
		}

		MenuFOVCameraManager = PlayerCameraManager;
		bMenuFOVApplied = true;
		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Display,
			TEXT("[TMMenuFOV] Applied menu default FOV %.1f; saved previous default FOV %.1f."),
			TargetFOV,
			SavedMenuPreviousDefaultFOV);
	}

	if (!FMath::IsNearlyEqual(PlayerCameraManager->DefaultFOV, TargetFOV, 0.05f))
	{
		PlayerCameraManager->DefaultFOV = TargetFOV;
	}

	if (!bLoadoutVisible && !FMath::IsNearlyEqual(PlayerCameraManager->GetFOVAngle(), TargetFOV, 0.05f))
	{
		PlayerCameraManager->SetFOV(TargetFOV);
	}
}

void UTMMenuViewerMeshTransitionSubsystem::RestoreMenuFOV()
{
	if (!bMenuFOVApplied)
	{
		return;
	}

	if (APlayerCameraManager* PlayerCameraManager = MenuFOVCameraManager.Get())
	{
		PlayerCameraManager->DefaultFOV = FMath::Clamp(SavedMenuPreviousDefaultFOV, 5.0f, 170.0f);
		if (!bLoadoutFOVApplied)
		{
			PlayerCameraManager->UnlockFOV();
		}
		UE_LOG(
			LogTMMenuViewerMeshTransition,
			Display,
			TEXT("[TMMenuFOV] Restored previous default FOV %.1f after leaving main menu."),
			PlayerCameraManager->DefaultFOV);
	}

	MenuFOVCameraManager.Reset();
	bMenuFOVApplied = false;
}

USkeletalMesh* UTMMenuViewerMeshTransitionSubsystem::ChooseNextMenuMesh(USkeletalMesh* CurrentMesh) const
{
	static const TCHAR* MeshPaths[] =
	{
		TEXT("/Game/Test/MPVS_SkeletonProbe/Secondary/SKM_Urban_Soldier_01_MPSBones.SKM_Urban_Soldier_01_MPSBones"),
		TEXT("/Game/Test/MPVS_SkeletonProbe/Secondary/SKM_Urban_Soldier_02_MPSBones.SKM_Urban_Soldier_02_MPSBones"),
		TEXT("/Game/Test/MPVS_SkeletonProbe/Secondary/SKM_Urban_Soldier_05_MPSBones.SKM_Urban_Soldier_05_MPSBones"),
		TEXT("/Game/Test/MPVS_SkeletonProbe/Secondary/SKM_Urban_Soldier_06_MPSBones.SKM_Urban_Soldier_06_MPSBones"),
	};

	TArray<USkeletalMesh*> CandidateMeshes;
	for (const TCHAR* MeshPath : MeshPaths)
	{
		if (USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, MeshPath))
		{
			CandidateMeshes.Add(Mesh);
		}
	}

	if (CandidateMeshes.Num() <= 1)
	{
		return nullptr;
	}

	int32 CurrentIndex = CandidateMeshes.IndexOfByKey(CurrentMesh);
	if (CurrentIndex == INDEX_NONE)
	{
		return CandidateMeshes[0];
	}

	return CandidateMeshes[(CurrentIndex + 1) % CandidateMeshes.Num()];
}

UTMMenuViewerMeshTransitionSubsystem::FBeatSyncSnapshot UTMMenuViewerMeshTransitionSubsystem::FindActiveBeatSync(
	UWorld* World) const
{
	FBeatSyncSnapshot Snapshot;
	if (!World)
	{
		return Snapshot;
	}

	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!Widget || Widget->HasAnyFlags(RF_ClassDefaultObject))
		{
			continue;
		}

		const UClass* WidgetClass = Widget->GetClass();
		if (!WidgetClass || !WidgetClass->GetPathName().Contains(TEXT("W_MainMenu")))
		{
			continue;
		}

		if (const UWorld* WidgetWorld = Widget->GetWorld())
		{
			if (WidgetWorld != World)
			{
				continue;
			}
		}

		const FObjectPropertyBase* EnvelopeFollowerProperty =
			FindFProperty<FObjectPropertyBase>(WidgetClass, TEXT("EnvelopeFollower"));
		if (!EnvelopeFollowerProperty)
		{
			continue;
		}

		const UTMAudioEnvelopeFollower* Follower = Cast<UTMAudioEnvelopeFollower>(
			EnvelopeFollowerProperty->GetObjectPropertyValue_InContainer(Widget));
		Snapshot = MakeBeatSyncSnapshot(Follower);
		if (Snapshot.bHasActiveFollower)
		{
			return Snapshot;
		}
	}

	for (TObjectIterator<UTMAudioEnvelopeFollower> It; It; ++It)
	{
		const UTMAudioEnvelopeFollower* Follower = *It;
		if (!Follower)
		{
			continue;
		}

		if (const UWorld* FollowerWorld = Follower->GetWorld())
		{
			if (FollowerWorld != World)
			{
				continue;
			}
		}

		Snapshot = MakeBeatSyncSnapshot(Follower);
		if (Snapshot.bHasActiveFollower)
		{
			return Snapshot;
		}
	}

	return Snapshot;
}

void UTMMenuViewerMeshTransitionSubsystem::UpdateRhythmPulseMemory(
	FMenuViewerState& State,
	const FBeatSyncSnapshot& BeatSync) const
{
	if (!BeatSync.bHasActiveFollower)
	{
		State.bRhythmPulseWasHigh = false;
		return;
	}

	const float PulseThreshold = GetBeatSyncPulseThreshold();
	if (BeatSync.NormalizedEnvelopeValue <= PulseThreshold * 0.5f)
	{
		State.bRhythmPulseWasHigh = false;
	}
	else if (BeatSync.NormalizedEnvelopeValue >= PulseThreshold)
	{
		State.bRhythmPulseWasHigh = true;
	}
}

bool UTMMenuViewerMeshTransitionSubsystem::IsReadyForBeatSyncedCycle(
	FMenuViewerState& State,
	const FBeatSyncSnapshot& BeatSync,
	const float WorldTimeSeconds) const
{
	if (!BeatSync.bHasActiveFollower)
	{
		return true;
	}

	const float PulseThreshold = GetBeatSyncPulseThreshold();
	const bool bPulseHigh = BeatSync.NormalizedEnvelopeValue >= PulseThreshold;
	const bool bNewRhythmPulse = bPulseHigh && !State.bRhythmPulseWasHigh;
	UpdateRhythmPulseMemory(State, BeatSync);

	if (BeatSync.BeatIndex <= 0)
	{
		return bNewRhythmPulse;
	}

	const bool bNewBeat = BeatSync.BeatIndex > State.LastObservedBeatIndex;
	if (bNewBeat)
	{
		State.LastObservedBeatIndex = BeatSync.BeatIndex;
	}

	const bool bBeatInWindow = WorldTimeSeconds - BeatSync.LastBeatTimeSeconds <= GetBeatSyncWindow();
	if (bNewBeat && bBeatInWindow && IsBeatSyncCycleBeat(BeatSync))
	{
		return true;
	}

	if (bNewRhythmPulse)
	{
		return true;
	}

	return false;
}
