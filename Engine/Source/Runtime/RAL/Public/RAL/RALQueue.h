#pragma once

#include "CoreMinimal.h"
#include "RALResource.h"

class FRALCommandList;

class RAL_API FRALSemaphore : public FRALResource
{
public:
    virtual ~FRALSemaphore() = default;
};

class RAL_API FRALFence : public FRALResource
{
public:
    virtual ~FRALFence() = default;
    virtual void Reset() = 0;
    virtual void Wait(uint64 Timeout = UINT64_MAX) = 0;
    virtual bool IsSignaled() = 0;
};

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
