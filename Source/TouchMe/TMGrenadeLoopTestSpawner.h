#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectKey.h"
#include "TMGrenadeLoopTestSpawner.generated.h"

class UActorComponent;

UCLASS(BlueprintType, Blueprintable)
class TOUCHME_API ATMGrenadeLoopTestSpawner : public AActor
{
	GENERATED_BODY()

public:
	ATMGrenadeLoopTestSpawner();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual bool ShouldTickIfViewportsOnly() const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Grenade Loop")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Grenade Loop")
	TSubclassOf<AActor> GrenadeClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Grenade Loop")
	FVector SpawnOffset = FVector(0.f, 0.f, 80.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Grenade Loop")
	FRotator SpawnRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Grenade Loop")
	float InitialDelay = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Grenade Loop")
	float RespawnDelay = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Grenade Loop")
	float CleanupStableSeconds = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Grenade Loop")
	bool bTreatHiddenActorsAsRemoved = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Grenade Loop")
	bool bTrackOnlyLikelyGrenadeObjects = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Grenade Loop")
	bool bDestroyTrackedObjectsOnEndPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Grenade Loop")
	bool bDrawDebug = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Grenade Loop")
	float DebugMarkerRadius = 22.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TM|Grenade Loop")
	int32 SpawnedGrenadeCount = 0;

private:
	void StartNextGrenade();
	void SnapshotExistingObjects();
	void CollectBornObjects();
	bool AreTrackedObjectsRemoved() const;
	void ClearCycleTracking();
	bool ShouldTrackBornActor(const AActor* Actor) const;
	bool ShouldTrackBornComponent(const UActorComponent* Component) const;
	bool IsTrackedActorRemoved(const TWeakObjectPtr<AActor>& Actor) const;
	bool IsTrackedComponentRemoved(const TWeakObjectPtr<UActorComponent>& Component) const;
	bool IsRuntimeWorld() const;

	TSet<TObjectKey<AActor>> BaselineActors;
	TSet<TObjectKey<AActor>> KnownBornActors;
	TArray<TWeakObjectPtr<AActor>> TrackedActors;

	TSet<TObjectKey<UActorComponent>> BaselineComponents;
	TSet<TObjectKey<UActorComponent>> KnownBornComponents;
	TArray<TWeakObjectPtr<UActorComponent>> TrackedComponents;

	TWeakObjectPtr<AActor> ActiveGrenade;
	float NextSpawnTimeSeconds = 0.f;
	float AllRemovedSinceSeconds = -1.f;
	bool bCycleActive = false;
};
