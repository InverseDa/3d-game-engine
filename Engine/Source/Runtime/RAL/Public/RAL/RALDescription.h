#pragma once

#include "CoreMinimal.h"
#include "RALTypes.h"

class FRALBuffer;
class FRALShader;
class FRALSampler;
class FRALTexture;
class FRALTextureView;
class FRALBindGroupLayout;

enum class EBufferUsageFlags : uint8
{
    None = 0,
    VertexBuffer    = 1 << 0,
    IndexBuffer     = 1 << 1,
    UniformBuffer   = 1 << 2,
    StorageBuffer   = 1 << 3, // Bindless Needed
    IndirectArgs    = 1 << 4,
    TransferSrc     = 1 << 5,
    TransferDst     = 1 << 6,
};

enum class ERALSurfaceType : uint8
{
    Unknown = 0,
    Win32,
    MetalLayer,
};

struct FRALSurfaceDesc
{
    ERALSurfaceType Type = ERALSurfaceType::Unknown;
    void* WindowHandle = nullptr;
    void* ViewHandle = nullptr;
    void* LayerHandle = nullptr;
};

struct FRALBufferDesc
{
    FString Name;
    uint64 Size = 0;
    uint32 Usage;       // EResourceUsage
    uint32 UsageFlag;   // EBufferUsageFlags
};

struct FRALTextureDesc
{
    FString Name;
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
};

struct FRALSwapchainDesc
{
    FRALSurfaceDesc Surface;
    uint32 Width = 0;
    uint32 Height = 0;
    EPixelFormat BackBufferFormat = EPixelFormat::B8G8R8A8_SRGB;
    bool bEnableVsync = true;
};

struct FRALTextureViewDesc
{
    FRALTexture* Texture = nullptr;
    EPixelFormat Format = EPixelFormat::Unknown;
    uint32 MipSlice = 0;
    uint32 ArraySlice = 0;
    uint32 MipLevels = 1;
    uint32 ArrayLayers = 1;
};

// ***********************************************************************************************
// ******************************** Resource Barriers ********************************************
// ***********************************************************************************************

struct FRALTextureBarrierDesc
{
    FRALTexture* Texture = nullptr;
    ERALResourceState BeforeState = ERALResourceState::Unknown;
    ERALResourceState AfterState = ERALResourceState::Unknown;
    EShaderStage BeforeShaderStage = EShaderStage::None;
    EShaderStage AfterShaderStage = EShaderStage::None;

    uint32 BaseMipLevel = 0;
    uint32 MipCount = 1;
    uint32 BaseArrayLayer = 0;
    uint32 LayerCount = 1;
};

struct FRALBufferBarrierDesc
{
    FRALBuffer* Buffer = nullptr;
    ERALResourceState BeforeState = ERALResourceState::Unknown;
    ERALResourceState AfterState = ERALResourceState::Unknown;
    EShaderStage BeforeShaderStage = EShaderStage::None;
    EShaderStage AfterShaderStage = EShaderStage::None;

    uint64 Offset = 0;
    uint64 Size = ~0ull;
};

struct FRALBarrierBatch
{
    std::vector<FRALTextureBarrierDesc> TextureBarriers;
    std::vector<FRALBufferBarrierDesc> BufferBarriers;
};

// ***********************************************************************************************
// ******************************* Vertex Input Layout *******************************************
// ***********************************************************************************************

// 顶点输入布局结构
struct FRALVertexInputBinding
{
    uint32 Binding = 0;
    uint32 Stride = 0;
    bool bPerInstance = false; // false = 逐顶点, true = 逐实例
};

struct FRALVertexInputAttribute
{
    uint32 Location = 0;    // Shader 位置
    uint32 Binding = 0;     // 绑定槽位
    EPixelFormat Format = EPixelFormat::Unknown;
    uint32 Offset = 0;      // 顶点内偏移
};

// ***********************************************************************************************
// ******************************** Render Pass Relative *****************************************
// ***********************************************************************************************

enum class EAttachmentLoadOp : uint8
{
    Load,       // Contain
    Clear,      // Clear particular color
    DontCare,   // Dont care
};

enum class EAttachmentStoreOp : uint8
{
    Store,      // Save the result to GPU memory
    DontCare,   // Discard the result
};

struct FRALColorAttachmentDesc
{
    FRALTextureView* RenderTarget = nullptr;
    EAttachmentLoadOp LoadOp = EAttachmentLoadOp::Clear;
    EAttachmentStoreOp StoreOp = EAttachmentStoreOp::Store;
    float ClearColor[4] = { 0.f, 0.f, 0.f, 1.f };
};

struct FRALDepthStencilAttachmentDesc
{
    FRALTextureView* DepthStencilTarget = nullptr;
    EAttachmentLoadOp LoadOp = EAttachmentLoadOp::Clear;
    EAttachmentStoreOp StoreOp = EAttachmentStoreOp::Store;
    float ClearDepth = 1.f;
    uint8 ClearStencil = 0;
};

struct FRALRenderPassDesc
{
    FRALColorAttachmentDesc ColorAttachments[8];    // Max support 8 MRT
    uint32 ColorAttachmentCount = 0;

    FRALDepthStencilAttachmentDesc DepthStencilAttachment;
    bool bHasDepthStencil = false;
};

// ***********************************************************************************************
// ********************************** Pipeline Relative ******************************************
// ***********************************************************************************************

struct FRALBlendStateDesc
{
    bool bEnable = false;
    // TODO: kodak
};

struct FRALDepthStencilStateDesc
{
    bool bDepthTestEnable = true;
    bool bDepthWriteEnable = true;
    ECompareFunction DepthFunc = ECompareFunction::Less;
};

struct FRALRasterizerStateDesc
{
    ECullMode CullMode = ECullMode::Back;
    EFillMode FillMode = EFillMode::Solid;
    bool bFrontCounterClockwise = false;  // true: CCW is the right side (OpenGL Default); false: CW is the right side (DirectX Default)
};

struct FRALPipelineDesc_Graphics
{
    FString Name;

    // Shader
    FRALShader* VertexShader = nullptr;
    FRALShader* PixelShader = nullptr;

    // Vertex Input Layout
    std::vector<FRALVertexInputBinding> VertexBindings;
    std::vector<FRALVertexInputAttribute> VertexAttributes;

    // State
    FRALBlendStateDesc BlendState;
    FRALDepthStencilStateDesc DepthStencilState;
    FRALRasterizerStateDesc RasterizerState;

    // BindGroup Layouts
    std::vector<FRALBindGroupLayout*> BindGroupLayouts;

    // Render Target Format
    EPixelFormat RenderTargetFormats[8];
    uint32 RenderTargetCount = 0;
    EPixelFormat DepthStencilFormat = EPixelFormat::Unknown;
};

// ***********************************************************************************************
// ************************************ Shader Relative ******************************************
// ***********************************************************************************************

struct FRALShaderResourceBinding
{
    uint32 Set = 0;
    uint32 Binding = 0;
    uint32 Count = 1;
    EShaderResourceType Type = EShaderResourceType::UniformBuffer;
    EShaderStage StageFlags = EShaderStage::None;
};

struct FRALShaderDesc
{
    FString Name;
    EShaderStage Stage = EShaderStage::None;
    std::vector<FRALShaderResourceBinding> Bindings;
    const void* ByteCode = nullptr;
    uint64 ByteCodeSize = 0;
    FString EntryPoint = "main";
};

// ***********************************************************************************************
// ********************************** Bind Group / Layout ****************************************
// ***********************************************************************************************

enum class ERALBindGroupItemType : uint8
{
    UniformBuffer,
    StorageBuffer,
    SampledImage,
    StorageImage,
    Sampler,
    CombinedImageSampler,
};

struct FRALBindGroupLayoutItem
{
    uint32 Binding = 0;
    uint32 Count = 1;
    ERALBindGroupItemType Type = ERALBindGroupItemType::UniformBuffer;
    EShaderStage StageFlags = EShaderStage::None;
};

struct FRALBindGroupLayoutDesc
{
    FString Name;
    uint32 SetIndex = 0; // set in a shader
    std::vector<FRALBindGroupLayoutItem> Bindings;
};

struct FRALBindGroupItem
{
    uint32 Binding = 0;

    FRALBuffer* Buffer = nullptr;
    FRALTextureView* TextureView = nullptr;
    FRALSampler* Sampler = nullptr;

    uint64 Offset = 0;
    uint64 Range  = 0;
};

struct FRALBindGroupDesc
{
    FString Name;
    FRALBindGroupLayout* Layout = nullptr;
    std::vector<FRALBindGroupItem> Items;
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

struct FRALSamplerDesc
{
    FString Name;

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

    float BorderColor[4] = { 0.f, 0.f, 0.f, 1.f };
};
