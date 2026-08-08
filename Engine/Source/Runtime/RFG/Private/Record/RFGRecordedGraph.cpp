#include "Record/RFGRecordedGraph.h"

#include <cassert>

namespace LE
{

FRFGPassHandle FRFGRecordedGraph::AddPassNode(const FRFGPassNode& InPassNode)
{
    FRFGPassNode PassNode = InPassNode;
    PassNode.Handle.Id = NextPassId++;

    PassNodes.PushBack(PassNode);
    PassOrder.PushBack(PassNode.Handle);
    return PassNode.Handle;
}

FRFGResourceHandle FRFGRecordedGraph::AddResourceNode(const FRFGResourceNode& InResourceNode)
{
    FRFGResourceNode ResourceNode = InResourceNode;
    ResourceNode.Handle.Id = NextResourceId++;

    ResourceNodes.PushBack(ResourceNode);
    return ResourceNode.Handle;
}

FRFGPassNode& FRFGRecordedGraph::GetPassNode(FRFGPassHandle PassHandle)
{
    assert(PassHandle.IsValid() && PassHandle.Id < PassNodes.Size());
    return PassNodes[PassHandle.Id];
}

FRFGResourceNode& FRFGRecordedGraph::GetResourceNode(FRFGResourceHandle ResourceHandle)
{
    assert(ResourceHandle.IsValid() && ResourceHandle.Id < ResourceNodes.Size());
    return ResourceNodes[ResourceHandle.Id];
}

const FRFGPassNode& FRFGRecordedGraph::GetPassNode(FRFGPassHandle PassHandle) const
{
    assert(PassHandle.IsValid() && PassHandle.Id < PassNodes.Size());
    return PassNodes[PassHandle.Id];
}

const FRFGResourceNode& FRFGRecordedGraph::GetResourceNode(FRFGResourceHandle ResourceHandle) const
{
    assert(ResourceHandle.IsValid() && ResourceHandle.Id < ResourceNodes.Size());
    return ResourceNodes[ResourceHandle.Id];
}

void FRFGRecordedGraph::MarkOutput(FRFGResourceHandle ResourceHandle)
{
    if (ResourceHandle.IsValid())
    {
        OutputResources.Insert(ResourceHandle);
    }
}

bool FRFGRecordedGraph::IsOutputResource(FRFGResourceHandle ResourceHandle) const
{
    return OutputResources.Contains(ResourceHandle);
}

const LE::Array<FRFGPassNode>& FRFGRecordedGraph::GetPassNodes() const
{
    return PassNodes;
}

const LE::Array<FRFGResourceNode>& FRFGRecordedGraph::GetResourceNodes() const
{
    return ResourceNodes;
}

const LE::Array<FRFGPassHandle>& FRFGRecordedGraph::GetPassOrder() const
{
    return PassOrder;
}

void FRFGRecordedGraph::Reset()
{
    NextPassId = 0;
    NextResourceId = 0;
    PassNodes.Clear();
    ResourceNodes.Clear();
    PassOrder.Clear();
    OutputResources.Clear();
}

} // namespace LE
