#pragma once

#include "CoreMinimal.h"
#include "RALResource.h"

class FRALFence;
class FRALCommandList;

enum class EQueueType : uint8
{
    Graphics, // Render + Compute + Transfer
    Compute,  // Compute only
    Transfer, // Copy only (DMA)
};

class RAL_API FRALQueue : public FRALResource
{
public:
    virtual ~FRALQueue() = default;

public:
    virtual void Sumbit(FRALCommandList* CmdList, FRALFence* FenceSignal = nullptr) = 0;
    virtual void WaitIdle() = 0;
    virtual EQueueType GetType() const = 0;
};
