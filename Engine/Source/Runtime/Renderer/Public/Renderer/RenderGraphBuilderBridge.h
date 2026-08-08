#pragma once

#include "CoreMinimal.h"
#include "RFGMinimal.h"


namespace LE
{

class FRALBuffer;
class FRALTexture;

class RENDERER_API FRenderGraphBuilderBridge final : public FNonCopyable
{
public:
    explicit FRenderGraphBuilderBridge(LE::FRFGBuilder& InBuilder);
    ~FRenderGraphBuilderBridge();

public:
    LE::FRFGResourceHandle ImportTexture(const LE::String& ResourceName, LE::FRALTexture* Texture, LE::ERALResourceState InitialState);
    LE::FRFGResourceHandle ImportBuffer(const LE::String& ResourceName, LE::FRALBuffer* Buffer, LE::ERALResourceState InitialState);
    void Read(LE::FRFGPassHandle PassHandle, LE::FRFGResourceHandle ResourceHandle, const LE::FRFGAccessDesc& AccessDesc = {});
    void Write(LE::FRFGPassHandle PassHandle, LE::FRFGResourceHandle ResourceHandle, const LE::FRFGAccessDesc& AccessDesc = {});
    void MarkOutput(LE::FRFGResourceHandle ResourceHandle);

public:
    LE::FRFGBuilder& GetGraphBuilder();
    const LE::FRFGBuilder& GetGraphBuilder() const;

private:
    LE::FRFGBuilder* Builder = nullptr;
    LE::HashMap<const LE::FRALTexture*, LE::FRFGResourceHandle> ImportedTextures;
    LE::HashMap<const LE::FRALBuffer*, LE::FRFGResourceHandle> ImportedBuffers;
};

} // namespace LE
