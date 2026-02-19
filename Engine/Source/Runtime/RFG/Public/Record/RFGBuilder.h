#pragma once

#include "CoreMinimal.h"
#include "Core/RFGBlackboard.h"
#include "Core/RFGTypes.h"
#include "Record/RFGPassRegistry.h"
#include "Record/RFGRecordedGraph.h"

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
    FRFGResourceHandle CreateTexture(const FString& ResourceName, const FRFGTextureDesc& Desc);
    FRFGResourceHandle CreateBuffer(const FString& ResourceName, const FRFGBufferDesc& Desc);
    FRFGResourceHandle ImportTexture(const FString& ResourceName, FRALTexture* ExternalTexture);
    FRFGResourceHandle ImportBuffer(const FString& ResourceName, FRALBuffer* ExternalBuffer);

public:
    FRFGPassHandle AddPass(
        const FString& PassName,
        const FString& PassTypeName,
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
