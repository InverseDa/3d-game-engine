#pragma once

#include "CoreMinimal.h"
#include "Core/RFGBlackboard.h"
#include "Core/RFGTypes.h"
#include "Record/RFGPassRegistry.h"
#include "Record/RFGRecordedGraph.h"

namespace LE
{

class FRALTexture;
class FRALBuffer;

struct FRFGBuilderCreateInfo
{
    IRFGPassRegistry* PassRegistry = nullptr;
};

class RFG_API FRFGBuilder final : public FNonCopyable
{
public:
    explicit FRFGBuilder(const FRFGBuilderCreateInfo& InCreateInfo = {});
    ~FRFGBuilder();

public:
    void SetPassRegistry(IRFGPassRegistry* InPassRegistry);
    IRFGPassRegistry* GetPassRegistry() const;

public:
    FRFGResourceHandle CreateTexture(const LE::String& ResourceName, const FRFGTextureDesc& Desc);
    FRFGResourceHandle CreateBuffer(const LE::String& ResourceName, const FRFGBufferDesc& Desc);
    FRFGResourceHandle ImportTexture(const LE::String& ResourceName, LE::FRALTexture* ExternalTexture, LE::ERALResourceState InitialState);
    FRFGResourceHandle ImportBuffer(const LE::String& ResourceName, LE::FRALBuffer* ExternalBuffer, LE::ERALResourceState InitialState);

public:
    FRFGPassHandle AddPass(
        const LE::String& PassName,
        const LE::String& PassTypeName,
        const FRFGPassParameterBlock& ParameterBlock = {},
        ERFGPassFlags Flags = ERFGPassFlags::None,
        ERFGQueueType QueueType = ERFGQueueType::Graphics,
        const FRFGSourceLocation& SourceLocation = {});

    void SetPassCallback(FRFGPassHandle PassHandle, const FRFGPassCallback& PassCallback);

public:
    void Read(FRFGPassHandle PassHandle, FRFGResourceHandle ResourceHandle, const FRFGAccessDesc& AccessDesc = {});
    void Write(FRFGPassHandle PassHandle, FRFGResourceHandle ResourceHandle, const FRFGAccessDesc& AccessDesc = {});
    void AddDependency(FRFGPassHandle BeforePass, FRFGPassHandle AfterPass);

public:
    void MarkOutput(FRFGResourceHandle ResourceHandle);

public:
    FRFGGraphSignature BuildSignature() const;

public:
    FRFGRecordedGraph& GetRecordedGraph();
    const FRFGRecordedGraph& GetRecordedGraph() const;
    FRFGBlackboard& GetBlackboard();
    const FRFGBlackboard& GetBlackboard() const;

public:
    void Reset();

private:
    IRFGPassRegistry* PassRegistry = nullptr;
    FRFGRecordedGraph RecordedGraph;
    FRFGBlackboard Blackboard;
};

} // namespace LE
