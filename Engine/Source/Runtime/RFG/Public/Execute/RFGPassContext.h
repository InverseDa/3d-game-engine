#pragma once

#include "CoreMinimal.h"
#include "Core/RFGHandles.h"
#include "Core/RFGTypes.h"


namespace LE
{

class FRALDevice;
class FRALQueue;
class FRALCommandList;
class FRALTexture;
class FRALBuffer;
class FRFGRecordedGraph;
class FRFGCompiledPlan;

struct FRFGExecutionContext
{
    LE::FRALDevice* Device = nullptr;

    LE::FRALQueue* GraphicsQueue  = nullptr;
    LE::FRALQueue* ComputeQueue   = nullptr;
    LE::FRALQueue* TransferQueue  = nullptr;

    // 当前阶段：单 CommandList，所有 Pass 共享
    LE::FRALCommandList* CommandList = nullptr;

    // 多队列预留：根据 Pass 所属队列返回对应 CommandList
    // 当前实现统一返回 CommandList，后续多线程扩展时在此分发
    LE::FRALCommandList* GetCommandList(ERFGQueueType /*Queue*/) const
    {
        return CommandList;
    }

    void ResetTransientResources();

    LE::HashMap<uint32, LE::FRALTexture*> TextureResources;
    LE::HashMap<uint32, LE::FRALBuffer*> BufferResources;
    LE::Array<LE::FRALTexture*> OwnedTextures;
    LE::Array<LE::FRALBuffer*> OwnedBuffers;
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
    LE::FRALDevice* GetDevice() const;
    LE::FRALCommandList* GetCommandList() const;

public:
    LE::FRALTexture* ResolveTexture(FRFGResourceHandle ResourceHandle) const;
    LE::FRALBuffer* ResolveBuffer(FRFGResourceHandle ResourceHandle) const;

public:
    uint32 GetPassIndex() const;

private:
    FRFGExecutionContext* ExecutionContext = nullptr;
    const FRFGRecordedGraph* RecordedGraph = nullptr;
    const FRFGCompiledPlan* CompiledPlan = nullptr;
    uint32 PassIndex = 0xFFFFFFFFu;
};

} // namespace LE
