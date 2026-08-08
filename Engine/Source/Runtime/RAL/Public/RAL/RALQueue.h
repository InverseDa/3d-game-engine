#pragma once

#include "CoreMinimal.h"
#include "RALResource.h"
#include "RALSyncPrimitives.h"

namespace LE
{

class FRALCommandList;

enum class EQueueType : uint8
{
    Graphics, // Render + Compute + Transfer
    Compute,  // Compute only
    Transfer, // Copy only (DMA)
};

enum class ERALQueueSubmitResult : uint8
{
    Success,
    InvalidArguments,
    Error,
};

struct FRALSubmitInfo
{
    FRALCommandList* CmdList = nullptr;

    LE::Array<FRALSemaphore*> WaitSemaphores;
    LE::Array<FRALSemaphore*> SignalSemaphores;

    FRALFence* FenceToSignal = nullptr;

    bool IsStructurallyValid() const noexcept
    {
        if (CmdList == nullptr && WaitSemaphores.IsEmpty() && SignalSemaphores.IsEmpty() &&
            FenceToSignal == nullptr)
        {
            return false;
        }
        for (FRALSemaphore* const Semaphore : WaitSemaphores)
        {
            if (Semaphore == nullptr)
            {
                return false;
            }
        }
        for (FRALSemaphore* const Semaphore : SignalSemaphores)
        {
            if (Semaphore == nullptr)
            {
                return false;
            }
        }
        return true;
    }
};

class RAL_API FRALQueue : public FRALResource
{
public:
    virtual ~FRALQueue() = default;

public:
    virtual ERALQueueSubmitResult Submit(const FRALSubmitInfo& SubmitInfo) = 0;
    virtual void WaitIdle() = 0;
    virtual EQueueType GetType() const = 0;
};

} // namespace LE
