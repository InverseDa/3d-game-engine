#pragma once

#include "CoreMinimal.h"
#include "RALTypes.h"

class FRALTexture;

struct FRALBufferDesc
{
    FString Name;
    uint64 Size = 0;
    EResourceUsage HeapType = EResourceUsage::Upload;

    bool bIsVertextBuffer = false;
    bool bIsIndexBuffer = false;
    bool bIsUniformBuffer = false;
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

struct FRALGraphicsPipelineDesc
{
    
};