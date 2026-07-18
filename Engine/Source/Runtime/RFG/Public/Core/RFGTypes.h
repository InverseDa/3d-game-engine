#pragma once

#include "CoreMinimal.h"
#include "RAL/RALTypes.h"
#include "Core/RFGHandles.h"

#include <functional>

class FRFGPassContext;

enum class ERFGQueueType : uint8
{
    Graphics,
    Compute,
    Transfer,
};

enum class ERFGResourceType : uint8
{
    Texture,
    Buffer,
};

enum class ERFGAccessType : uint8
{
    None,
    Read,
    Write,
    ReadWrite,
};

enum class ERFGHazardType : uint8
{
    RAW,
    WAR,
    WAW,
};

enum class ERFGPipelineStage : uint8
{
    None      = 0,
    Graphics  = 1 << 0,
    Compute   = 1 << 1,
    Copy      = 1 << 2,
    Host      = 1 << 3,
    All       = 0xFF,
};

enum class ERFGPassFlags : uint32
{
    None                   = 0,
    HasSideEffects         = 1 << 0,
    NeverCull              = 1 << 1,
    AsyncComputeCandidate  = 1 << 2,
};

inline ERFGPassFlags operator|(ERFGPassFlags Lhs, ERFGPassFlags Rhs)
{
    return static_cast<ERFGPassFlags>(static_cast<uint32>(Lhs) | static_cast<uint32>(Rhs));
}

inline bool EnumHasAnyFlags(ERFGPassFlags Value, ERFGPassFlags Flags)
{
    return (static_cast<uint32>(Value) & static_cast<uint32>(Flags)) != 0;
}

enum class ERFGResourceFlags : uint32
{
    None        = 0,
    Imported    = 1 << 0,
    External    = 1 << 1,
    Persistent  = 1 << 2,
};

inline ERFGResourceFlags operator|(ERFGResourceFlags Lhs, ERFGResourceFlags Rhs)
{
    return static_cast<ERFGResourceFlags>(static_cast<uint32>(Lhs) | static_cast<uint32>(Rhs));
}

inline bool EnumHasAnyFlags(ERFGResourceFlags Value, ERFGResourceFlags Flags)
{
    return (static_cast<uint32>(Value) & static_cast<uint32>(Flags)) != 0;
}

using FRFGPassCallback = std::function<void(FRFGPassContext&)>;

struct FRFGTextureDesc
{
    uint32 Width = 1;
    uint32 Height = 1;
    uint32 Depth = 1;
    uint32 MipLevels = 1;
    uint32 ArrayLayers = 1;
    EPixelFormat Format = EPixelFormat::Unknown;
    uint32 UsageMask = 0;
};

struct FRFGBufferDesc
{
    uint64 Size = 0;
    uint64 Stride = 0;
    EResourceUsage Usage = EResourceUsage::Local;
    uint32 UsageMask = 0;
};

struct FRFGResourceDesc
{
    ERFGResourceType Type = ERFGResourceType::Texture;
    FRFGTextureDesc Texture;
    FRFGBufferDesc Buffer;
};

struct FRFGAccessDesc
{
    ERFGAccessType Access = ERFGAccessType::Read;
    // Read/Write is used for dependency analysis. State describes the concrete
    // RAL usage/layout required while the pass executes.
    ERALResourceState State = ERALResourceState::Unknown;
    EShaderStage ShaderStage = EShaderStage::AllStage;
    ERFGPipelineStage PipelineStage = ERFGPipelineStage::Graphics;

    uint32 BaseMipLevel = 0;
    uint32 MipCount = 1;
    uint32 BaseArrayLayer = 0;
    uint32 LayerCount = 1;
};

struct FRFGPassParameterBlock
{
    const void* Data = nullptr;
    uint32 DataSize = 0;
    uint32 Revision = 0;
};

struct FRFGCompileOptions
{
    bool bEnablePassCulling = true;
    bool bEnableBarrierElision = true;
    bool bEnableStateMerging = true;
    bool bEnablePlanCache = true;
    bool bDeterministicSort = true;
};

struct FRFGGraphSignature
{
    uint64 Value = 0;
};

struct FRFGSourceLocation
{
    const char* File = "";
    uint32 Line = 0;
};
