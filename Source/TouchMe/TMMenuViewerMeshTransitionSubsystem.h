#pragma once

#include "CoreMinimal.h"
#include "Engine/Scene.h"
#include "Subsystems/WorldSubsystem.h"
#include "TMMenuViewerMeshTransitionSubsystem.generated.h"

class AActor;
class APlayerCameraManager;
class UCameraComponent;
class ULightComponent;
class UMaterialInterface;
class UParticleSystemComponent;
class UPrimitiveComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UTMAudioEnvelopeFollower;

UCLASS()
class TOUCHME_API UTMMenuViewerMeshTransitionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FComponentVisibilityState
	{
		bool bVisible = true;
		bool bHiddenInGame = false;
	};

	struct FMenuViewerState
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<USkeletalMeshComponent> VestComponent;
		TWeakObjectPtr<USkeletalMesh> LastMesh;
		TMap<TWeakObjectPtr<AActor>, bool> AttachedWeaponActorHiddenStates;
		TMap<TWeakObjectPtr<UPrimitiveComponent>, FComponentVisibilityState> AttachedWeaponComponentVisibilityStates;
		float AutoCycleElapsedSeconds = 0.f;
		int32 LastObservedBeatIndex = 0;
		bool bRhythmPulseWasHigh = false;
	};

	struct FBeatSyncSnapshot
	{
		bool bHasActiveFollower = false;
		int32 BeatIndex = 0;
		int32 BeatsPerBar = 4;
		float LastBeatTimeSeconds = -1.0f;
		float LastBeatStrength = 0.0f;
		float NormalizedEnvelopeValue = 0.0f;
	};

	struct FLoadoutPostProcessFocus
	{
		float FocalDistance = 360.0f;
		float FocalRegion = 95.0f;
	};

	struct FBackGlowVisualState
	{
		FVector OriginalLocation = FVector::ZeroVector;
		FRotator OriginalRotation = FRotator::ZeroRotator;
		FVector OriginalScale = FVector::OneVector;
		TArray<TWeakObjectPtr<UMaterialInterface>> OriginalMaterials;
		bool bVisible = true;
		bool bHiddenInGame = false;
	};

	struct FBackGlowLightState
	{
		float OriginalIntensity = 0.0f;
		FLinearColor OriginalColor = FLinearColor::White;
		float OriginalAttenuationRadius = 0.0f;
		float OriginalSourceWidth = 0.0f;
		float OriginalSourceHeight = 0.0f;
		bool bHasAttenuationRadius = false;
		bool bHasRectSourceSize = false;
	};

	enum class EAttachmentCameraFocusGroup : uint8
	{
		None,
		Optics,
		SideRail,
		Underbarrel,
		Muzzle
	};

	struct FAttachmentCameraFocusPose
	{
		FVector LocationOffset = FVector::ZeroVector;
		FRotator RotationOffset = FRotator::ZeroRotator;
	};

	void TrackMenuViewer(AActor* Actor, float DeltaTime, bool bLoadoutPreviewVisible);
	void NoteMeshChanged(const FMenuViewerState& State, USkeletalMeshComponent* VestComponent, USkeletalMesh* PreviousMesh) const;
	void CaptureStableState(FMenuViewerState& State, USkeletalMeshComponent* VestComponent);
	void UpdateAutoCycle(
		FMenuViewerState& State,
		USkeletalMeshComponent* VestComponent,
		USkeletalMeshComponent* LeaderComponent,
		float DeltaTime,
		const FBeatSyncSnapshot& BeatSync);

	static bool IsMenuViewerActor(const AActor* Actor);
	static bool IsAttachedWeaponActor(const AActor* Actor);
	static bool IsLoadoutFOVVisible(UWorld* World);
	static bool IsAttachmentsVisible(UWorld* World);
	static bool IsMainMenuVisible(UWorld* World);
	static bool IsLoadoutPreviewVisible(UWorld* World);
	static bool IsVisibleLoadoutPreviewWeaponActor(AActor* Actor);
	static bool IsLoadoutBackGlowLight(const ULightComponent* LightComponent);
	static bool IsLoadoutBackGlowVisual(const UPrimitiveComponent* PrimitiveComponent);
	static APlayerCameraManager* ResolvePlayerCameraManager(UWorld* World);
	static UCameraComponent* ResolveActiveCameraComponent(UWorld* World);
	static void UpdateAttachedWeaponVisibility(AActor* Actor, FMenuViewerState& State, bool bHide);
	static void HideAttachedWeaponActors(AActor* Actor, FMenuViewerState& State);
	static void RestoreAttachedWeaponActors(FMenuViewerState& State);
	static USkeletalMeshComponent* ReadMeshComponentProperty(UObject* Object, FName PropertyName);
	static USkeletalMeshComponent* ResolveLeaderComponent(AActor* Actor, USkeletalMeshComponent* VestComponent);
	static bool IsAutoCycleEnabled();
	static bool IsBeatSyncEnabled();
	static bool ShouldUseDownbeat();
	static float GetAutoCycleInterval();
	static float GetBeatSyncWindow();
	static float GetBeatSyncPulseThreshold();
	static FBeatSyncSnapshot MakeBeatSyncSnapshot(const UTMAudioEnvelopeFollower* Follower);
	static EAttachmentCameraFocusGroup InferAttachmentCameraFocusGroupFromText(const FString& Text);
	static FString DescribeAttachmentCameraFocusGroup(EAttachmentCameraFocusGroup Group);

	USkeletalMesh* ChooseNextMenuMesh(USkeletalMesh* CurrentMesh) const;
	FBeatSyncSnapshot FindActiveBeatSync(UWorld* World) const;
	void UpdateRhythmPulseMemory(FMenuViewerState& State, const FBeatSyncSnapshot& BeatSync) const;
	bool IsReadyForBeatSyncedCycle(FMenuViewerState& State, const FBeatSyncSnapshot& BeatSync, float WorldTimeSeconds) const;
	void UpdateMenuFOV(UWorld* World, bool bMainMenuVisible, bool bLoadoutVisible);
	void UpdateLoadoutFOV(UWorld* World, bool bLoadoutVisible);
	void UpdateLoadoutPostProcess(UWorld* World, bool bPostProcessVisible, bool bLoadoutVisible, bool bAttachmentsVisible);
	void ApplyLoadoutPostProcess(UWorld* World, UCameraComponent* CameraComponent, bool bLoadoutVisible, bool bAttachmentsVisible);
	void RestoreLoadoutPostProcess();
	void UpdateAttachmentsPreviewBrightnessTiming(bool bAttachmentsVisible, bool bLoadoutVisible, float DeltaTime);
	void UpdateLoadoutBackGlowTiming(bool bLoadoutVisible, float DeltaTime);
	void UpdateLoadoutBackGlow(UWorld* World, bool bLoadoutVisible, float DeltaTime);
	void RestoreLoadoutBackGlow();
	void UpdateMainMenuBackGlow(UWorld* World, bool bMainMenuGlowVisible, float DeltaTime);
	void RestoreMainMenuBackGlow();
	void UpdateMainMenuCameraDrift(UWorld* World, bool bMenuDriftVisible, bool bLoadoutMode, bool bAttachmentsVisible, float DeltaTime);
	void RestoreMainMenuCameraDrift();
	void UpdateAttachmentsCameraFocus(UWorld* World, bool bAttachmentsVisible, float DeltaTime, UCameraComponent* CameraComponent);
	void ResetAttachmentsCameraFocus();
	AActor* ResolveLoadoutPreviewWeaponActor(UWorld* World, const FVector& CameraLocation) const;
	AActor* ResolveAttachmentsPreviewWeaponActor(UWorld* World, const FVector& CameraLocation) const;
	FLoadoutPostProcessFocus ResolveLoadoutPostProcessFocus(UWorld* World, const UCameraComponent* CameraComponent) const;
	EAttachmentCameraFocusGroup ResolveActiveAttachmentCameraFocusGroup(UWorld* World) const;
	FAttachmentCameraFocusPose ResolveAttachmentCameraFocusPose(UWorld* World, const UCameraComponent* CameraComponent, EAttachmentCameraFocusGroup Group) const;
	bool ResolveAttachmentCameraFocusSocketWorldLocation(UWorld* World, EAttachmentCameraFocusGroup Group, FVector& OutLocation) const;
	void RestoreLoadoutFOV();
	void RestoreMenuFOV();

	TMap<TWeakObjectPtr<AActor>, FMenuViewerState> MenuViewerStates;
	TWeakObjectPtr<APlayerCameraManager> MenuFOVCameraManager;
	TWeakObjectPtr<APlayerCameraManager> LoadoutFOVCameraManager;
	TWeakObjectPtr<UCameraComponent> LoadoutPostProcessCamera;
	TWeakObjectPtr<UParticleSystemComponent> LoadoutBackGlowSpawnedVisual;
	TMap<TWeakObjectPtr<ULightComponent>, FBackGlowLightState> LoadoutBackGlowLightStates;
	TMap<TWeakObjectPtr<UPrimitiveComponent>, FBackGlowVisualState> LoadoutBackGlowVisualStates;
	TMap<TWeakObjectPtr<ULightComponent>, FBackGlowLightState> MainMenuBackGlowLightStates;
	TMap<TWeakObjectPtr<UPrimitiveComponent>, FBackGlowVisualState> MainMenuBackGlowVisualStates;
	TWeakObjectPtr<UCameraComponent> MainMenuCameraDriftCamera;
	FPostProcessSettings SavedLoadoutPostProcessSettings;
	FVector MainMenuCameraDriftBaseRelativeLocation = FVector::ZeroVector;
	FVector MainMenuCameraDriftLastRelativeLocationOffset = FVector::ZeroVector;
	FRotator MainMenuCameraDriftBaseRelativeRotation = FRotator::ZeroRotator;
	FRotator MainMenuCameraDriftLastRelativeRotationOffset = FRotator::ZeroRotator;
	FVector AttachmentsCameraFocusCurrentLocationOffset = FVector::ZeroVector;
	FVector AttachmentsCameraFocusStartLocationOffset = FVector::ZeroVector;
	FVector AttachmentsCameraFocusTargetLocationOffset = FVector::ZeroVector;
	FRotator AttachmentsCameraFocusCurrentRotationOffset = FRotator::ZeroRotator;
	FRotator AttachmentsCameraFocusStartRotationOffset = FRotator::ZeroRotator;
	FRotator AttachmentsCameraFocusTargetRotationOffset = FRotator::ZeroRotator;
	float SavedMenuPreviousDefaultFOV = 90.0f;
	float SavedLoadoutPreviousFOV = 90.0f;
	float SavedLoadoutPostProcessBlendWeight = 0.0f;
	float LoadoutBackGlowElapsedSeconds = 0.0f;
	float LoadoutBackGlowCurrentAlpha = 0.0f;
	float AttachmentsPreviewBrightnessElapsedSeconds = 0.0f;
	float AttachmentsPreviewBrightnessTransitionStartAlpha = 0.0f;
	float AttachmentsPreviewBrightnessCurrentAlpha = 0.0f;
	float MainMenuBackGlowElapsedSeconds = 0.0f;
	float MainMenuBackGlowCurrentScale = 0.0f;
	float MainMenuCameraDriftElapsedSeconds = 0.0f;
	float AttachmentsCameraFocusDelayElapsedSeconds = 0.0f;
	float AttachmentsCameraFocusBlendElapsedSeconds = 0.0f;
	EAttachmentCameraFocusGroup AttachmentsCameraFocusObservedGroup = EAttachmentCameraFocusGroup::None;
	EAttachmentCameraFocusGroup AttachmentsCameraFocusTargetGroup = EAttachmentCameraFocusGroup::None;
	bool bMenuFOVApplied = false;
	bool bLoadoutFOVApplied = false;
	bool bLoadoutPostProcessApplied = false;
	bool bLoadoutPostProcessLastLoadoutMode = false;
	bool bAttachmentsPreviewBrightnessActive = false;
	bool bAttachmentsPreviewBrightnessTargetVisible = false;
	bool bLoadoutBackGlowTargetVisible = false;
	bool bLoadoutBackGlowActive = false;
	bool bMainMenuBackGlowActive = false;
	bool bMainMenuBackGlowForceComplete = false;
	bool bMainMenuCameraDriftApplied = false;
	bool bMainMenuCameraDriftLoadoutMode = false;
	bool bAttachmentsCameraFocusWaitingForDelay = false;
	bool bAttachmentsCameraFocusTransitionActive = false;
};
