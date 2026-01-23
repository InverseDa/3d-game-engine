#pragma once

#include "CoreMinimal.h"
#include "RALResource.h"
#include "RALDescription.h"

class RAL_API FRALGraphicsPipeline : public FRALResource
{
public:
    virtual ~FRALGraphicsPipeline() = default;

public:
    virtual const FRALGraphicsPipelineDesc& GetDesc() const = 0;
};