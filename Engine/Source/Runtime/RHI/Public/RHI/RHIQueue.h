#pragma once

#include "CoreMinimal.h"
#include "RHIResource.h"
#include "RHISyncPrimitives.h"

namespace LE
{

class FRHICommandList;

enum class EQueueType : uint8
{
    Graphics, // Render + Compute + Transfer
    Compute,  // Compute only
    Transfer, // Copy only (DMA)
};

enum class ERHIQueueSubmitResult : uint8
{
    Success,
    InvalidArguments,
    Error,
    DeviceLost,
};

struct FRHISubmitInfo
{
    FRHICommandList* CmdList = nullptr;

    LE::Array<FRHISemaphore*> WaitSemaphores;
    LE::Array<FRHISemaphore*> SignalSemaphores;

    FRHIFence* FenceToSignal = nullptr;

    bool IsStructurallyValid() const noexcept
    {
        if (CmdList == nullptr && WaitSemaphores.IsEmpty() && SignalSemaphores.IsEmpty() && FenceToSignal == nullptr)
        {
            return false;
        }
        for (FRHISemaphore* const Semaphore : WaitSemaphores)
        {
            if (Semaphore == nullptr)
            {
                return false;
            }
        }
        for (FRHISemaphore* const Semaphore : SignalSemaphores)
        {
            if (Semaphore == nullptr)
            {
                return false;
            }
        }
        return true;
    }
};

class RHI_API FRHIQueue : public FRHIResource
{
public:
    virtual ~FRHIQueue() = default;

public:
    virtual ERHIQueueSubmitResult Submit(const FRHISubmitInfo& SubmitInfo) = 0;
    virtual void WaitIdle() = 0;
    virtual ERHIFenceWaitResult WaitIdleChecked() { return ERHIFenceWaitResult::Error; }
    virtual EQueueType GetType() const = 0;
};

} // namespace LE
