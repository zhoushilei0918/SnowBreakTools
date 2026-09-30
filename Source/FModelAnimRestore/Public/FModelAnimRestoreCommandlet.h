#pragma once
#include "Commandlets/Commandlet.h"
#include "FModelAnimRestoreCommandlet.generated.h"

UCLASS()
class UFModelAnimRestoreCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UFModelAnimRestoreCommandlet();
    virtual int32 Main(const FString& Params) override;
};
