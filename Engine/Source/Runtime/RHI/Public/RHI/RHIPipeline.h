#pragma once

#include "CoreMinimal.h"
#include "RHIResource.h"
#include "RHIDescription.h"

namespace LE
{

class RHI_API FRHIPipeline_Graphics : public FRHIResource
{
public:
    virtual ~FRHIPipeline_Graphics() = default;

public:
    virtual const FRHIPipelineDesc_Graphics& GetDesc() const = 0;
};

} // namespace LE
