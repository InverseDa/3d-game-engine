#pragma once

#include "CoreMinimal.h"
#include "RALResource.h"
#include "RALDescription.h"

class RAL_API FRALSampler : public FRALResource
{
public:
    ~FRALSampler() override = default;

public:
    virtual const FRALSamplerDesc& GetDesc() const = 0;
};
