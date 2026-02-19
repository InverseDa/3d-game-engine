#pragma once

#include "CoreMinimal.h"
#include "Core/RFGHandles.h"
#include "Core/RFGTypes.h"

#include <unordered_set>
#include <vector>

class FRALTexture;
class FRALBuffer;
class IRFGPassExecutor;

struct FRFGPassResourceAccess
{
    FRFGResourceHandle Resource;
    FRFGAccessDesc Access;
};

struct FRFGPassNode
{
    FRFGPassHandle Handle;
    FString Name;
    FString PassTypeName;
    ERFGQueueType Queue = ERFGQueueType::Graphics;
    ERFGPassFlags Flags = ERFGPassFlags::None;
    FRFGSourceLocation SourceLocation;

    FRFGPassParameterBlock Parameters;
    FRFGPassCallback ExecuteCallback;
    IRFGPassExecutor* Executor = nullptr;

    std::vector<FRFGPassResourceAccess> ResourceAccesses;
    std::vector<FRFGPassHandle> ExplicitDependencies;
};

struct FRFGResourceNode
{
    FRFGResourceHandle Handle;
    FString Name;
    FRFGResourceDesc Desc;
    ERFGResourceFlags Flags = ERFGResourceFlags::None;

    FRALTexture* ImportedTexture = nullptr;
    FRALBuffer* ImportedBuffer = nullptr;
};

class RFG_API FRFGRecordedGraph
{
public:
    FRFGRecordedGraph() = default;
    ~FRFGRecordedGraph() = default;

public:
    FRFGPassHandle AddPassNode(const FRFGPassNode& InPassNode);
    FRFGResourceHandle AddResourceNode(const FRFGResourceNode& InResourceNode);

public:
    FRFGPassNode& GetPassNode(FRFGPassHandle PassHandle);
    FRFGResourceNode& GetResourceNode(FRFGResourceHandle ResourceHandle);
    const FRFGPassNode& GetPassNode(FRFGPassHandle PassHandle) const;
    const FRFGResourceNode& GetResourceNode(FRFGResourceHandle ResourceHandle) const;

public:
    void MarkOutput(FRFGResourceHandle ResourceHandle);
    bool IsOutputResource(FRFGResourceHandle ResourceHandle) const;

public:
    const std::vector<FRFGPassNode>& GetPassNodes() const;
    const std::vector<FRFGResourceNode>& GetResourceNodes() const;
    const std::vector<FRFGPassHandle>& GetPassOrder() const;

public:
    void Reset();

private:
    uint32 NextPassId = 0;
    uint32 NextResourceId = 0;

    std::vector<FRFGPassNode> PassNodes;
    std::vector<FRFGResourceNode> ResourceNodes;
    std::vector<FRFGPassHandle> PassOrder;
    std::unordered_set<FRFGResourceHandle> OutputResources;
};
