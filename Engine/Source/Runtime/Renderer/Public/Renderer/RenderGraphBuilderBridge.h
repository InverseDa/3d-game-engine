#pragma once

#include "CoreMinimal.h"
#include "RFGMinimal.h"

#include <unordered_map>

class FRALBuffer;
class FRALTexture;

class RENDERER_API FRenderGraphBuilderBridge final : public FNonCopyable
{
public:
    explicit FRenderGraphBuilderBridge(FRFGBuilder& InBuilder);
    ~FRenderGraphBuilderBridge();

public:
    FRFGResourceHandle ImportTexture(const FString& ResourceName, FRALTexture* Texture);
    FRFGResourceHandle ImportBuffer(const FString& ResourceName, FRALBuffer* Buffer);
    void Read(FRFGPassHandle PassHandle, FRFGResourceHandle ResourceHandle, const FRFGAccessDesc& AccessDesc = {});
    void Write(FRFGPassHandle PassHandle, FRFGResourceHandle ResourceHandle, const FRFGAccessDesc& AccessDesc = {});
    void MarkOutput(FRFGResourceHandle ResourceHandle);

public:
    FRFGBuilder& GetGraphBuilder();
    const FRFGBuilder& GetGraphBuilder() const;

private:
    FRFGBuilder* Builder = nullptr;
    std::unordered_map<const FRALTexture*, FRFGResourceHandle> ImportedTextures;
    std::unordered_map<const FRALBuffer*, FRFGResourceHandle> ImportedBuffers;
};
