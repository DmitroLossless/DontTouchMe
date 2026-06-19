#pragma once

#include "Commandlets/Commandlet.h"

#include "TMAnimGraphPatchCommandlet.generated.h"

UCLASS()
class UTMAnimGraphPatchCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
