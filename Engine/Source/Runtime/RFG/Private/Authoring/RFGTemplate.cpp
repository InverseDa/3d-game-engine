#include "Authoring/RFGTemplate.h"

uint32 FRFGTemplate::GetVersion() const
{
    return Version;
}

void FRFGTemplate::SetVersion(uint32 InVersion)
{
    Version = InVersion;
}

const std::vector<FRFGTemplateResource>& FRFGTemplate::GetResources() const
{
    return Resources;
}

const std::vector<FRFGTemplateNode>& FRFGTemplate::GetNodes() const
{
    return Nodes;
}

const std::vector<FRFGTemplateEdge>& FRFGTemplate::GetEdges() const
{
    return Edges;
}

std::vector<FRFGTemplateResource>& FRFGTemplate::GetMutableResources()
{
    return Resources;
}

std::vector<FRFGTemplateNode>& FRFGTemplate::GetMutableNodes()
{
    return Nodes;
}

std::vector<FRFGTemplateEdge>& FRFGTemplate::GetMutableEdges()
{
    return Edges;
}

void FRFGTemplate::Clear()
{
    Version = 1;
    Resources.clear();
    Nodes.clear();
    Edges.clear();
}
