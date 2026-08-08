#include "Authoring/RFGTemplate.h"

namespace LE
{

uint32 FRFGTemplate::GetVersion() const
{
    return Version;
}

void FRFGTemplate::SetVersion(uint32 InVersion)
{
    Version = InVersion;
}

const LE::Array<FRFGTemplateResource>& FRFGTemplate::GetResources() const
{
    return Resources;
}

const LE::Array<FRFGTemplateNode>& FRFGTemplate::GetNodes() const
{
    return Nodes;
}

const LE::Array<FRFGTemplateEdge>& FRFGTemplate::GetEdges() const
{
    return Edges;
}

LE::Array<FRFGTemplateResource>& FRFGTemplate::GetMutableResources()
{
    return Resources;
}

LE::Array<FRFGTemplateNode>& FRFGTemplate::GetMutableNodes()
{
    return Nodes;
}

LE::Array<FRFGTemplateEdge>& FRFGTemplate::GetMutableEdges()
{
    return Edges;
}

void FRFGTemplate::Clear()
{
    Version = 1;
    Resources.Clear();
    Nodes.Clear();
    Edges.Clear();
}

} // namespace LE
