#pragma once

#include "CoreMinimal.h"
#include "RHIResource.h"
#include "RHIDescription.h"

namespace LE
{

class RHI_API FRHISampler : public FRHIResource
{
public:
    ~FRHISampler() override = default;

public:
    virtual const FRHISamplerDesc& GetDesc() const = 0;
};

} // namespace LE
