#include "Execute/RFGExecutor.h"

#include "Compile/RFGCompiledPlan.h"
#include "Execute/RFGPassContext.h"
#include "RAL/RALBuffer.h"
#include "RAL/RALCommandList.h"
#include "RAL/RALDescription.h"
#include "RAL/RALDevice.h"
#include "RAL/RALQueue.h"
#include "RAL/RALSyncPrimitives.h"
#include "RAL/RALTexture.h"
#include "Record/RFGPassRegistry.h"
#include "Record/RFGRecordedGraph.h"

namespace LE
{

LE_DECLARE_LOG_CATEGORY(LogRFGExecute);

namespace
{
LE::FRALTextureDesc MakeTextureDesc(const FRFGResourceNode& ResourceNode)
{
    LE::FRALTextureDesc Desc;
    Desc.Name = ResourceNode.Name;
    Desc.Width = ResourceNode.Desc.Texture.Width;
    Desc.Height = ResourceNode.Desc.Texture.Height;
    Desc.Depth = ResourceNode.Desc.Texture.Depth;
    Desc.MipLevels = ResourceNode.Desc.Texture.MipLevels;
    Desc.ArrayLayers = ResourceNode.Desc.Texture.ArrayLayers;
    Desc.Format = ResourceNode.Desc.Texture.Format;
    Desc.bIsUAV = (ResourceNode.Desc.Texture.UsageMask & (1u << 0)) != 0;
    Desc.bIsRenderTarget = (ResourceNode.Desc.Texture.UsageMask & (1u << 1)) != 0;
    Desc.bIsDepthStencil = (ResourceNode.Desc.Texture.UsageMask & (1u << 2)) != 0;
    Desc.bIsShaderResource = (ResourceNode.Desc.Texture.UsageMask & (1u << 3)) != 0;
    return Desc;
}

LE::FRALBufferDesc MakeBufferDesc(const FRFGResourceNode& ResourceNode)
{
    LE::FRALBufferDesc Desc;
    Desc.Name = ResourceNode.Name;
    Desc.Size = ResourceNode.Desc.Buffer.Size;
    Desc.Usage = static_cast<uint32>(ResourceNode.Desc.Buffer.Usage);
    Desc.UsageFlag = ResourceNode.Desc.Buffer.UsageMask;
    return Desc;
}

void PrepareResources(const FRFGRecordedGraph& RecordedGraph, FRFGExecutionContext& ExecutionContext)
{
    ExecutionContext.ResetTransientResources();

    if (ExecutionContext.Device == nullptr)
    {
        return;
    }

    for (const FRFGResourceNode& ResourceNode : RecordedGraph.GetResourceNodes())
    {
        if (ResourceNode.Desc.Type == ERFGResourceType::Texture)
        {
            LE::FRALTexture* Texture = ResourceNode.ImportedTexture;
            if (Texture == nullptr)
            {
                Texture = ExecutionContext.Device->CreateTexture(MakeTextureDesc(ResourceNode));
                if (Texture != nullptr)
                {
                    ExecutionContext.OwnedTextures.PushBack(Texture);
                }
            }

            ExecutionContext.TextureResources.InsertOrAssign(ResourceNode.Handle.Id, Texture);
        }
        else
        {
            LE::FRALBuffer* Buffer = ResourceNode.ImportedBuffer;
            if (Buffer == nullptr)
            {
                Buffer = ExecutionContext.Device->CreateBuffer(MakeBufferDesc(ResourceNode));
                if (Buffer != nullptr)
                {
                    ExecutionContext.OwnedBuffers.PushBack(Buffer);
                }
            }

            ExecutionContext.BufferResources.InsertOrAssign(ResourceNode.Handle.Id, Buffer);
        }
    }
}

void ApplyBarriers(
    const LE::Array<FRFGBarrierTransition>& Transitions,
    const FRFGRecordedGraph& RecordedGraph,
    FRFGExecutionContext& ExecutionContext,
    LE::FRALCommandList* CommandList)
{
    if (Transitions.IsEmpty() || CommandList == nullptr)
    {
        return;
    }

    LE::FRALBarrierBatch BarrierBatch;
    BarrierBatch.TextureBarriers.Reserve(Transitions.Size());
    BarrierBatch.BufferBarriers.Reserve(Transitions.Size());

    for (const FRFGBarrierTransition& Transition : Transitions)
    {
        if (Transition.SrcQueue != ERFGQueueType::Graphics ||
            Transition.DstQueue != ERFGQueueType::Graphics ||
            Transition.SrcQueue != Transition.DstQueue)
        {
            LE_LOG(LogRFGExecute, Error,
                "RFG barrier skipped: cross-queue transitions are not supported yet. ResourceId={}, SrcQueue={}, DstQueue={}.",
                Transition.Resource.Id,
                static_cast<uint32>(Transition.SrcQueue),
                static_cast<uint32>(Transition.DstQueue));
            continue;
        }

        if (Transition.BeforeState == LE::ERALResourceState::Unknown ||
            Transition.AfterState == LE::ERALResourceState::Unknown)
        {
            LE_LOG(LogRFGExecute, Error,
                "RFG barrier skipped: resource state is Unknown. ResourceId={}, BeforeState={}, AfterState={}.",
                Transition.Resource.Id,
                static_cast<uint32>(Transition.BeforeState),
                static_cast<uint32>(Transition.AfterState));
            continue;
        }

        const FRFGResourceNode& ResourceNode = RecordedGraph.GetResourceNode(Transition.Resource);
        if (ResourceNode.Desc.Type == ERFGResourceType::Texture)
        {
            LE::FRALTexture* const* const Texture = ExecutionContext.TextureResources.Find(Transition.Resource.Id);
            if (Texture == nullptr || *Texture == nullptr)
            {
                LE_LOG(LogRFGExecute, Error,
                    "RFG texture barrier skipped: resource could not be resolved. ResourceId={}.",
                    Transition.Resource.Id);
                continue;
            }

            LE::FRALTextureBarrierDesc Barrier;
            Barrier.Texture = *Texture;
            Barrier.BeforeState = Transition.BeforeState;
            Barrier.AfterState = Transition.AfterState;
            Barrier.BeforeShaderStage = Transition.BeforeShaderStage;
            Barrier.AfterShaderStage = Transition.AfterShaderStage;
            Barrier.BaseMipLevel = Transition.BaseMipLevel;
            Barrier.MipCount = Transition.MipCount;
            Barrier.BaseArrayLayer = Transition.BaseArrayLayer;
            Barrier.LayerCount = Transition.LayerCount;
            BarrierBatch.TextureBarriers.PushBack(Barrier);
        }
        else
        {
            LE::FRALBuffer* const* const Buffer = ExecutionContext.BufferResources.Find(Transition.Resource.Id);
            if (Buffer == nullptr || *Buffer == nullptr)
            {
                LE_LOG(LogRFGExecute, Error,
                    "RFG buffer barrier skipped: resource could not be resolved. ResourceId={}.",
                    Transition.Resource.Id);
                continue;
            }

            LE::FRALBufferBarrierDesc Barrier;
            Barrier.Buffer = *Buffer;
            Barrier.BeforeState = Transition.BeforeState;
            Barrier.AfterState = Transition.AfterState;
            Barrier.BeforeShaderStage = Transition.BeforeShaderStage;
            Barrier.AfterShaderStage = Transition.AfterShaderStage;
            BarrierBatch.BufferBarriers.PushBack(Barrier);
        }
    }

    if (!BarrierBatch.TextureBarriers.IsEmpty() || !BarrierBatch.BufferBarriers.IsEmpty())
    {
        CommandList->ResourceBarriers(BarrierBatch);
    }
}
}

ERALQueueSubmitResult FRFGExecutor::Execute(
    const FRFGCompiledPlan& CompiledPlan,
    const FRFGRecordedGraph& RecordedGraph,
    FRFGExecutionContext& ExecutionContext,
    const FRFGExecuteOptions& ExecuteOptions) const
{
    ERALQueueSubmitResult SubmitResult = ERALQueueSubmitResult::Success;
    PrepareResources(RecordedGraph, ExecutionContext);

    const bool bHasOwnedTransientResources =
        !ExecutionContext.OwnedTextures.IsEmpty() || !ExecutionContext.OwnedBuffers.IsEmpty();
    const bool bCompletesSynchronously =
        ExecuteOptions.bSubmitImmediately && ExecuteOptions.bWaitForCompletion;
    if (bHasOwnedTransientResources && !bCompletesSynchronously &&
        ExecuteOptions.DeferredReleaseSink == nullptr)
    {
        LE_LOG(LogRFGExecute, Error,
            "Asynchronous RFG execution rejected: owned transients require a deferred-release sink.");
        ExecutionContext.ResetTransientResources();
        return ERALQueueSubmitResult::InvalidArguments;
    }

    LE::FRALCommandList* const CommandList = ExecutionContext.CommandList;
    LE::FRALQueue* const SubmitQueue = ExecutionContext.GraphicsQueue;
    if (CommandList == nullptr)
    {
        LE_LOG(LogRFGExecute, Error, "RFG execution requires a command list.");
        ExecutionContext.ResetTransientResources();
        return ERALQueueSubmitResult::InvalidArguments;
    }

    LE::FRALSubmitInfo SubmitInfo;
    if (ExecuteOptions.bSubmitImmediately)
    {
        if (SubmitQueue == nullptr)
        {
            LE_LOG(LogRFGExecute, Error, "Immediate RFG execution requires a submit queue.");
            ExecutionContext.ResetTransientResources();
            return ERALQueueSubmitResult::InvalidArguments;
        }
        SubmitInfo = ExecuteOptions.SubmitInfo != nullptr
            ? *ExecuteOptions.SubmitInfo
            : LE::FRALSubmitInfo{};
        if (ExecuteOptions.SubmitInfo == nullptr)
        {
            SubmitInfo.CmdList = CommandList;
        }
        if (SubmitInfo.CmdList != CommandList || !SubmitInfo.IsStructurallyValid())
        {
            LE_LOG(LogRFGExecute, Error,
                "RFG submit info must be structurally valid and reference the execution command list.");
            ExecutionContext.ResetTransientResources();
            return ERALQueueSubmitResult::InvalidArguments;
        }
    }

    CommandList->Begin();

    FRFGPassContext PassContext;
    PassContext.SetExecutionContext(&ExecutionContext);
    PassContext.SetRecordedGraph(&RecordedGraph);
    PassContext.SetCompiledPlan(&CompiledPlan);

    const LE::Array<FRFGCompiledPass>& Passes = CompiledPlan.GetPasses();
    for (uint32 PassIndex = 0; PassIndex < Passes.Size(); ++PassIndex)
    {
        PassContext.SetPassIndex(PassIndex);
        LE::FRALCommandList* PassCommandList = ExecutionContext.GetCommandList(Passes[PassIndex].Queue);
        ApplyBarriers(Passes[PassIndex].PreBarriers, RecordedGraph, ExecutionContext, PassCommandList);

        const FRFGPassNode& PassNode = RecordedGraph.GetPassNode(Passes[PassIndex].Handle);
        if (PassNode.ExecuteCallback)
        {
            PassNode.ExecuteCallback(PassContext);
        }
        else if (PassNode.Executor != nullptr)
        {
            PassNode.Executor->Execute(PassContext, PassNode.Parameters);
        }

        ApplyBarriers(Passes[PassIndex].PostBarriers, RecordedGraph, ExecutionContext, PassCommandList);
    }

    CommandList->End();

    if (bHasOwnedTransientResources && !bCompletesSynchronously &&
        !ExecutionContext.TransferTransientResources(*ExecuteOptions.DeferredReleaseSink))
    {
        LE_LOG(LogRFGExecute, Error,
            "Asynchronous RFG execution rejected: deferred-release sink refused an owned transient.");
        return ERALQueueSubmitResult::InvalidArguments;
    }

    if (ExecuteOptions.bSubmitImmediately)
    {
        // The caller waits before acquiring. Reset immediately before the
        // submission that will signal the fence, after recording has ended.
        if (SubmitInfo.FenceToSignal != nullptr)
        {
            SubmitInfo.FenceToSignal->Reset();
        }
        SubmitResult = SubmitQueue->Submit(SubmitInfo);
    }

    if (ExecuteOptions.bSubmitImmediately && ExecuteOptions.bWaitForCompletion &&
        SubmitResult == ERALQueueSubmitResult::Success)
    {
        SubmitQueue->WaitIdle();
    }

    // Synchronous executions still own their transients until queue completion.
    // Asynchronous executions transferred them before submission and only clear
    // empty resolution state here.
    ExecutionContext.ResetTransientResources();
    return SubmitResult;
}

} // namespace LE
