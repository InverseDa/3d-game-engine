#include "Compile/RFGBarrierPlanner.h"

#include "Compile/RFGCompiledPlan.h"
#include "Record/RFGRecordedGraph.h"

namespace LE
{

namespace
{
bool AccessHasRead(ERFGAccessType AccessType)
{
    return AccessType == ERFGAccessType::Read || AccessType == ERFGAccessType::ReadWrite;
}

bool AccessHasWrite(ERFGAccessType AccessType)
{
    return AccessType == ERFGAccessType::Write || AccessType == ERFGAccessType::ReadWrite;
}

struct FLastResourceState
{
    LE::ERALResourceState ResourceState = LE::ERALResourceState::Unknown;
    LE::EShaderStage ShaderStage = LE::EShaderStage::None;
    ERFGPipelineStage Stage = ERFGPipelineStage::None;
    ERFGAccessType Access = ERFGAccessType::None;
    ERFGQueueType Queue = ERFGQueueType::Graphics;
    bool bInitialized = false;
};
}

void FRFGBarrierPlanner::BuildResourceLifetimes(const FRFGRecordedGraph& RecordedGraph, FRFGCompiledPlan& InOutPlan) const
{
    LE::Array<FRFGCompiledResourceLife>& ResourceLifetimes = InOutPlan.GetMutableResourceLifetimes();
    ResourceLifetimes.Clear();

    LE::HashMap<uint32, uint32> LifetimeIndexByResourceId;

    const LE::Array<FRFGCompiledPass>& Passes = InOutPlan.GetPasses();
    for (uint32 PassIndex = 0; PassIndex < Passes.Size(); ++PassIndex)
    {
        const FRFGPassNode& PassNode = RecordedGraph.GetPassNode(Passes[PassIndex].Handle);
        for (const FRFGPassResourceAccess& ResourceAccess : PassNode.ResourceAccesses)
        {
            uint32* const LifetimeIndex = LifetimeIndexByResourceId.Find(ResourceAccess.Resource.Id);
            if (LifetimeIndex == nullptr)
            {
                FRFGCompiledResourceLife ResourceLife;
                ResourceLife.Resource = ResourceAccess.Resource;
                ResourceLife.FirstPassIndex = PassIndex;
                ResourceLife.LastPassIndex = PassIndex;

                const FRFGResourceNode& ResourceNode = RecordedGraph.GetResourceNode(ResourceAccess.Resource);
                ResourceLife.bTransient = !EnumHasAnyFlags(
                    ResourceNode.Flags,
                    ERFGResourceFlags::Imported | ERFGResourceFlags::External | ERFGResourceFlags::Persistent);

                LifetimeIndexByResourceId.Insert(ResourceAccess.Resource.Id, static_cast<uint32>(ResourceLifetimes.Size()));
                ResourceLifetimes.PushBack(ResourceLife);
            }
            else
            {
                FRFGCompiledResourceLife& ResourceLife = ResourceLifetimes[*LifetimeIndex];
                if (PassIndex < ResourceLife.FirstPassIndex)
                {
                    ResourceLife.FirstPassIndex = PassIndex;
                }
                if (PassIndex > ResourceLife.LastPassIndex)
                {
                    ResourceLife.LastPassIndex = PassIndex;
                }
            }
        }
    }
}

void FRFGBarrierPlanner::BuildBarriers(const FRFGRecordedGraph& RecordedGraph, FRFGCompiledPlan& InOutPlan) const
{
    LE::HashMap<uint32, FLastResourceState> LastStates;

    LE::Array<FRFGCompiledPass>& Passes = InOutPlan.GetMutablePasses();
    for (FRFGCompiledPass& CompiledPass : Passes)
    {
        CompiledPass.PreBarriers.Clear();
        CompiledPass.PostBarriers.Clear();

        const FRFGPassNode& PassNode = RecordedGraph.GetPassNode(CompiledPass.Handle);
        for (const FRFGPassResourceAccess& ResourceAccess : PassNode.ResourceAccesses)
        {
            FLastResourceState* LastStatePointer = LastStates.Find(ResourceAccess.Resource.Id);
            if (LastStatePointer == nullptr)
            {
                LastStates.Insert(ResourceAccess.Resource.Id, FLastResourceState{});
                LastStatePointer = LastStates.Find(ResourceAccess.Resource.Id);
            }
            FLastResourceState& LastState = *LastStatePointer;
            const FRFGResourceNode& ResourceNode = RecordedGraph.GetResourceNode(ResourceAccess.Resource);
            const LE::ERALResourceState BeforeState = LastState.bInitialized ? LastState.ResourceState : ResourceNode.InitialState;
            const bool bNeedsBarrier =
                (!LastState.bInitialized && ResourceAccess.Access.State != LE::ERALResourceState::Undefined) ||
                (LastState.bInitialized &&
                 (LastState.Queue != CompiledPass.Queue ||
                 BeforeState != ResourceAccess.Access.State ||
                 AccessHasWrite(LastState.Access) ||
                 AccessHasWrite(ResourceAccess.Access.Access)));

            if (bNeedsBarrier)
            {
                FRFGBarrierTransition Transition;
                Transition.Resource = ResourceAccess.Resource;
                Transition.BeforeState = BeforeState;
                Transition.AfterState = ResourceAccess.Access.State;
                Transition.BeforeShaderStage = LastState.ShaderStage;
                Transition.AfterShaderStage = ResourceAccess.Access.ShaderStage;
                Transition.BeforeStage = LastState.Stage;
                Transition.AfterStage = ResourceAccess.Access.PipelineStage;
                Transition.BeforeAccess = LastState.Access;
                Transition.AfterAccess = ResourceAccess.Access.Access;
                Transition.BaseMipLevel = ResourceAccess.Access.BaseMipLevel;
                Transition.MipCount = ResourceAccess.Access.MipCount;
                Transition.BaseArrayLayer = ResourceAccess.Access.BaseArrayLayer;
                Transition.LayerCount = ResourceAccess.Access.LayerCount;
                Transition.SrcQueue = LastState.Queue;
                Transition.DstQueue = CompiledPass.Queue;
                CompiledPass.PreBarriers.PushBack(Transition);
            }

            LastState.ResourceState = ResourceAccess.Access.State;
            LastState.ShaderStage = ResourceAccess.Access.ShaderStage;
            LastState.Stage = ResourceAccess.Access.PipelineStage;
            LastState.Access = ResourceAccess.Access.Access;
            LastState.Queue = CompiledPass.Queue;
            LastState.bInitialized = true;
        }
    }
}

} // namespace LE
