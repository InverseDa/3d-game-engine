#pragma once

#include "CoreMinimal.h"
#include "Core/RFGTypes.h"


namespace LE
{

struct FRFGTemplateResource
{
    uint32 ResourceId = 0xFFFFFFFFu;
    LE::String Name;
    FRFGResourceDesc Desc;
    ERFGResourceFlags Flags = ERFGResourceFlags::None;
};

struct FRFGTemplateNode
{
    uint32 NodeId = 0xFFFFFFFFu;
    LE::String Name;
    LE::String PassTypeName;
    ERFGQueueType Queue = ERFGQueueType::Graphics;
    ERFGPassFlags Flags = ERFGPassFlags::None;
    FRFGPassParameterBlock DefaultParameters;

    LE::Array<uint32> ReadResources;
    LE::Array<uint32> WriteResources;
};

struct FRFGTemplateEdge
{
    uint32 SourceNodeId = 0xFFFFFFFFu;
    uint32 TargetNodeId = 0xFFFFFFFFu;
};

class RFG_API FRFGTemplate
{
public:
    FRFGTemplate() = default;
    ~FRFGTemplate() = default;

public:
    uint32 GetVersion() const;
    void SetVersion(uint32 InVersion);

public:
    const LE::Array<FRFGTemplateResource>& GetResources() const;
    const LE::Array<FRFGTemplateNode>& GetNodes() const;
    const LE::Array<FRFGTemplateEdge>& GetEdges() const;

public:
    LE::Array<FRFGTemplateResource>& GetMutableResources();
    LE::Array<FRFGTemplateNode>& GetMutableNodes();
    LE::Array<FRFGTemplateEdge>& GetMutableEdges();

public:
    void Clear();

private:
    uint32 Version = 1;
    LE::Array<FRFGTemplateResource> Resources;
    LE::Array<FRFGTemplateNode> Nodes;
    LE::Array<FRFGTemplateEdge> Edges;
};

} // namespace LE
