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

enum class ERHIFenceWaitResult : uint8
{
    Success,
    Timeout,
    DeviceLost,
    Error,
};

class RHI_API FRHIFence : public FRHIResource
{
public:
    virtual ~FRHIFence() override = default;
    virtual void Reset() = 0;
    virtual void Wait(uint64 Timeout = UINT64_MAX) = 0;
    // Error/Timeout do not prove that the submitted work has retired.
    virtual ERHIFenceWaitResult WaitChecked(uint64 = UINT64_MAX) { return ERHIFenceWaitResult::Error; }
    virtual bool IsSignaled() = 0;
};

} // namespace LE
