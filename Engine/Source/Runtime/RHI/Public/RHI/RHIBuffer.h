#pragma once

#include "CoreMinimal.h"
#include "RHIResource.h"
#include "RHIDescription.h"

namespace LE
{

class RHI_API FRHIBuffer : public FRHIResource
{
public:
    virtual ~FRHIBuffer() = default;

public:
    virtual void* Map(uint64 Offset, uint64 Size) = 0;
    virtual void  Unmap() = 0;

public:
    virtual const FRHIBufferDesc& GetDesc() const = 0;
};

} // namespace LE
