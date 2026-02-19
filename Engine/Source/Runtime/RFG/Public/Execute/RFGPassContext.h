#pragma once

#include "CoreMinimal.h"
#include "Core/RFGHandles.h"
#include "Core/RFGTypes.h"

class FRALDevice;
class FRALQueue;
class FRALCommandList;
class FRALTexture;
class FRALBuffer;
class FRFGRecordedGraph;
class FRFGCompiledPlan;

struct FRFGExecutionContext
{
    FRALDevice* Device = nullptr;

    FRALQueue* GraphicsQueue  = nullptr;
    FRALQueue* ComputeQueue   = nullptr;
    FRALQueue* TransferQueue  = nullptr;

    // 当前阶段：单 CommandList，所有 Pass 共享
    FRALCommandList* CommandList = nullptr;

    // 多队列预留：根据 Pass 所属队列返回对应 CommandList
    // 当前实现统一返回 CommandList，后续多线程扩展时在此分发
    FRALCommandList* GetCommandList(ERFGQueueType /*Queue*/) const
    {
        return CommandList;
    }
};

class RFG_API FRFGPassContext
{
public:
    FRFGPassContext() = default;
    ~FRFGPassContext() = default;

public:
    void SetExecutionContext(FRFGExecutionContext* InExecutionContext);
    void SetRecordedGraph(const FRFGRecordedGraph* InRecordedGraph);
    void SetCompiledPlan(const FRFGCompiledPlan* InCompiledPlan);
    void SetPassIndex(uint32 InPassIndex);

public:
    FRALDevice* GetDevice() const;
    FRALCommandList* GetCommandList() const;

public:
    FRALTexture* ResolveTexture(FRFGResourceHandle ResourceHandle) const;
    FRALBuffer* ResolveBuffer(FRFGResourceHandle ResourceHandle) const;

public:
    uint32 GetPassIndex() const;

private:
    FRFGExecutionContext* ExecutionContext = nullptr;
    const FRFGRecordedGraph* RecordedGraph = nullptr;
    const FRFGCompiledPlan* CompiledPlan = nullptr;
    uint32 PassIndex = 0xFFFFFFFFu;
};
