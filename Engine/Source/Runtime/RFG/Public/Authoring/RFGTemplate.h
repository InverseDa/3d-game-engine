#pragma once

#include "CoreMinimal.h"
#include "Core/RFGTypes.h"

#include <vector>

struct FRFGTemplateResource
{
    uint32 ResourceId = 0xFFFFFFFFu;
    FString Name;
    FRFGResourceDesc Desc;
    ERFGResourceFlags Flags = ERFGResourceFlags::None;
};

struct FRFGTemplateNode
{
    uint32 NodeId = 0xFFFFFFFFu;
    FString Name;
    FString PassTypeName;
    ERFGQueueType Queue = ERFGQueueType::Graphics;
    ERFGPassFlags Flags = ERFGPassFlags::None;
    FRFGPassParameterBlock DefaultParameters;

    std::vector<uint32> ReadResources;
    std::vector<uint32> WriteResources;
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
    const std::vector<FRFGTemplateResource>& GetResources() const;
    const std::vector<FRFGTemplateNode>& GetNodes() const;
    const std::vector<FRFGTemplateEdge>& GetEdges() const;

public:
    std::vector<FRFGTemplateResource>& GetMutableResources();
    std::vector<FRFGTemplateNode>& GetMutableNodes();
    std::vector<FRFGTemplateEdge>& GetMutableEdges();

public:
    void Clear();

private:
    uint32 Version = 1;
    std::vector<FRFGTemplateResource> Resources;
    std::vector<FRFGTemplateNode> Nodes;
    std::vector<FRFGTemplateEdge> Edges;
};
