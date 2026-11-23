#pragma once

#include "CoreMinimal.h"
#include "Platform/PlatformSurface.h"
#include "RHITypes.h"

namespace LE
{

class FRHIBuffer;
class FRHIShader;
class FRHISampler;
class FRHITexture;
class FRHITextureView;
class FRHIBindGroupLayout;

enum class EBufferUsageFlags : uint8
{
    None = 0,
    VertexBuffer = 1 << 0,
    IndexBuffer = 1 << 1,
    UniformBuffer = 1 << 2,
    StorageBuffer = 1 << 3, // Bindless Needed
    IndirectArgs = 1 << 4,
    TransferSrc = 1 << 5,
    TransferDst = 1 << 6,
};

struct FRHIBufferDesc
{
    LE::String Name;
    uint64 Size = 0;
    uint32 Usage = 0;     // EResourceUsage
    uint32 UsageFlag = 0; // EBufferUsageFlags
};

struct FRHITextureDesc
{
    LE::String Name;
    uint32 Width = 1;
    uint32 Height = 1;
    uint32 Depth = 1;
    uint32 MipLevels = 1;
    uint32 ArrayLayers = 1;

    EPixelFormat Format = EPixelFormat::R8G8B8A8_UNORM;

    bool bIsUAV = false;
    bool bIsRenderTarget = false;
    bool bIsDepthStencil = false;
    bool bIsShaderResource = false;
    bool bIsTransferDestination = false;
};

struct FRHISwapchainDesc
{
    FPlatformSurface Surface;
    uint32 Width = 0;
    uint32 Height = 0;
    EPixelFormat BackBufferFormat = EPixelFormat::B8G8R8A8_SRGB;
    bool bEnableVsync = true;
};

struct FRHITextureViewDesc
{
    FRHITexture* Texture = nullptr;
    EPixelFormat Format = EPixelFormat::Unknown;
    uint32 MipSlice = 0;
    uint32 ArraySlice = 0;
    uint32 MipLevels = 1;
    uint32 ArrayLayers = 1;
};

// ***********************************************************************************************
// ******************************** Resource Barriers ********************************************
// ***********************************************************************************************

struct FRHITextureBarrierDesc
{
    FRHITexture* Texture = nullptr;
    ERHIResourceState BeforeState = ERHIResourceState::Unknown;
    ERHIResourceState AfterState = ERHIResourceState::Unknown;
    EShaderStage BeforeShaderStage = EShaderStage::None;
    EShaderStage AfterShaderStage = EShaderStage::None;

    uint32 BaseMipLevel = 0;
    uint32 MipCount = 1;
    uint32 BaseArrayLayer = 0;
    uint32 LayerCount = 1;
};

struct FRHIBufferBarrierDesc
{
    FRHIBuffer* Buffer = nullptr;
    ERHIResourceState BeforeState = ERHIResourceState::Unknown;
    ERHIResourceState AfterState = ERHIResourceState::Unknown;
    EShaderStage BeforeShaderStage = EShaderStage::None;
    EShaderStage AfterShaderStage = EShaderStage::None;

    uint64 Offset = 0;
    uint64 Size = ~0ull;
};

struct FRHIBarrierBatch
{
    LE::Array<FRHITextureBarrierDesc> TextureBarriers;
    LE::Array<FRHIBufferBarrierDesc> BufferBarriers;
};

// ***********************************************************************************************
// ******************************* Vertex Input Layout *******************************************
// ***********************************************************************************************

// 顶点输入布局结构
struct FRHIVertexInputBinding
{
    uint32 Binding = 0;
    uint32 Stride = 0;
    bool bPerInstance = false; // false = 逐顶点, true = 逐实例
};

struct FRHIVertexInputAttribute
{
    uint32 Location = 0; // Shader 位置
    uint32 Binding = 0;  // 绑定槽位
    EPixelFormat Format = EPixelFormat::Unknown;
    uint32 Offset = 0; // 顶点内偏移
};

// ***********************************************************************************************
// ******************************** Render Pass Relative *****************************************
// ***********************************************************************************************

enum class EAttachmentLoadOp : uint8
{
    Load,     // Contain
    Clear,    // Clear particular color
    DontCare, // Dont care
};

enum class EAttachmentStoreOp : uint8
{
    Store,    // Save the result to GPU memory
    DontCare, // Discard the result
};

struct FRHIColorAttachmentDesc
{
    FRHITextureView* RenderTarget = nullptr;
    EAttachmentLoadOp LoadOp = EAttachmentLoadOp::Clear;
    EAttachmentStoreOp StoreOp = EAttachmentStoreOp::Store;
    float ClearColor[4] = {0.f, 0.f, 0.f, 1.f};
};

struct FRHIDepthStencilAttachmentDesc
{
    FRHITextureView* DepthStencilTarget = nullptr;
    EAttachmentLoadOp LoadOp = EAttachmentLoadOp::Clear;
    EAttachmentStoreOp StoreOp = EAttachmentStoreOp::Store;
    float ClearDepth = 1.f;
    uint8 ClearStencil = 0;
};

struct FRHIRenderAreaDesc
{
    int32 X = 0;
    int32 Y = 0;
    uint32 Width = 0;  // 0 means derive the extent from the attachments.
    uint32 Height = 0; // 0 means derive the extent from the attachments.
};

struct FRHIRenderPassDesc
{
    FRHIRenderAreaDesc RenderArea;

    FRHIColorAttachmentDesc ColorAttachments[8]; // Max support 8 MRT
    uint32 ColorAttachmentCount = 0;

    FRHIDepthStencilAttachmentDesc DepthStencilAttachment;
    bool bHasDepthStencil = false;
};

// ***********************************************************************************************
// ********************************** Pipeline Relative ******************************************
// ***********************************************************************************************

struct FRHIBlendStateDesc
{
    bool bEnable = false;
    // TODO: kodak
};

struct FRHIDepthStencilStateDesc
{
    bool bDepthTestEnable = true;
    bool bDepthWriteEnable = true;
    ECompareFunction DepthFunc = ECompareFunction::Less;
};

// The first renderer slice needs one portable, offset-zero push range. The
// 128-byte cap is Vulkan's guaranteed minimum and keeps the RHI contract
// backend-independent until larger device limits are surfaced explicitly.
struct FRHIPushConstantRangeDesc
{
    EShaderStage StageFlags = EShaderStage::None;
    uint32 Size = 0;

    bool IsValid() const noexcept { return StageFlags != EShaderStage::None && Size > 0 && (Size % 4u) == 0 && Size <= 128; }

    bool IsDisabled() const noexcept { return StageFlags == EShaderStage::None && Size == 0; }

    bool AcceptsWrite(const EShaderStage InStageFlags, const uint32 InSize) const noexcept { return IsValid() && InStageFlags == StageFlags && InSize == Size; }
};

struct FRHIRasterizerStateDesc
{
    ECullMode CullMode = ECullMode::Back;
    EFillMode FillMode = EFillMode::Solid;
    bool bFrontCounterClockwise = false; // true: CCW is the right side (OpenGL Default); false: CW is the right side (DirectX Default)
};

struct FRHIPipelineDesc_Graphics
{
    LE::String Name;

    // Shader
    FRHIShader* VertexShader = nullptr;
    FRHIShader* PixelShader = nullptr;

    // Vertex Input Layout
    LE::Array<FRHIVertexInputBinding> VertexBindings;
    LE::Array<FRHIVertexInputAttribute> VertexAttributes;

    // State
    FRHIBlendStateDesc BlendState;
    FRHIDepthStencilStateDesc DepthStencilState;
    FRHIRasterizerStateDesc RasterizerState;
    FRHIPushConstantRangeDesc PushConstants;

    // BindGroup Layouts
    LE::Array<FRHIBindGroupLayout*> BindGroupLayouts;

    // Render Target Format
    EPixelFormat RenderTargetFormats[8]{};
    uint32 RenderTargetCount = 0;
    EPixelFormat DepthStencilFormat = EPixelFormat::Unknown;
};

struct FRHIRenderingAttachmentSignature
{
    EPixelFormat RenderTargetFormats[8]{};
    uint32 RenderTargetCount = 0;
    EPixelFormat DepthStencilFormat = EPixelFormat::Unknown;

    void Reset() noexcept
    {
        for (EPixelFormat& Format : RenderTargetFormats)
        {
            Format = EPixelFormat::Unknown;
        }
        RenderTargetCount = 0;
        DepthStencilFormat = EPixelFormat::Unknown;
    }
};

inline bool IsRenderingSignatureCompatible(const FRHIRenderingAttachmentSignature& Signature, const FRHIPipelineDesc_Graphics& Pipeline) noexcept
{
    if (Signature.RenderTargetCount > 8 || Pipeline.RenderTargetCount > 8 || Signature.RenderTargetCount != Pipeline.RenderTargetCount || Signature.DepthStencilFormat != Pipeline.DepthStencilFormat)
    {
        return false;
    }
    for (uint32 Index = 0; Index < Signature.RenderTargetCount; ++Index)
    {
        if (Signature.RenderTargetFormats[Index] != Pipeline.RenderTargetFormats[Index])
        {
            return false;
        }
    }
    return true;
}

// ***********************************************************************************************
// ************************************ Shader Relative ******************************************
// ***********************************************************************************************

struct FRHIShaderResourceBinding
{
    uint32 Set = 0;
    uint32 Binding = 0;
    uint32 Count = 1;
    EShaderResourceType Type = EShaderResourceType::UniformBuffer;
    EShaderStage StageFlags = EShaderStage::None;
};

struct FRHIShaderDesc
{
    LE::String Name;
    EShaderStage Stage = EShaderStage::None;
    LE::Array<FRHIShaderResourceBinding> Bindings;
    const void* ByteCode = nullptr;
    uint64 ByteCodeSize = 0;
    LE::String EntryPoint = "main";
};

// ***********************************************************************************************
// ********************************** Bind Group / Layout ****************************************
// ***********************************************************************************************

enum class ERHIBindGroupItemType : uint8
{
    UniformBuffer,
    StorageBuffer,
    SampledImage,
    StorageImage,
    Sampler,
    CombinedImageSampler,
};

struct FRHIBindGroupLayoutItem
{
    uint32 Binding = 0;
    uint32 Count = 1;
    ERHIBindGroupItemType Type = ERHIBindGroupItemType::UniformBuffer;
    EShaderStage StageFlags = EShaderStage::None;
};

struct FRHIBindGroupLayoutDesc
{
    LE::String Name;
    uint32 SetIndex = 0; // set in a shader
    LE::Array<FRHIBindGroupLayoutItem> Bindings;
};

struct FRHIBindGroupItem
{
    uint32 Binding = 0;

    FRHIBuffer* Buffer = nullptr;
    FRHITextureView* TextureView = nullptr;
    FRHISampler* Sampler = nullptr;

    uint64 Offset = 0;
    uint64 Range = 0;
};

struct FRHIBindGroupDesc
{
    LE::String Name;
    FRHIBindGroupLayout* Layout = nullptr;
    LE::Array<FRHIBindGroupItem> Items;
};

// ***********************************************************************************************
// **************************************** Sampler **********************************************
// ***********************************************************************************************

enum class ESamplerFilter : uint8
{
    Nearest,
    Linear,
};

enum class ESamplerMipmapMode : uint8
{
    Nearest,
    Linear,
};

enum class ESamplerAddressMode : uint8
{
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder,
};

struct FRHISamplerDesc
{
    LE::String Name;

    ESamplerFilter MinFilter = ESamplerFilter::Linear;
    ESamplerFilter MagFilter = ESamplerFilter::Linear;
    ESamplerMipmapMode MipmapMode = ESamplerMipmapMode::Linear;

    ESamplerAddressMode AddressU = ESamplerAddressMode::Repeat;
    ESamplerAddressMode AddressV = ESamplerAddressMode::Repeat;
    ESamplerAddressMode AddressW = ESamplerAddressMode::Repeat;

    float MipLODBias = 0.f;
    float MinLOD = 0.f;
    float MaxLOD = 32.f;

    bool bEnableAnisotropic = false;
    float MaxAnisotropy = 1.f;

    bool bEnableCompare = false;
    ECompareFunction CompareFunc = ECompareFunction::LessEqual;

    float BorderColor[4] = {0.f, 0.f, 0.f, 1.f};
};

} // namespace LE
