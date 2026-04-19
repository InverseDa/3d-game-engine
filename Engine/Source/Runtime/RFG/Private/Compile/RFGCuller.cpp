#include "Compile/RFGCuller.h"

#include "Compile/RFGCompiledPlan.h"
#include "Record/RFGRecordedGraph.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
bool AccessCanProduceOutput(ERFGAccessType AccessType)
{
    return AccessType == ERFGAccessType::Write || AccessType == ERFGAccessType::ReadWrite;
}
}

void FRFGCuller::CullPasses(const FRFGRecordedGraph& RecordedGraph, FRFGCompiledPlan& InOutPlan) const
{
    std::vector<FRFGCompiledPass>& Passes = InOutPlan.GetMutablePasses();
    if (Passes.empty())
    {
        return;
    }

    std::unordered_map<uint32, uint32> PassIndexById;
    for (uint32 PassIndex = 0; PassIndex < Passes.size(); ++PassIndex)
    {
        PassIndexById.emplace(Passes[PassIndex].Handle.Id, PassIndex);
    }

    std::unordered_set<uint32> RequiredPassIds;
    std::vector<FRFGPassHandle> PendingPasses;

    for (const FRFGCompiledPass& CompiledPass : Passes)
    {
        const FRFGPassNode& PassNode = RecordedGraph.GetPassNode(CompiledPass.Handle);
        const bool bHasRetentionFlags =
            EnumHasAnyFlags(PassNode.Flags, ERFGPassFlags::HasSideEffects) ||
            EnumHasAnyFlags(PassNode.Flags, ERFGPassFlags::NeverCull);

        bool bProducesOutput = false;
        for (const FRFGPassResourceAccess& ResourceAccess : PassNode.ResourceAccesses)
        {
            if (RecordedGraph.IsOutputResource(ResourceAccess.Resource) && AccessCanProduceOutput(ResourceAccess.Access.Access))
            {
                bProducesOutput = true;
                break;
            }
        }

        if (bHasRetentionFlags || bProducesOutput)
        {
            if (RequiredPassIds.insert(CompiledPass.Handle.Id).second)
            {
                PendingPasses.push_back(CompiledPass.Handle);
            }
        }
    }

    while (!PendingPasses.empty())
    {
        const FRFGPassHandle PassHandle = PendingPasses.back();
        PendingPasses.pop_back();

        const auto PassIndexIt = PassIndexById.find(PassHandle.Id);
        if (PassIndexIt == PassIndexById.end())
        {
            continue;
        }

        const FRFGCompiledPass& CompiledPass = Passes[PassIndexIt->second];
        for (const FRFGDependencyEdge& Edge : CompiledPass.IncomingEdges)
        {
            if (RequiredPassIds.insert(Edge.SourcePass.Id).second)
            {
                PendingPasses.push_back(Edge.SourcePass);
            }
        }
    }

    std::vector<FRFGCompiledPass> CulledPasses;
    CulledPasses.reserve(RequiredPassIds.size());

    for (FRFGCompiledPass& Pass : Passes)
    {
        if (RequiredPassIds.find(Pass.Handle.Id) != RequiredPassIds.end())
        {
            CulledPasses.push_back(std::move(Pass));
        }
    }

    Passes = std::move(CulledPasses);

    PassIndexById.clear();
    for (uint32 PassIndex = 0; PassIndex < Passes.size(); ++PassIndex)
    {
        PassIndexById.emplace(Passes[PassIndex].Handle.Id, PassIndex);
    }

    for (FRFGCompiledPass& Pass : Passes)
    {
        Pass.IncomingEdges.erase(
            std::remove_if(
                Pass.IncomingEdges.begin(),
                Pass.IncomingEdges.end(),
                [&](const FRFGDependencyEdge& Edge)
                {
                    return RequiredPassIds.find(Edge.SourcePass.Id) == RequiredPassIds.end();
                }),
            Pass.IncomingEdges.end());

        uint32 DependencyLevel = 0;
        for (const FRFGDependencyEdge& Edge : Pass.IncomingEdges)
        {
            const auto SourceIndexIt = PassIndexById.find(Edge.SourcePass.Id);
            if (SourceIndexIt == PassIndexById.end())
            {
                continue;
            }

            DependencyLevel = std::max(DependencyLevel, Passes[SourceIndexIt->second].DependencyLevel + 1);
        }

        Pass.DependencyLevel = DependencyLevel;
    }
}
