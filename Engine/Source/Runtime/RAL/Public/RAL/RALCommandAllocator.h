#pragma once

#include "CoreMinimal.h"
#include "RALResource.h"

namespace LE
{

class RAL_API FRALCommandAllocator : public FRALResource
{
public:
    ~FRALCommandAllocator() override = default;

public:
    /** Reset only after every command list allocated from this allocator has completed. */
    virtual bool Reset() = 0;
};

} // namespace LE
