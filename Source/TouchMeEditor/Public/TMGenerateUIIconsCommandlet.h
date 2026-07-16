#pragma once

#include "Commandlets/Commandlet.h"
#include "TMGenerateUIIconsCommandlet.generated.h"

UCLASS()
class TOUCHMEEDITOR_API UTMGenerateUIIconsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
