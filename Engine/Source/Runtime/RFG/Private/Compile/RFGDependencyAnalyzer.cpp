#include "Compile/RFGDependencyAnalyzer.h"

#include "Compile/RFGCompiledPlan.h"
#include "Record/RFGRecordedGraph.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
    std::vector<FRFGPassHandle> LastReaders;
    std::vector<FRFGPassHandle> LastWriters;
};

struct FCompiledPassOrderKey
{
    uint32 DependencyLevel = 0;
    uint32 InsertionOrder = 0;
    FString Name;
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

    return std::string(Lhs.Name.GetData()) < std::string(Rhs.Name.GetData());
}
}

void FRFGDependencyAnalyzer::BuildDependencies(const FRFGRecordedGraph& RecordedGraph, FRFGCompiledPlan& OutPlan) const
{
    OutPlan.Clear();

    std::vector<FRFGCompiledPass>& CompiledPasses = OutPlan.GetMutablePasses();
    CompiledPasses.reserve(RecordedGraph.GetPassOrder().size());

    std::unordered_map<uint32, uint32> PassIndexById;
    for (const FRFGPassHandle PassHandle : RecordedGraph.GetPassOrder())
    {
        const FRFGPassNode& PassNode = RecordedGraph.GetPassNode(PassHandle);

        FRFGCompiledPass CompiledPass;
        CompiledPass.Handle = PassHandle;
        CompiledPass.Name = PassNode.Name;
        CompiledPass.Queue = PassNode.Queue;
        CompiledPass.Flags = PassNode.Flags;

        PassIndexById.emplace(PassHandle.Id, static_cast<uint32>(CompiledPasses.size()));
        CompiledPasses.push_back(CompiledPass);
    }

    std::unordered_map<uint32, FResourceDependencyState> DependencyStateByResource;
    std::unordered_set<uint64> DependencyKeys;

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
            if (!DependencyKeys.insert(DependencyKey).second)
            {
                return;
            }

            FRFGDependencyEdge Edge;
            Edge.SourcePass = SourcePass;
            Edge.TargetPass = CompiledPass.Handle;
            Edge.Hazard = HazardType;
            CompiledPass.IncomingEdges.push_back(Edge);
        };

        for (const FRFGPassHandle ExplicitDependency : PassNode.ExplicitDependencies)
        {
            AddDependency(ExplicitDependency, ERFGHazardType::RAW);
        }

        for (const FRFGPassResourceAccess& ResourceAccess : PassNode.ResourceAccesses)
        {
            FResourceDependencyState& ResourceState = DependencyStateByResource[ResourceAccess.Resource.Id];
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

                ResourceState.LastReaders.clear();
                ResourceState.LastWriters.clear();
                ResourceState.LastWriters.push_back(CompiledPass.Handle);
                continue;
            }

            if (bReads)
            {
                for (FRFGPassHandle Writer : ResourceState.LastWriters)
                {
                    AddDependency(Writer, ERFGHazardType::RAW);
                }

                ResourceState.LastReaders.push_back(CompiledPass.Handle);
            }
        }
    }

    for (FRFGCompiledPass& CompiledPass : CompiledPasses)
    {
        uint32 DependencyLevel = 0;
        for (const FRFGDependencyEdge& Edge : CompiledPass.IncomingEdges)
        {
            const auto SourceIndexIt = PassIndexById.find(Edge.SourcePass.Id);
            if (SourceIndexIt == PassIndexById.end())
            {
                continue;
            }

            const uint32 SourceLevel = CompiledPasses[SourceIndexIt->second].DependencyLevel;
            DependencyLevel = std::max(DependencyLevel, SourceLevel + 1);
        }

        CompiledPass.DependencyLevel = DependencyLevel;
    }

    std::unordered_map<uint32, std::vector<uint32>> OutgoingEdgesByPassId;
    std::vector<uint32> InDegree(CompiledPasses.size(), 0);
    std::vector<uint32> StableDependencyLevels(CompiledPasses.size(), 0);
    std::vector<uint32> InsertionOrders(CompiledPasses.size(), 0);

    for (uint32 PassIndex = 0; PassIndex < CompiledPasses.size(); ++PassIndex)
    {
        InsertionOrders[PassIndex] = CompiledPasses[PassIndex].Handle.Id;
        InDegree[PassIndex] = static_cast<uint32>(CompiledPasses[PassIndex].IncomingEdges.size());

        for (const FRFGDependencyEdge& Edge : CompiledPasses[PassIndex].IncomingEdges)
        {
            const auto SourceIndexIt = PassIndexById.find(Edge.SourcePass.Id);
            if (SourceIndexIt != PassIndexById.end())
            {
                OutgoingEdgesByPassId[Edge.SourcePass.Id].push_back(PassIndex);
            }
        }
    }

    std::vector<uint32> AvailablePassIndices;
    for (uint32 PassIndex = 0; PassIndex < CompiledPasses.size(); ++PassIndex)
    {
        if (InDegree[PassIndex] == 0)
        {
            AvailablePassIndices.push_back(PassIndex);
        }
    }

    auto StableSortAvailable = [&]()
    {
        std::sort(
            AvailablePassIndices.begin(),
            AvailablePassIndices.end(),
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

    std::vector<FRFGCompiledPass> OrderedPasses;
    OrderedPasses.reserve(CompiledPasses.size());

    while (!AvailablePassIndices.empty())
    {
        const uint32 CurrentIndex = AvailablePassIndices.front();
        AvailablePassIndices.erase(AvailablePassIndices.begin());

        CompiledPasses[CurrentIndex].DependencyLevel = StableDependencyLevels[CurrentIndex];
        OrderedPasses.push_back(CompiledPasses[CurrentIndex]);

        const auto OutgoingEdgesIt = OutgoingEdgesByPassId.find(CompiledPasses[CurrentIndex].Handle.Id);
        if (OutgoingEdgesIt == OutgoingEdgesByPassId.end())
        {
            continue;
        }

        for (uint32 TargetIndex : OutgoingEdgesIt->second)
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
                AvailablePassIndices.push_back(TargetIndex);
            }
        }

        StableSortAvailable();
    }

    if (OrderedPasses.size() != CompiledPasses.size())
    {
        std::vector<uint32> RemainingPassIndices;
        for (uint32 PassIndex = 0; PassIndex < CompiledPasses.size(); ++PassIndex)
        {
            const FRFGPassHandle PassHandle = CompiledPasses[PassIndex].Handle;
            const bool bAlreadyOrdered = std::find_if(
                OrderedPasses.begin(),
                OrderedPasses.end(),
                [&](const FRFGCompiledPass& OrderedPass)
                {
                    return OrderedPass.Handle == PassHandle;
                }) != OrderedPasses.end();

            if (!bAlreadyOrdered)
            {
                RemainingPassIndices.push_back(PassIndex);
            }
        }

        std::sort(
            RemainingPassIndices.begin(),
            RemainingPassIndices.end(),
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
            OrderedPasses.push_back(CompiledPasses[RemainingIndex]);
        }
    }

    CompiledPasses = std::move(OrderedPasses);
}
