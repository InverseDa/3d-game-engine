#include "Renderer/RenderGraphBuilderBridge.h"

FRenderGraphBuilderBridge::FRenderGraphBuilderBridge(FRFGBuilder& InBuilder)
    : Builder(&InBuilder)
{
}

FRenderGraphBuilderBridge::~FRenderGraphBuilderBridge() = default;

FRFGResourceHandle FRenderGraphBuilderBridge::ImportTexture(const FString& ResourceName, FRALTexture* Texture)
{
    if (Texture == nullptr)
    {
        return FRFGResourceHandle();
    }

    const auto Found = ImportedTextures.find(Texture);
    if (Found != ImportedTextures.end())
    {
        return Found->second;
    }

    const FRFGResourceHandle Handle = Builder->ImportTexture(ResourceName, Texture);
    ImportedTextures.emplace(Texture, Handle);
    return Handle;
}

FRFGResourceHandle FRenderGraphBuilderBridge::ImportBuffer(const FString& ResourceName, FRALBuffer* Buffer)
{
    if (Buffer == nullptr)
    {
        return FRFGResourceHandle();
    }

    const auto Found = ImportedBuffers.find(Buffer);
    if (Found != ImportedBuffers.end())
    {
        return Found->second;
    }

    const FRFGResourceHandle Handle = Builder->ImportBuffer(ResourceName, Buffer);
    ImportedBuffers.emplace(Buffer, Handle);
    return Handle;
}

void FRenderGraphBuilderBridge::Read(FRFGPassHandle PassHandle, FRFGResourceHandle ResourceHandle, const FRFGAccessDesc& AccessDesc)
{
    Builder->Read(PassHandle, ResourceHandle, AccessDesc);
}

void FRenderGraphBuilderBridge::Write(FRFGPassHandle PassHandle, FRFGResourceHandle ResourceHandle, const FRFGAccessDesc& AccessDesc)
{
    Builder->Write(PassHandle, ResourceHandle, AccessDesc);
}

void FRenderGraphBuilderBridge::MarkOutput(FRFGResourceHandle ResourceHandle)
{
    Builder->MarkOutput(ResourceHandle);
}

FRFGBuilder& FRenderGraphBuilderBridge::GetGraphBuilder()
{
    return *Builder;
}

const FRFGBuilder& FRenderGraphBuilderBridge::GetGraphBuilder() const
{
    return *Builder;
}
