#pragma once

#include "CoreMinimal.h"
#include "RALTypes.h"

class FRALTexture;
class FRALTextureView;

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

struct FRALSamplerDesc
{
    
};

struct FRALSwapchainDesc
{
    void* WindowHandle = nullptr;
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

struct FRALGraphicsPipelineDesc
{
    FString Name;

    // Shader
    FRALShader* VertexShader = nullptr;
    FRALShader* PixelShader = nullptr;

    // State
    FRALBlendStateDesc BlendState;
    FRALDepthStencilStateDesc DepthStencilState;
    FRALRasterizerStateDesc RasterizerState;

    // TODO: kodak input layout

    // Render Target Format
    EPixelFormat RenderTargetFormats[8];
    uint32 RenderTargetCount = 0;
    EPixelFormat DepthStencilFormat = EPixelFormat::Unknown;
};