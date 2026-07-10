#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TMFoliageExplosionCollisionTesterLoop.generated.h"

class ATMFoliageExplosionCollisionTester;

UCLASS(BlueprintType, Blueprintable)
class TOUCHME_API ATMFoliageExplosionCollisionTesterLoop : public AActor
{
	GENERATED_BODY()

public:
	ATMFoliageExplosionCollisionTesterLoop();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual bool ShouldTickIfViewportsOnly() const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Debug")
	float Radius = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Debug")
	float Strength = 75000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Debug")
	float BendDistance = 240.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Debug")
	float InitialRadiusDivisor = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Debug")
	float ExpansionDuration = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Debug")
	float RespawnDelay = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Debug")
	float MaterialSpeedScale = 0.012f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Debug")
	FVector PullDirection = FVector::ForwardVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Debug")
	bool bRunInEditor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Debug")
	bool bDrawDebugSphere = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Debug")
	FColor DebugColor = FColor::Cyan;

	UFUNCTION(BlueprintCallable, Category = "TM|Foliage Debug")
	ATMFoliageExplosionCollisionTester* SpawnPulse();

private:
	bool CanRunLoop() const;

	UPROPERTY(Transient)
	TWeakObjectPtr<ATMFoliageExplosionCollisionTester> ActivePulse;

	float RespawnCooldownSeconds = 0.f;
};
