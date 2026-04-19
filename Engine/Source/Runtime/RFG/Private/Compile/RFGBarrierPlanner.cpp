#include "Compile/RFGBarrierPlanner.h"

#include "Compile/RFGCompiledPlan.h"
#include "Record/RFGRecordedGraph.h"

#include <unordered_map>

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
    ERFGPipelineStage Stage = ERFGPipelineStage::None;
    ERFGAccessType Access = ERFGAccessType::None;
    ERFGQueueType Queue = ERFGQueueType::Graphics;
    bool bInitialized = false;
};
}

void FRFGBarrierPlanner::BuildResourceLifetimes(const FRFGRecordedGraph& RecordedGraph, FRFGCompiledPlan& InOutPlan) const
{
    std::vector<FRFGCompiledResourceLife>& ResourceLifetimes = InOutPlan.GetMutableResourceLifetimes();
    ResourceLifetimes.clear();

    std::unordered_map<uint32, uint32> LifetimeIndexByResourceId;

    const std::vector<FRFGCompiledPass>& Passes = InOutPlan.GetPasses();
    for (uint32 PassIndex = 0; PassIndex < Passes.size(); ++PassIndex)
    {
        const FRFGPassNode& PassNode = RecordedGraph.GetPassNode(Passes[PassIndex].Handle);
        for (const FRFGPassResourceAccess& ResourceAccess : PassNode.ResourceAccesses)
        {
            const auto LifetimeIt = LifetimeIndexByResourceId.find(ResourceAccess.Resource.Id);
            if (LifetimeIt == LifetimeIndexByResourceId.end())
            {
                FRFGCompiledResourceLife ResourceLife;
                ResourceLife.Resource = ResourceAccess.Resource;
                ResourceLife.FirstPassIndex = PassIndex;
                ResourceLife.LastPassIndex = PassIndex;

                const FRFGResourceNode& ResourceNode = RecordedGraph.GetResourceNode(ResourceAccess.Resource);
                ResourceLife.bTransient = !EnumHasAnyFlags(
                    ResourceNode.Flags,
                    ERFGResourceFlags::Imported | ERFGResourceFlags::External | ERFGResourceFlags::Persistent);

                LifetimeIndexByResourceId.emplace(ResourceAccess.Resource.Id, static_cast<uint32>(ResourceLifetimes.size()));
                ResourceLifetimes.push_back(ResourceLife);
            }
            else
            {
                FRFGCompiledResourceLife& ResourceLife = ResourceLifetimes[LifetimeIt->second];
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
    std::unordered_map<uint32, FLastResourceState> LastStates;

    std::vector<FRFGCompiledPass>& Passes = InOutPlan.GetMutablePasses();
    for (FRFGCompiledPass& CompiledPass : Passes)
    {
        CompiledPass.PreBarriers.clear();
        CompiledPass.PostBarriers.clear();

        const FRFGPassNode& PassNode = RecordedGraph.GetPassNode(CompiledPass.Handle);
        for (const FRFGPassResourceAccess& ResourceAccess : PassNode.ResourceAccesses)
        {
            FLastResourceState& LastState = LastStates[ResourceAccess.Resource.Id];
            const bool bNeedsBarrier =
                LastState.bInitialized &&
                (LastState.Queue != CompiledPass.Queue ||
                 LastState.Stage != ResourceAccess.Access.PipelineStage ||
                 LastState.Access != ResourceAccess.Access.Access) &&
                (AccessHasWrite(LastState.Access) || AccessHasWrite(ResourceAccess.Access.Access));

            if (bNeedsBarrier)
            {
                FRFGBarrierTransition Transition;
                Transition.Resource = ResourceAccess.Resource;
                Transition.BeforeStage = LastState.Stage;
                Transition.AfterStage = ResourceAccess.Access.PipelineStage;
                Transition.BeforeAccess = LastState.Access;
                Transition.AfterAccess = ResourceAccess.Access.Access;
                Transition.SrcQueue = LastState.Queue;
                Transition.DstQueue = CompiledPass.Queue;
                CompiledPass.PreBarriers.push_back(Transition);
            }

            LastState.Stage = ResourceAccess.Access.PipelineStage;
            LastState.Access = ResourceAccess.Access.Access;
            LastState.Queue = CompiledPass.Queue;
            LastState.bInitialized = true;
        }
    }
}
