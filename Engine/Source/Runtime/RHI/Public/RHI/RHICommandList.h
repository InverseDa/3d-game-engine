#pragma once

#include "CoreMinimal.h"
#include "RHIBuffer.h"
#include "RHIResource.h"
#include "RHIPipeline.h"
#include "RHIDescription.h"


namespace LE
{

class FRHIBuffer;
class FRHIPipeline_Graphics;
class FRHIBindGroup;
class FRHITextureView;

// This upload operation writes a whole tightly packed color image at mip 0,
// layer 0. It is deliberately limited to one-layer, one-mip RGBA8 textures.
inline bool IsTextureUploadDestination(const FRHITextureDesc& Desc) noexcept
{
    return Desc.bIsTransferDestination && Desc.bIsShaderResource && !Desc.bIsDepthStencil &&
        Desc.Width != 0 && Desc.Height != 0 && Desc.Depth == 1 &&
        Desc.MipLevels == 1 && Desc.ArrayLayers == 1 &&
        (Desc.Format == EPixelFormat::R8G8B8A8_UNORM || Desc.Format == EPixelFormat::R8G8B8A8_SRGB);
}

inline bool IsBufferToTexture2DCopyValid(
    const FRHIBufferDesc& Source, const FRHITextureDesc& Destination, uint64 SourceOffset = 0) noexcept
{
    constexpr uint64 TexelBytes = 4;
    if (!IsTextureUploadDestination(Destination) ||
        (Source.UsageFlag & static_cast<uint32>(EBufferUsageFlags::TransferSrc)) == 0 ||
        SourceOffset > Source.Size || SourceOffset % TexelBytes != 0)
    {
        return false;
    }
    const uint64 TexelCount = static_cast<uint64>(Destination.Width) * Destination.Height;
    return TexelCount <= (Source.Size - SourceOffset) / TexelBytes;
}

struct FRHIViewport
{
    float X = 0.0f;
    float Y = 0.0f;
    float Width = 0.0f;
    float Height = 0.0f;
    float MinDepth = 0.0f;
    float MaxDepth = 1.0f;
};

struct FRHIScissorRect
{
    int32 X = 0;
    int32 Y = 0;
    int32 Width = 0;
    int32 Height = 0;
};

class RHI_API FRHICommandList : public FRHIResource
{
public:
    virtual ~FRHICommandList() = default;

public:
    virtual void Begin() = 0;
    virtual void End() = 0;

    virtual void ResourceBarriers(const FRHIBarrierBatch& Barriers) = 0;

    // Checked upload recording is optional for backends. A supported command
    // list records one complete Undefined -> CopyDestination -> ShaderResource
    // (Pixel) sequence outside rendering before EndChecked().
    virtual bool BeginChecked() { return false; }
    virtual bool EndChecked() { return false; }
    virtual bool TextureUploadBarrier(FRHITexture*, ERHIResourceState, ERHIResourceState) { return false; }
    virtual bool CopyBufferToTexture2D(FRHIBuffer*, FRHITexture*, uint64 = 0) { return false; }

public:
    virtual void BeginRenderPass(const FRHIRenderPassDesc& Desc) = 0;
    virtual void EndRenderPass() = 0;

public:
    virtual void SetGraphicsPipeline(FRHIPipeline_Graphics* Pipeline) = 0;
    virtual void SetViewport(const FRHIViewport& Viewport) = 0;
    virtual void SetScissorRect(const FRHIScissorRect& Scissor) = 0;

public:
    virtual void SetVertexBuffer(uint32 Slot, FRHIBuffer* Buffer, uint64 Offset = 0) = 0;
    virtual void SetIndexBuffer(FRHIBuffer* Buffer, uint64 Offset = 0, EPixelFormat IndexFormat = EPixelFormat::R32_UINT) = 0;
    // 0: Global, 1: Pass, 2: Material, 3: Object
    virtual void SetBindGroup(uint32 SetIndex, FRHIBindGroup* BindGroup) = 0;

public:
    virtual void SetPushConstants(EShaderStage Stage, const void* Data, uint32 Size) = 0;

public:
    virtual void Draw(uint32 VertexCount, uint32 InstanceCount = 1, uint32 FirstInstance = 0) = 0;
    virtual void DrawIndexed(uint32 IndexCount, uint32 InstanceCount = 1, uint32 FirstIndex = 0, int32 VertexOffset = 0, uint32 FirstInstance = 0) = 0;
};

} // namespace LE
