#include "Execute/RFGPassContext.h"

#include "Compile/RFGCompiledPlan.h"
#include "Execute/RFGDeferredReleaseSink.h"
#include "RHI/RHIBuffer.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHITexture.h"
#include "Record/RFGRecordedGraph.h"

namespace LE
{

void FRFGExecutionContext::ResetTransientResources()
{
    for (LE::FRHITexture* Texture : OwnedTextures)
    {
        RHI::DestroyResource(Texture);
    }

    for (LE::FRHIBuffer* Buffer : OwnedBuffers)
    {
        RHI::DestroyResource(Buffer);
    }

    TextureResources.Clear();
    BufferResources.Clear();
    OwnedTextures.Clear();
    OwnedBuffers.Clear();
}

bool FRFGExecutionContext::TransferTransientResources(IRFGDeferredReleaseSink& DeferredReleaseSink) noexcept
{
    bool bAcceptedAll = true;
    for (LE::FRHITexture*& Texture : OwnedTextures)
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
        for (LE::FRHIBuffer*& Buffer : OwnedBuffers)
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

LE::FRHIDevice* FRFGPassContext::GetDevice() const
{
    return ExecutionContext != nullptr ? ExecutionContext->Device : nullptr;
}

LE::FRHICommandList* FRFGPassContext::GetCommandList() const
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

LE::FRHITexture* FRFGPassContext::ResolveTexture(FRFGResourceHandle ResourceHandle) const
{
    if (ExecutionContext == nullptr)
    {
        return nullptr;
    }

    LE::FRHITexture* const* const Found = ExecutionContext->TextureResources.Find(ResourceHandle.Id);
    return Found != nullptr ? *Found : nullptr;
}

LE::FRHIBuffer* FRFGPassContext::ResolveBuffer(FRFGResourceHandle ResourceHandle) const
{
    if (ExecutionContext == nullptr)
    {
        return nullptr;
    }

    LE::FRHIBuffer* const* const Found = ExecutionContext->BufferResources.Find(ResourceHandle.Id);
    return Found != nullptr ? *Found : nullptr;
}

uint32 FRFGPassContext::GetPassIndex() const
{
    return PassIndex;
}

} // namespace LE
