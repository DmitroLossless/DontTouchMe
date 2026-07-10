#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TMFoliageExplosionCollisionTester.generated.h"

class UMaterialParameterCollection;
class UPrimitiveComponent;
class USphereComponent;

UCLASS(BlueprintType, Blueprintable)
class TOUCHME_API ATMFoliageExplosionCollisionTester : public AActor
{
	GENERATED_BODY()

public:
	ATMFoliageExplosionCollisionTester();

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnConstruction(const FTransform& Transform) override;

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
	bool bAutoDestroyAfterExpansion = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Debug")
	bool bExpandInEditor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Debug")
	FVector PullDirection = FVector::ForwardVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Debug")
	float MaterialSpeedScale = 0.012f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Debug")
	bool bDrawDebugSphere = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TM|Foliage Debug")
	FColor DebugColor = FColor::Cyan;

	UFUNCTION(BlueprintCallable, Category = "TM|Foliage Debug")
	void RefreshAffectedFoliage();

private:
	void ConfigureSphere();
	float GetCurrentSphereRadius() const;
	void HoldMaterialBend();
	void ClearMaterialBend();
	UMaterialParameterCollection* GetMWControllerCollection();

	static bool ShouldAffectFoliageComponent(const UPrimitiveComponent* Component, const FVector& Origin, float Radius);
	static bool PrepareFoliageCollision(UPrimitiveComponent* Component);

	UPROPERTY(VisibleAnywhere, Category = "TM|Foliage Debug")
	TObjectPtr<USphereComponent> SphereComponent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> MWControllerCollection;

	float ElapsedSeconds = 0.f;
};
