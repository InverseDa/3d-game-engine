#include "Renderer/RenderGraphBuilderBridge.h"

namespace LE
{

FRenderGraphBuilderBridge::FRenderGraphBuilderBridge(LE::FRFGBuilder& InBuilder)
    : Builder(&InBuilder)
{
}

FRenderGraphBuilderBridge::~FRenderGraphBuilderBridge() = default;

LE::FRFGResourceHandle FRenderGraphBuilderBridge::ImportTexture(const LE::String& ResourceName, LE::FRALTexture* Texture, LE::ERALResourceState InitialState)
{
    if (Texture == nullptr)
    {
        return LE::FRFGResourceHandle();
    }

    const LE::FRFGResourceHandle* const Found = ImportedTextures.Find(Texture);
    if (Found != nullptr)
    {
        return *Found;
    }

    const LE::FRFGResourceHandle Handle = Builder->ImportTexture(ResourceName, Texture, InitialState);
    const LE::FRALTexture* const TextureKey = Texture;
    ImportedTextures.Insert(TextureKey, Handle);
    return Handle;
}

LE::FRFGResourceHandle FRenderGraphBuilderBridge::ImportBuffer(const LE::String& ResourceName, LE::FRALBuffer* Buffer, LE::ERALResourceState InitialState)
{
    if (Buffer == nullptr)
    {
        return LE::FRFGResourceHandle();
    }

    const LE::FRFGResourceHandle* const Found = ImportedBuffers.Find(Buffer);
    if (Found != nullptr)
    {
        return *Found;
    }

    const LE::FRFGResourceHandle Handle = Builder->ImportBuffer(ResourceName, Buffer, InitialState);
    const LE::FRALBuffer* const BufferKey = Buffer;
    ImportedBuffers.Insert(BufferKey, Handle);
    return Handle;
}

void FRenderGraphBuilderBridge::Read(LE::FRFGPassHandle PassHandle, LE::FRFGResourceHandle ResourceHandle, const LE::FRFGAccessDesc& AccessDesc)
{
    Builder->Read(PassHandle, ResourceHandle, AccessDesc);
}

void FRenderGraphBuilderBridge::Write(LE::FRFGPassHandle PassHandle, LE::FRFGResourceHandle ResourceHandle, const LE::FRFGAccessDesc& AccessDesc)
{
    Builder->Write(PassHandle, ResourceHandle, AccessDesc);
}

void FRenderGraphBuilderBridge::MarkOutput(LE::FRFGResourceHandle ResourceHandle)
{
    Builder->MarkOutput(ResourceHandle);
}

LE::FRFGBuilder& FRenderGraphBuilderBridge::GetGraphBuilder()
{
    return *Builder;
}

const LE::FRFGBuilder& FRenderGraphBuilderBridge::GetGraphBuilder() const
{
    return *Builder;
}

} // namespace LE
