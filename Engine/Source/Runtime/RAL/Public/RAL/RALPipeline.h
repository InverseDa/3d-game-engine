#pragma once

#include "CoreMinimal.h"
#include "RALResource.h"
#include "RALDescription.h"

class RAL_API FRALPipeline_Graphics : public FRALResource
{
public:
    virtual ~FRALPipeline_Graphics() = default;

public:
    virtual const FRALPipelineDesc_Graphics& GetDesc() const = 0;
};