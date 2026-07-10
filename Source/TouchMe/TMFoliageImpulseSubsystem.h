#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TMFoliageImpulseSubsystem.generated.h"

class UMaterialParameterCollection;
class UInstancedStaticMeshComponent;
class UPrimitiveComponent;
class USphereComponent;

UCLASS()
class TOUCHME_API UTMFoliageImpulseSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	void AddDefaultRadialImpulse(const FVector& Origin);
	void AddRadialImpulse(
		const FVector& Origin,
		float Radius,
		float ImpulseStrength,
		float Duration);

private:
	struct FTrackedGrenadeActor
	{
		TWeakObjectPtr<AActor> Actor;
		FVector LastLocation = FVector::ZeroVector;
		FString DebugName;
		FString DebugClassPath;
		bool bWasHidden = false;
		bool bTriggered = false;
	};

	struct FRecentImpulse
	{
		FVector Origin = FVector::ZeroVector;
		float TimeSeconds = 0.f;
	};

	struct FActiveImpulseSphere
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<USphereComponent> SphereComponent;
		FVector Origin = FVector::ZeroVector;
		float Radius = 0.f;
		float ImpulseStrength = 0.f;
		float DurationSeconds = 1.f;
		float ElapsedSeconds = 0.f;
		TArray<TWeakObjectPtr<UPrimitiveComponent>> CandidateComponents;
		TSet<TWeakObjectPtr<UPrimitiveComponent>> ProcessedComponents;
		bool bStartedMaterialPulse = false;
	};

	struct FActiveMaterialPulse
	{
		FVector Origin = FVector::ZeroVector;
		FVector Direction = FVector::ForwardVector;
		float Strength = 0.f;
		float ElapsedSeconds = 0.f;
	};

	struct FActiveInstancePulse
	{
		TWeakObjectPtr<UInstancedStaticMeshComponent> Component;
		int32 InstanceIndex = INDEX_NONE;
		FVector2D Direction = FVector2D(1.f, 0.f);
		float Weight = 0.f;
		float ElapsedSeconds = 0.f;
	};

	void TickActiveImpulseSpheres(float DeltaTime);
	void TickActiveMaterialPulses(float DeltaTime);
	void TickActiveInstancePulses(float DeltaTime);
	void ScanForGrenadeActors(float DeltaTime);
	void UpdateTrackedGrenadeActors();
	void TriggerTrackedGrenadeImpulse(FTrackedGrenadeActor& State);
	void SpawnImpulseSphere(
		const FVector& Origin,
		float Radius,
		float ImpulseStrength,
		float Duration);
	void ProcessImpulseSphere(FActiveImpulseSphere& ImpulseSphere, float CurrentRadius);
	void DestroyImpulseSphere(FActiveImpulseSphere& ImpulseSphere) const;
	void StartMaterialPulse(const FVector& Origin, float ImpulseStrength);
	void AddInstancePulsesForComponent(UPrimitiveComponent* Component, const FVector& Origin, float Radius);
	void ApplyMaterialPulse(const FActiveMaterialPulse& Pulse, float SignedAmplitude);
	void ClearMaterialPulse();
	UMaterialParameterCollection* GetMWControllerCollection();
	bool IsDuplicateRecentImpulse(const FVector& Origin) const;
	void AddRecentImpulse(const FVector& Origin);

	static bool IsSubsystemEnabled();
	static bool IsAutoGrenadeTrackingEnabled();
	static bool IsGrenadeImpulseActor(const AActor* Actor);
	static bool ShouldAffectFoliageComponent(const UPrimitiveComponent* Component, const FVector& Origin, float Radius);
	static bool PrepareFoliageCollision(UPrimitiveComponent* Component);

	TMap<TWeakObjectPtr<AActor>, FTrackedGrenadeActor> TrackedGrenadeActors;
	TArray<FActiveImpulseSphere> ActiveImpulseSpheres;
	TArray<FActiveMaterialPulse> ActiveMaterialPulses;
	TArray<FActiveInstancePulse> ActiveInstancePulses;
	TArray<FRecentImpulse> RecentImpulses;
	TWeakObjectPtr<UMaterialParameterCollection> MWControllerCollection;
	float GrenadeScanAccumulatorSeconds = 0.f;
};
