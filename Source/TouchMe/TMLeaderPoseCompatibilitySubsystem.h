#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TMLeaderPoseCompatibilitySubsystem.generated.h"

class USkinnedMeshComponent;

UCLASS()
class TOUCHME_API UTMLeaderPoseCompatibilitySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void PatchUrbanFollowerComponent(USkinnedMeshComponent* FollowerComponent) const;
};
