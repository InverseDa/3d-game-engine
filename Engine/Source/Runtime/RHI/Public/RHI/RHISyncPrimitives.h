#pragma once

#include "CoreMinimal.h"
#include "RHIResource.h"

namespace LE
{

class RHI_API FRHISemaphore : public FRHIResource
{
public:
    virtual ~FRHISemaphore() override = default;
};

class RHI_API FRHIFence : public FRHIResource
{
public:
    virtual ~FRHIFence() override = default;
    virtual void Reset() = 0;
    virtual void Wait(uint64 Timeout = UINT64_MAX) = 0;
    virtual bool IsSignaled() = 0;
};

} // namespace LE
