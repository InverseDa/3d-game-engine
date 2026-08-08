#pragma once

#include "CoreMinimal.h"
#include "Core/RFGHandles.h"
#include "Core/RFGTypes.h"


namespace LE
{

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
    LE::String Name;
    LE::String PassTypeName;
    ERFGQueueType Queue = ERFGQueueType::Graphics;
    ERFGPassFlags Flags = ERFGPassFlags::None;
    FRFGSourceLocation SourceLocation;

    FRFGPassParameterBlock Parameters;
    FRFGPassCallback ExecuteCallback;
    IRFGPassExecutor* Executor = nullptr;

    LE::Array<FRFGPassResourceAccess> ResourceAccesses;
    LE::Array<FRFGPassHandle> ExplicitDependencies;
};

struct FRFGResourceNode
{
    FRFGResourceHandle Handle;
    LE::String Name;
    FRFGResourceDesc Desc;
    ERFGResourceFlags Flags = ERFGResourceFlags::None;
    LE::ERALResourceState InitialState = LE::ERALResourceState::Undefined;

    LE::FRALTexture* ImportedTexture = nullptr;
    LE::FRALBuffer* ImportedBuffer = nullptr;
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
    const LE::Array<FRFGPassNode>& GetPassNodes() const;
    const LE::Array<FRFGResourceNode>& GetResourceNodes() const;
    const LE::Array<FRFGPassHandle>& GetPassOrder() const;

public:
    void Reset();

private:
    uint32 NextPassId = 0;
    uint32 NextResourceId = 0;

    LE::Array<FRFGPassNode> PassNodes;
    LE::Array<FRFGResourceNode> ResourceNodes;
    LE::Array<FRFGPassHandle> PassOrder;
    LE::HashSet<FRFGResourceHandle, FRFGResourceHandleHash> OutputResources;
};

} // namespace LE
