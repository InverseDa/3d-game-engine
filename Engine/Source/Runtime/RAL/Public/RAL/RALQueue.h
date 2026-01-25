#pragma once

#include "CoreMinimal.h"
#include "RALResource.h"
#include "RALSyncPrimitives.h"

class FRALCommandList;

enum class EQueueType : uint8
{
    Graphics, // Render + Compute + Transfer
    Compute,  // Compute only
    Transfer, // Copy only (DMA)
};

struct FRALSubmitInfo
{
    FRALCommandList* CmdList = nullptr;

    std::vector<FRALSemaphore*> WaitSemaphores;
    std::vector<FRALSemaphore*> SignalSemaphores;

    FRALFence* FenceToSignal = nullptr;
};

class RAL_API FRALQueue : public FRALResource
{
public:
    virtual ~FRALQueue() = default;

public:
    virtual void Submit(const FRALSubmitInfo& SubmitInfo) = 0;
    virtual void WaitIdle() = 0;
    virtual EQueueType GetType() const = 0;
};
