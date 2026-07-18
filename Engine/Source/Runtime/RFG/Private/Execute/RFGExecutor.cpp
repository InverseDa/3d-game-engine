#include "Execute/RFGExecutor.h"

#include "Compile/RFGCompiledPlan.h"
#include "Execute/RFGPassContext.h"
#include "RAL/RALBuffer.h"
#include "RAL/RALCommandList.h"
#include "RAL/RALDescription.h"
#include "RAL/RALDevice.h"
#include "RAL/RALQueue.h"
#include "RAL/RALTexture.h"
#include "Record/RFGPassRegistry.h"
#include "Record/RFGRecordedGraph.h"

LE_DECLARE_LOG_CATEGORY(LogRFGExecute);

namespace
{
FRALTextureDesc MakeTextureDesc(const FRFGResourceNode& ResourceNode)
{
    FRALTextureDesc Desc;
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

FRALBufferDesc MakeBufferDesc(const FRFGResourceNode& ResourceNode)
{
    FRALBufferDesc Desc;
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
            FRALTexture* Texture = ResourceNode.ImportedTexture;
            if (Texture == nullptr)
            {
                Texture = ExecutionContext.Device->CreateTexture(MakeTextureDesc(ResourceNode));
                if (Texture != nullptr)
                {
                    ExecutionContext.OwnedTextures.push_back(Texture);
                }
            }

            ExecutionContext.TextureResources[ResourceNode.Handle.Id] = Texture;
        }
        else
        {
            FRALBuffer* Buffer = ResourceNode.ImportedBuffer;
            if (Buffer == nullptr)
            {
                Buffer = ExecutionContext.Device->CreateBuffer(MakeBufferDesc(ResourceNode));
                if (Buffer != nullptr)
                {
                    ExecutionContext.OwnedBuffers.push_back(Buffer);
                }
            }

            ExecutionContext.BufferResources[ResourceNode.Handle.Id] = Buffer;
        }
    }
}

void ApplyBarriers(
    const std::vector<FRFGBarrierTransition>& Transitions,
    const FRFGRecordedGraph& RecordedGraph,
    FRFGExecutionContext& ExecutionContext,
    FRALCommandList* CommandList)
{
    if (Transitions.empty() || CommandList == nullptr)
    {
        return;
    }

    FRALBarrierBatch BarrierBatch;
    BarrierBatch.TextureBarriers.reserve(Transitions.size());
    BarrierBatch.BufferBarriers.reserve(Transitions.size());

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

        if (Transition.BeforeState == ERALResourceState::Unknown ||
            Transition.AfterState == ERALResourceState::Unknown)
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
            const auto TextureIt = ExecutionContext.TextureResources.find(Transition.Resource.Id);
            if (TextureIt == ExecutionContext.TextureResources.end() || TextureIt->second == nullptr)
            {
                LE_LOG(LogRFGExecute, Error,
                    "RFG texture barrier skipped: resource could not be resolved. ResourceId={}.",
                    Transition.Resource.Id);
                continue;
            }

            FRALTextureBarrierDesc Barrier;
            Barrier.Texture = TextureIt->second;
            Barrier.BeforeState = Transition.BeforeState;
            Barrier.AfterState = Transition.AfterState;
            Barrier.BeforeShaderStage = Transition.BeforeShaderStage;
            Barrier.AfterShaderStage = Transition.AfterShaderStage;
            Barrier.BaseMipLevel = Transition.BaseMipLevel;
            Barrier.MipCount = Transition.MipCount;
            Barrier.BaseArrayLayer = Transition.BaseArrayLayer;
            Barrier.LayerCount = Transition.LayerCount;
            BarrierBatch.TextureBarriers.push_back(Barrier);
        }
        else
        {
            const auto BufferIt = ExecutionContext.BufferResources.find(Transition.Resource.Id);
            if (BufferIt == ExecutionContext.BufferResources.end() || BufferIt->second == nullptr)
            {
                LE_LOG(LogRFGExecute, Error,
                    "RFG buffer barrier skipped: resource could not be resolved. ResourceId={}.",
                    Transition.Resource.Id);
                continue;
            }

            FRALBufferBarrierDesc Barrier;
            Barrier.Buffer = BufferIt->second;
            Barrier.BeforeState = Transition.BeforeState;
            Barrier.AfterState = Transition.AfterState;
            Barrier.BeforeShaderStage = Transition.BeforeShaderStage;
            Barrier.AfterShaderStage = Transition.AfterShaderStage;
            BarrierBatch.BufferBarriers.push_back(Barrier);
        }
    }

    if (!BarrierBatch.TextureBarriers.empty() || !BarrierBatch.BufferBarriers.empty())
    {
        CommandList->ResourceBarriers(BarrierBatch);
    }
}
}

void FRFGExecutor::Execute(
    const FRFGCompiledPlan& CompiledPlan,
    const FRFGRecordedGraph& RecordedGraph,
    FRFGExecutionContext& ExecutionContext,
    const FRFGExecuteOptions& ExecuteOptions) const
{
    PrepareResources(RecordedGraph, ExecutionContext);

    FRALCommandList* CommandList = ExecutionContext.CommandList;
    if (CommandList != nullptr)
    {
        CommandList->Begin();
    }

    FRFGPassContext PassContext;
    PassContext.SetExecutionContext(&ExecutionContext);
    PassContext.SetRecordedGraph(&RecordedGraph);
    PassContext.SetCompiledPlan(&CompiledPlan);

    const std::vector<FRFGCompiledPass>& Passes = CompiledPlan.GetPasses();
    for (uint32 PassIndex = 0; PassIndex < Passes.size(); ++PassIndex)
    {
        PassContext.SetPassIndex(PassIndex);
        FRALCommandList* PassCommandList = ExecutionContext.GetCommandList(Passes[PassIndex].Queue);
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

    if (CommandList != nullptr)
    {
        CommandList->End();
    }

    FRALQueue* SubmitQueue = ExecutionContext.GraphicsQueue;
    if (ExecuteOptions.bSubmitImmediately && SubmitQueue != nullptr && CommandList != nullptr)
    {
        FRALSubmitInfo SubmitInfo;
        SubmitInfo.CmdList = CommandList;
        SubmitQueue->Submit(SubmitInfo);
    }

    if (ExecuteOptions.bWaitForCompletion && SubmitQueue != nullptr)
    {
        SubmitQueue->WaitIdle();
    }

    ExecutionContext.ResetTransientResources();
}
