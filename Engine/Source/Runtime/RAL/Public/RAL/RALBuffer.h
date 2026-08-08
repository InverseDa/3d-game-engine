#pragma once

#include "CoreMinimal.h"
#include "RALResource.h"
#include "RALDescription.h"

namespace LE
{

class RAL_API FRALBuffer : public FRALResource
{
public:
    virtual ~FRALBuffer() = default;

public:
    virtual void* Map(uint64 Offset, uint64 Size) = 0;
    virtual void  Unmap() = 0;

public:
    virtual const FRALBufferDesc& GetDesc() const = 0;
};

} // namespace LE
