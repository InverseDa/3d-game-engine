#pragma once

#include "CoreMinimal.h"
#include "RFGMinimal.h"

namespace LE
{

class FRHIBuffer;
class FRHITexture;

class RENDERER_API FRenderGraphBuilderBridge final : public FNonCopyable
{
public:
    explicit FRenderGraphBuilderBridge(LE::FRFGBuilder& InBuilder);
    ~FRenderGraphBuilderBridge();

public:
    LE::FRFGResourceHandle ImportTexture(const LE::String& ResourceName, LE::FRHITexture* Texture, LE::ERHIResourceState InitialState);
    LE::FRFGResourceHandle ImportBuffer(const LE::String& ResourceName, LE::FRHIBuffer* Buffer, LE::ERHIResourceState InitialState);
    void Read(LE::FRFGPassHandle PassHandle, LE::FRFGResourceHandle ResourceHandle, const LE::FRFGAccessDesc& AccessDesc = {});
    void Write(LE::FRFGPassHandle PassHandle, LE::FRFGResourceHandle ResourceHandle, const LE::FRFGAccessDesc& AccessDesc = {});
    void MarkOutput(LE::FRFGResourceHandle ResourceHandle);

public:
    LE::FRFGBuilder& GetGraphBuilder();
    const LE::FRFGBuilder& GetGraphBuilder() const;

private:
    LE::FRFGBuilder* Builder = nullptr;
    LE::HashMap<const LE::FRHITexture*, LE::FRFGResourceHandle> ImportedTextures;
    LE::HashMap<const LE::FRHIBuffer*, LE::FRFGResourceHandle> ImportedBuffers;
};

} // namespace LE
