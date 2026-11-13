#pragma once

#include "CoreMinimal.h"
#include "RHIResource.h"

namespace LE
{

class RHI_API FRHICommandAllocator : public FRHIResource
{
public:
    ~FRHICommandAllocator() override = default;

public:
    /** Reset only after every command list allocated from this allocator has completed. */
    virtual bool Reset() = 0;
};

} // namespace LE
