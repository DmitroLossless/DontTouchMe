#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TMFoliageCollisionPushTester.generated.h"

class UPrimitiveComponent;
class USceneComponent;
class USkeletalMesh;
class UNiagaraSystem;
class USphereComponent;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS(BlueprintType, Blueprintable)
class TOUCHME_API ATMFoliageCollisionPushTester : public AActor
{
	GENERATED_BODY()

public:
	ATMFoliageCollisionPushTester();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Collision")
	float Radius = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Collision")
	float InitialRadiusDivisor = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Collision")
	float ExpansionDuration = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Collision")
	bool bAutoDestroyAfterExpansion = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Collision")
	bool bLoopPulses = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Collision")
	float RespawnDelay = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Collision")
	bool bCreatePhysicsProxyBodies = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Collision")
	bool bUseSkeletalReplacements = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Collision")
	bool bRemoveOriginalInstances = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Collision")
	int32 MaxSkeletalReplacements = 24;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Collision")
	FName SkeletalSimulationRootBone = TEXT("foliage_01");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Collision|FX")
	bool bSpawnSkeletalReplacementImpactFX = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Collision|FX")
	TArray<TSoftObjectPtr<UNiagaraSystem>> SkeletalReplacementImpactFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Collision|FX")
	FVector SkeletalReplacementImpactFXScale = FVector(1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Collision")
	int32 MaxProxyBodies = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Collision")
	float ProxyBodyRadius = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Collision")
	float ProxyBodyMassKg = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Collision")
	float ProxyLinearDamping = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Collision")
	float ProxyAngularDamping = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Collision")
	bool bShowProxyMeshes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Collision")
	bool bDrawDebug = true;

	UFUNCTION(BlueprintCallable, Category = "TM|Foliage Collision")
	void RebuildProxyBodies();

private:
	void ConfigurePusherSphere();
	void ConfigureVegetationPhysicsBody(UPrimitiveComponent* Component) const;
	void CreateSkeletalReplacements();
	bool CreateSkeletalReplacement(const FTransform& WorldTransform, const UStaticMesh* SourceStaticMesh);
	void SpawnRandomSkeletalReplacementImpactFX(const FTransform& WorldTransform);
	void CreateProxyBody(const FVector& Location);
	void DestroyProxyBodies();
	void CollectProxyBodiesForComponent(UPrimitiveComponent* Component, const FVector& Origin);
	USkeletalMesh* FindSkeletalReplacementMesh(const UStaticMesh* StaticMesh) const;
	float GetCurrentSphereRadius() const;

	UPROPERTY(VisibleAnywhere, Category = "TM|Foliage Collision")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "TM|Foliage Collision")
	TObjectPtr<USphereComponent> PushSphere;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> ProxySphereMesh;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> ProxyBodies;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> SkeletalReplacementActors;

	float ElapsedSeconds = 0.f;
	float RespawnCooldownSeconds = 0.f;
	bool bPulseActive = true;
};
