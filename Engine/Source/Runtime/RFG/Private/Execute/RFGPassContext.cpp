#include "Execute/RFGPassContext.h"

#include "Compile/RFGCompiledPlan.h"
#include "RAL/RALBuffer.h"
#include "RAL/RALTexture.h"
#include "Record/RFGRecordedGraph.h"

void FRFGExecutionContext::ResetTransientResources()
{
    for (FRALTexture* Texture : OwnedTextures)
    {
        delete Texture;
    }

    for (FRALBuffer* Buffer : OwnedBuffers)
    {
        delete Buffer;
    }

    TextureResources.clear();
    BufferResources.clear();
    OwnedTextures.clear();
    OwnedBuffers.clear();
}

void FRFGPassContext::SetExecutionContext(FRFGExecutionContext* InExecutionContext)
{
    ExecutionContext = InExecutionContext;
}

void FRFGPassContext::SetRecordedGraph(const FRFGRecordedGraph* InRecordedGraph)
{
    RecordedGraph = InRecordedGraph;
}

void FRFGPassContext::SetCompiledPlan(const FRFGCompiledPlan* InCompiledPlan)
{
    CompiledPlan = InCompiledPlan;
}

void FRFGPassContext::SetPassIndex(uint32 InPassIndex)
{
    PassIndex = InPassIndex;
}

FRALDevice* FRFGPassContext::GetDevice() const
{
    return ExecutionContext != nullptr ? ExecutionContext->Device : nullptr;
}

FRALCommandList* FRFGPassContext::GetCommandList() const
{
    if (ExecutionContext == nullptr || CompiledPlan == nullptr)
    {
        return nullptr;
    }

    const std::vector<FRFGCompiledPass>& Passes = CompiledPlan->GetPasses();
    if (PassIndex >= Passes.size())
    {
        return nullptr;
    }

    return ExecutionContext->GetCommandList(Passes[PassIndex].Queue);
}

FRALTexture* FRFGPassContext::ResolveTexture(FRFGResourceHandle ResourceHandle) const
{
    if (ExecutionContext == nullptr)
    {
        return nullptr;
    }

    const auto It = ExecutionContext->TextureResources.find(ResourceHandle.Id);
    return It != ExecutionContext->TextureResources.end() ? It->second : nullptr;
}

FRALBuffer* FRFGPassContext::ResolveBuffer(FRFGResourceHandle ResourceHandle) const
{
    if (ExecutionContext == nullptr)
    {
        return nullptr;
    }

    const auto It = ExecutionContext->BufferResources.find(ResourceHandle.Id);
    return It != ExecutionContext->BufferResources.end() ? It->second : nullptr;
}

uint32 FRFGPassContext::GetPassIndex() const
{
    return PassIndex;
}
