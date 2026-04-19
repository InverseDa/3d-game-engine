#include "Record/RFGRecordedGraph.h"

#include <cassert>

FRFGPassHandle FRFGRecordedGraph::AddPassNode(const FRFGPassNode& InPassNode)
{
    FRFGPassNode PassNode = InPassNode;
    PassNode.Handle.Id = NextPassId++;

    PassNodes.push_back(PassNode);
    PassOrder.push_back(PassNode.Handle);
    return PassNode.Handle;
}

FRFGResourceHandle FRFGRecordedGraph::AddResourceNode(const FRFGResourceNode& InResourceNode)
{
    FRFGResourceNode ResourceNode = InResourceNode;
    ResourceNode.Handle.Id = NextResourceId++;

    ResourceNodes.push_back(ResourceNode);
    return ResourceNode.Handle;
}

FRFGPassNode& FRFGRecordedGraph::GetPassNode(FRFGPassHandle PassHandle)
{
    assert(PassHandle.IsValid() && PassHandle.Id < PassNodes.size());
    return PassNodes[PassHandle.Id];
}

FRFGResourceNode& FRFGRecordedGraph::GetResourceNode(FRFGResourceHandle ResourceHandle)
{
    assert(ResourceHandle.IsValid() && ResourceHandle.Id < ResourceNodes.size());
    return ResourceNodes[ResourceHandle.Id];
}

const FRFGPassNode& FRFGRecordedGraph::GetPassNode(FRFGPassHandle PassHandle) const
{
    assert(PassHandle.IsValid() && PassHandle.Id < PassNodes.size());
    return PassNodes[PassHandle.Id];
}

const FRFGResourceNode& FRFGRecordedGraph::GetResourceNode(FRFGResourceHandle ResourceHandle) const
{
    assert(ResourceHandle.IsValid() && ResourceHandle.Id < ResourceNodes.size());
    return ResourceNodes[ResourceHandle.Id];
}

void FRFGRecordedGraph::MarkOutput(FRFGResourceHandle ResourceHandle)
{
    if (ResourceHandle.IsValid())
    {
        OutputResources.insert(ResourceHandle);
    }
}

bool FRFGRecordedGraph::IsOutputResource(FRFGResourceHandle ResourceHandle) const
{
    return OutputResources.find(ResourceHandle) != OutputResources.end();
}

const std::vector<FRFGPassNode>& FRFGRecordedGraph::GetPassNodes() const
{
    return PassNodes;
}

const std::vector<FRFGResourceNode>& FRFGRecordedGraph::GetResourceNodes() const
{
    return ResourceNodes;
}

const std::vector<FRFGPassHandle>& FRFGRecordedGraph::GetPassOrder() const
{
    return PassOrder;
}

void FRFGRecordedGraph::Reset()
{
    NextPassId = 0;
    NextResourceId = 0;
    PassNodes.clear();
    ResourceNodes.clear();
    PassOrder.clear();
    OutputResources.clear();
}
