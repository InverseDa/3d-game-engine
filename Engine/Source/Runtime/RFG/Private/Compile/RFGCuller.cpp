#include "Compile/RFGCuller.h"

#include "Compile/RFGCompiledPlan.h"
#include "Record/RFGRecordedGraph.h"

#include <algorithm>

namespace LE
{

namespace
{
bool AccessCanProduceOutput(ERFGAccessType AccessType)
{
    return AccessType == ERFGAccessType::Write || AccessType == ERFGAccessType::ReadWrite;
}
}

void FRFGCuller::CullPasses(const FRFGRecordedGraph& RecordedGraph, FRFGCompiledPlan& InOutPlan) const
{
    LE::Array<FRFGCompiledPass>& Passes = InOutPlan.GetMutablePasses();
    if (Passes.IsEmpty())
    {
        return;
    }

    LE::HashMap<uint32, uint32> PassIndexById;
    for (uint32 PassIndex = 0; PassIndex < Passes.Size(); ++PassIndex)
    {
        PassIndexById.Insert(Passes[PassIndex].Handle.Id, PassIndex);
    }

    LE::HashSet<uint32> RequiredPassIds;
    LE::Array<FRFGPassHandle> PendingPasses;

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
            if (RequiredPassIds.Insert(CompiledPass.Handle.Id))
            {
                PendingPasses.PushBack(CompiledPass.Handle);
            }
        }
    }

    while (!PendingPasses.IsEmpty())
    {
        const FRFGPassHandle PassHandle = PendingPasses.Back();
        PendingPasses.PopBack();

        const uint32* const PassIndex = PassIndexById.Find(PassHandle.Id);
        if (PassIndex == nullptr)
        {
            continue;
        }

        const FRFGCompiledPass& CompiledPass = Passes[*PassIndex];
        for (const FRFGDependencyEdge& Edge : CompiledPass.IncomingEdges)
        {
            if (RequiredPassIds.Insert(Edge.SourcePass.Id))
            {
                PendingPasses.PushBack(Edge.SourcePass);
            }
        }
    }

    LE::Array<FRFGCompiledPass> CulledPasses;
    CulledPasses.Reserve(RequiredPassIds.Size());

    for (FRFGCompiledPass& Pass : Passes)
    {
        if (RequiredPassIds.Contains(Pass.Handle.Id))
        {
            CulledPasses.PushBack(std::move(Pass));
        }
    }

    Passes = std::move(CulledPasses);

    PassIndexById.Clear();
    for (uint32 PassIndex = 0; PassIndex < Passes.Size(); ++PassIndex)
    {
        PassIndexById.Insert(Passes[PassIndex].Handle.Id, PassIndex);
    }

    for (FRFGCompiledPass& Pass : Passes)
    {
        for (std::size_t EdgeIndex = Pass.IncomingEdges.Size(); EdgeIndex > 0; --EdgeIndex)
        {
            if (!RequiredPassIds.Contains(Pass.IncomingEdges[EdgeIndex - 1].SourcePass.Id))
            {
                Pass.IncomingEdges.Erase(EdgeIndex - 1);
            }
        }

        uint32 DependencyLevel = 0;
        for (const FRFGDependencyEdge& Edge : Pass.IncomingEdges)
        {
            const uint32* const SourceIndex = PassIndexById.Find(Edge.SourcePass.Id);
            if (SourceIndex == nullptr)
            {
                continue;
            }

            DependencyLevel = std::max(DependencyLevel, Passes[*SourceIndex].DependencyLevel + 1);
        }

        Pass.DependencyLevel = DependencyLevel;
    }
}

} // namespace LE
