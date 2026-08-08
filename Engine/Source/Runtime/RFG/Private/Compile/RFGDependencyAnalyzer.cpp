#include "Compile/RFGDependencyAnalyzer.h"

#include "Compile/RFGCompiledPlan.h"
#include "Record/RFGRecordedGraph.h"

#include <algorithm>
#include <limits>

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

uint64 MakeDependencyKey(FRFGPassHandle SourcePass, FRFGPassHandle TargetPass)
{
    return (static_cast<uint64>(SourcePass.Id) << 32) | static_cast<uint64>(TargetPass.Id);
}

struct FResourceDependencyState
{
    LE::Array<FRFGPassHandle> LastReaders;
    LE::Array<FRFGPassHandle> LastWriters;
};

struct FCompiledPassOrderKey
{
    uint32 DependencyLevel = 0;
    uint32 InsertionOrder = 0;
    LE::String Name;
};

bool IsPassOrderLess(const FCompiledPassOrderKey& Lhs, const FCompiledPassOrderKey& Rhs)
{
    if (Lhs.DependencyLevel != Rhs.DependencyLevel)
    {
        return Lhs.DependencyLevel < Rhs.DependencyLevel;
    }

    if (Lhs.InsertionOrder != Rhs.InsertionOrder)
    {
        return Lhs.InsertionOrder < Rhs.InsertionOrder;
    }

    return Lhs.Name < Rhs.Name;
}
}

void FRFGDependencyAnalyzer::BuildDependencies(const FRFGRecordedGraph& RecordedGraph, FRFGCompiledPlan& OutPlan) const
{
    OutPlan.Clear();

    LE::Array<FRFGCompiledPass>& CompiledPasses = OutPlan.GetMutablePasses();
    CompiledPasses.Reserve(RecordedGraph.GetPassOrder().Size());

    LE::HashMap<uint32, uint32> PassIndexById;
    for (const FRFGPassHandle PassHandle : RecordedGraph.GetPassOrder())
    {
        const FRFGPassNode& PassNode = RecordedGraph.GetPassNode(PassHandle);

        FRFGCompiledPass CompiledPass;
        CompiledPass.Handle = PassHandle;
        CompiledPass.Name = PassNode.Name;
        CompiledPass.Queue = PassNode.Queue;
        CompiledPass.Flags = PassNode.Flags;

        PassIndexById.Insert(PassHandle.Id, static_cast<uint32>(CompiledPasses.Size()));
        CompiledPasses.PushBack(CompiledPass);
    }

    LE::HashMap<uint32, FResourceDependencyState> DependencyStateByResource;
    LE::HashSet<uint64> DependencyKeys;

    for (FRFGCompiledPass& CompiledPass : CompiledPasses)
    {
        const FRFGPassNode& PassNode = RecordedGraph.GetPassNode(CompiledPass.Handle);

        auto AddDependency = [&](FRFGPassHandle SourcePass, ERFGHazardType HazardType)
        {
            if (!SourcePass.IsValid() || SourcePass == CompiledPass.Handle)
            {
                return;
            }

            const uint64 DependencyKey = MakeDependencyKey(SourcePass, CompiledPass.Handle);
            if (!DependencyKeys.Insert(DependencyKey))
            {
                return;
            }

            FRFGDependencyEdge Edge;
            Edge.SourcePass = SourcePass;
            Edge.TargetPass = CompiledPass.Handle;
            Edge.Hazard = HazardType;
            CompiledPass.IncomingEdges.PushBack(Edge);
        };

        for (const FRFGPassHandle ExplicitDependency : PassNode.ExplicitDependencies)
        {
            AddDependency(ExplicitDependency, ERFGHazardType::RAW);
        }

        for (const FRFGPassResourceAccess& ResourceAccess : PassNode.ResourceAccesses)
        {
            FResourceDependencyState* ResourceStatePointer = DependencyStateByResource.Find(ResourceAccess.Resource.Id);
            if (ResourceStatePointer == nullptr)
            {
                DependencyStateByResource.Insert(ResourceAccess.Resource.Id, FResourceDependencyState{});
                ResourceStatePointer = DependencyStateByResource.Find(ResourceAccess.Resource.Id);
            }
            FResourceDependencyState& ResourceState = *ResourceStatePointer;
            const bool bReads = AccessHasRead(ResourceAccess.Access.Access);
            const bool bWrites = AccessHasWrite(ResourceAccess.Access.Access);

            if (bWrites)
            {
                for (FRFGPassHandle Writer : ResourceState.LastWriters)
                {
                    AddDependency(Writer, ERFGHazardType::WAW);
                }

                for (FRFGPassHandle Reader : ResourceState.LastReaders)
                {
                    AddDependency(Reader, ERFGHazardType::WAR);
                }

                ResourceState.LastReaders.Clear();
                ResourceState.LastWriters.Clear();
                ResourceState.LastWriters.PushBack(CompiledPass.Handle);
                continue;
            }

            if (bReads)
            {
                for (FRFGPassHandle Writer : ResourceState.LastWriters)
                {
                    AddDependency(Writer, ERFGHazardType::RAW);
                }

                ResourceState.LastReaders.PushBack(CompiledPass.Handle);
            }
        }
    }

    for (FRFGCompiledPass& CompiledPass : CompiledPasses)
    {
        uint32 DependencyLevel = 0;
        for (const FRFGDependencyEdge& Edge : CompiledPass.IncomingEdges)
        {
            const uint32* const SourceIndex = PassIndexById.Find(Edge.SourcePass.Id);
            if (SourceIndex == nullptr)
            {
                continue;
            }

            const uint32 SourceLevel = CompiledPasses[*SourceIndex].DependencyLevel;
            DependencyLevel = std::max(DependencyLevel, SourceLevel + 1);
        }

        CompiledPass.DependencyLevel = DependencyLevel;
    }

    LE::HashMap<uint32, LE::Array<uint32>> OutgoingEdgesByPassId;
    LE::Array<uint32> InDegree;
    LE::Array<uint32> StableDependencyLevels;
    LE::Array<uint32> InsertionOrders;
    InDegree.Resize(CompiledPasses.Size(), 0);
    StableDependencyLevels.Resize(CompiledPasses.Size(), 0);
    InsertionOrders.Resize(CompiledPasses.Size(), 0);

    for (uint32 PassIndex = 0; PassIndex < CompiledPasses.Size(); ++PassIndex)
    {
        InsertionOrders[PassIndex] = CompiledPasses[PassIndex].Handle.Id;
        InDegree[PassIndex] = static_cast<uint32>(CompiledPasses[PassIndex].IncomingEdges.Size());

        for (const FRFGDependencyEdge& Edge : CompiledPasses[PassIndex].IncomingEdges)
        {
            if (PassIndexById.Contains(Edge.SourcePass.Id))
            {
                LE::Array<uint32>* Targets = OutgoingEdgesByPassId.Find(Edge.SourcePass.Id);
                if (Targets == nullptr)
                {
                    OutgoingEdgesByPassId.Insert(Edge.SourcePass.Id, LE::Array<uint32>{});
                    Targets = OutgoingEdgesByPassId.Find(Edge.SourcePass.Id);
                }
                Targets->PushBack(PassIndex);
            }
        }
    }

    LE::Array<uint32> AvailablePassIndices;
    for (uint32 PassIndex = 0; PassIndex < CompiledPasses.Size(); ++PassIndex)
    {
        if (InDegree[PassIndex] == 0)
        {
            AvailablePassIndices.PushBack(PassIndex);
        }
    }

    auto StableSortAvailable = [&]()
    {
        if (AvailablePassIndices.Size() < 2) { return; }
        std::sort(
            AvailablePassIndices.Data(),
            AvailablePassIndices.Data() + AvailablePassIndices.Size(),
            [&](uint32 LhsIndex, uint32 RhsIndex)
            {
                FCompiledPassOrderKey LhsKey;
                LhsKey.DependencyLevel = StableDependencyLevels[LhsIndex];
                LhsKey.InsertionOrder = InsertionOrders[LhsIndex];
                LhsKey.Name = CompiledPasses[LhsIndex].Name;

                FCompiledPassOrderKey RhsKey;
                RhsKey.DependencyLevel = StableDependencyLevels[RhsIndex];
                RhsKey.InsertionOrder = InsertionOrders[RhsIndex];
                RhsKey.Name = CompiledPasses[RhsIndex].Name;

                return IsPassOrderLess(LhsKey, RhsKey);
            });
    };

    StableSortAvailable();

    LE::Array<FRFGCompiledPass> OrderedPasses;
    OrderedPasses.Reserve(CompiledPasses.Size());

    while (!AvailablePassIndices.IsEmpty())
    {
        const uint32 CurrentIndex = AvailablePassIndices.Front();
        AvailablePassIndices.Erase(0);

        CompiledPasses[CurrentIndex].DependencyLevel = StableDependencyLevels[CurrentIndex];
        OrderedPasses.PushBack(CompiledPasses[CurrentIndex]);

        const LE::Array<uint32>* const OutgoingEdges = OutgoingEdgesByPassId.Find(CompiledPasses[CurrentIndex].Handle.Id);
        if (OutgoingEdges == nullptr)
        {
            continue;
        }

        for (uint32 TargetIndex : *OutgoingEdges)
        {
            StableDependencyLevels[TargetIndex] = std::max(
                StableDependencyLevels[TargetIndex],
                StableDependencyLevels[CurrentIndex] + 1);

            if (InDegree[TargetIndex] == 0)
            {
                continue;
            }

            --InDegree[TargetIndex];
            if (InDegree[TargetIndex] == 0)
            {
                AvailablePassIndices.PushBack(TargetIndex);
            }
        }

        StableSortAvailable();
    }

    if (OrderedPasses.Size() != CompiledPasses.Size())
    {
        LE::Array<uint32> RemainingPassIndices;
        for (uint32 PassIndex = 0; PassIndex < CompiledPasses.Size(); ++PassIndex)
        {
            const FRFGPassHandle PassHandle = CompiledPasses[PassIndex].Handle;
            bool bAlreadyOrdered = false;
            for (const FRFGCompiledPass& OrderedPass : OrderedPasses)
            {
                if (OrderedPass.Handle == PassHandle) { bAlreadyOrdered = true; break; }
            }

            if (!bAlreadyOrdered)
            {
                RemainingPassIndices.PushBack(PassIndex);
            }
        }

        if (RemainingPassIndices.Size() > 1) std::sort(
            RemainingPassIndices.Data(),
            RemainingPassIndices.Data() + RemainingPassIndices.Size(),
            [&](uint32 LhsIndex, uint32 RhsIndex)
            {
                FCompiledPassOrderKey LhsKey;
                LhsKey.DependencyLevel = StableDependencyLevels[LhsIndex];
                LhsKey.InsertionOrder = InsertionOrders[LhsIndex];
                LhsKey.Name = CompiledPasses[LhsIndex].Name;

                FCompiledPassOrderKey RhsKey;
                RhsKey.DependencyLevel = StableDependencyLevels[RhsIndex];
                RhsKey.InsertionOrder = InsertionOrders[RhsIndex];
                RhsKey.Name = CompiledPasses[RhsIndex].Name;

                return IsPassOrderLess(LhsKey, RhsKey);
            });

        for (uint32 RemainingIndex : RemainingPassIndices)
        {
            CompiledPasses[RemainingIndex].DependencyLevel = StableDependencyLevels[RemainingIndex];
            OrderedPasses.PushBack(CompiledPasses[RemainingIndex]);
        }
    }

    CompiledPasses = std::move(OrderedPasses);
}

} // namespace LE
