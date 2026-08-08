#include "Execute/RFGPassContext.h"

#include "Compile/RFGCompiledPlan.h"
#include "Execute/RFGDeferredReleaseSink.h"
#include "RAL/RALBuffer.h"
#include "RAL/RALDevice.h"
#include "RAL/RALTexture.h"
#include "Record/RFGRecordedGraph.h"

namespace LE
{

void FRFGExecutionContext::ResetTransientResources()
{
    for (LE::FRALTexture* Texture : OwnedTextures)
    {
        RAL::DestroyResource(Texture);
    }

    for (LE::FRALBuffer* Buffer : OwnedBuffers)
    {
        RAL::DestroyResource(Buffer);
    }

    TextureResources.Clear();
    BufferResources.Clear();
    OwnedTextures.Clear();
    OwnedBuffers.Clear();
}

bool FRFGExecutionContext::TransferTransientResources(
    IRFGDeferredReleaseSink& DeferredReleaseSink) noexcept
{
    bool bAcceptedAll = true;
    for (LE::FRALTexture*& Texture : OwnedTextures)
    {
        if (Texture != nullptr && DeferredReleaseSink.DeferRelease(Texture))
        {
            Texture = nullptr;
        }
        else if (Texture != nullptr)
        {
            bAcceptedAll = false;
            break;
        }
    }

    if (bAcceptedAll)
    {
        for (LE::FRALBuffer*& Buffer : OwnedBuffers)
        {
            if (Buffer != nullptr && DeferredReleaseSink.DeferRelease(Buffer))
            {
                Buffer = nullptr;
            }
            else if (Buffer != nullptr)
            {
                bAcceptedAll = false;
                break;
            }
        }
    }

    // Null entries were transferred and are now owned by the sink. Any
    // non-null entries remain RFG-owned and are destroyed here.
    ResetTransientResources();
    return bAcceptedAll;
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

LE::FRALDevice* FRFGPassContext::GetDevice() const
{
    return ExecutionContext != nullptr ? ExecutionContext->Device : nullptr;
}

LE::FRALCommandList* FRFGPassContext::GetCommandList() const
{
    if (ExecutionContext == nullptr || CompiledPlan == nullptr)
    {
        return nullptr;
    }

    const LE::Array<FRFGCompiledPass>& Passes = CompiledPlan->GetPasses();
    if (PassIndex >= Passes.Size())
    {
        return nullptr;
    }

    return ExecutionContext->GetCommandList(Passes[PassIndex].Queue);
}

LE::FRALTexture* FRFGPassContext::ResolveTexture(FRFGResourceHandle ResourceHandle) const
{
    if (ExecutionContext == nullptr)
    {
        return nullptr;
    }

    LE::FRALTexture* const* const Found = ExecutionContext->TextureResources.Find(ResourceHandle.Id);
    return Found != nullptr ? *Found : nullptr;
}

LE::FRALBuffer* FRFGPassContext::ResolveBuffer(FRFGResourceHandle ResourceHandle) const
{
    if (ExecutionContext == nullptr)
    {
        return nullptr;
    }

    LE::FRALBuffer* const* const Found = ExecutionContext->BufferResources.Find(ResourceHandle.Id);
    return Found != nullptr ? *Found : nullptr;
}

uint32 FRFGPassContext::GetPassIndex() const
{
    return PassIndex;
}

} // namespace LE
