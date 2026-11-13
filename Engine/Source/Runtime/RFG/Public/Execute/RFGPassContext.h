#pragma once

#include "CoreMinimal.h"
#include "Core/RFGHandles.h"
#include "Core/RFGTypes.h"


namespace LE
{

class FRHIDevice;
class FRHIQueue;
class FRHICommandList;
class FRHITexture;
class FRHIBuffer;
class FRFGRecordedGraph;
class FRFGCompiledPlan;
class IRFGDeferredReleaseSink;

struct FRFGExecutionContext
{
    LE::FRHIDevice* Device = nullptr;

    LE::FRHIQueue* GraphicsQueue  = nullptr;
    LE::FRHIQueue* ComputeQueue   = nullptr;
    LE::FRHIQueue* TransferQueue  = nullptr;

    // 当前阶段：单 CommandList，所有 Pass 共享
    LE::FRHICommandList* CommandList = nullptr;

    // 多队列预留：根据 Pass 所属队列返回对应 CommandList
    // 当前实现统一返回 CommandList，后续多线程扩展时在此分发
    LE::FRHICommandList* GetCommandList(ERFGQueueType /*Queue*/) const
    {
        return CommandList;
    }

    void ResetTransientResources();
    bool TransferTransientResources(IRFGDeferredReleaseSink& DeferredReleaseSink) noexcept;

    LE::HashMap<uint32, LE::FRHITexture*> TextureResources;
    LE::HashMap<uint32, LE::FRHIBuffer*> BufferResources;
    LE::Array<LE::FRHITexture*> OwnedTextures;
    LE::Array<LE::FRHIBuffer*> OwnedBuffers;
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
    LE::FRHIDevice* GetDevice() const;
    LE::FRHICommandList* GetCommandList() const;

public:
    LE::FRHITexture* ResolveTexture(FRFGResourceHandle ResourceHandle) const;
    LE::FRHIBuffer* ResolveBuffer(FRFGResourceHandle ResourceHandle) const;

public:
    uint32 GetPassIndex() const;

private:
    FRFGExecutionContext* ExecutionContext = nullptr;
    const FRFGRecordedGraph* RecordedGraph = nullptr;
    const FRFGCompiledPlan* CompiledPlan = nullptr;
    uint32 PassIndex = 0xFFFFFFFFu;
};

} // namespace LE
