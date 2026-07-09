#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TMMenuViewerMeshTransitionSubsystem.generated.h"

class AActor;
class UPrimitiveComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UTMAudioEnvelopeFollower;

UCLASS()
class TOUCHME_API UTMMenuViewerMeshTransitionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
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

	void TrackMenuViewer(AActor* Actor, float DeltaTime);
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
	static bool IsLoadoutPreviewVisible(UWorld* World);
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

	USkeletalMesh* ChooseNextMenuMesh(USkeletalMesh* CurrentMesh) const;
	FBeatSyncSnapshot FindActiveBeatSync(UWorld* World) const;
	void UpdateRhythmPulseMemory(FMenuViewerState& State, const FBeatSyncSnapshot& BeatSync) const;
	bool IsReadyForBeatSyncedCycle(FMenuViewerState& State, const FBeatSyncSnapshot& BeatSync, float WorldTimeSeconds) const;

	TMap<TWeakObjectPtr<AActor>, FMenuViewerState> MenuViewerStates;
};
