#pragma once

#include "CoreMinimal.h"

#define LE_RAL_ENABLE_VALIDATION 1

enum class ERALPlatform : uint8
{
    Unknown,
    OpenGL,
    D3D11,
    D3D12,
    Vulkan,
};

enum class EPixelFormat : uint8
{
    Unknown = 0,

    // 8-bit
    R8_UNORM,
    R8_SNORM,
    R8_UINT,
    R8_SINT,

    // 32-bit
    R32_UINT,
    R8G8B8A8_UNORM,
    R8G8B8A8_SNORM,
    R8G8B8A8_UINT,
    R8G8B8A8_SINT,

    // SRGB
    R8G8B8A8_SRGB,
    B8G8R8A8_SRGB,

    // Float
    R16G16_FLOAT,
    R16G16B16A16_FLOAT,
    R32_FLOAT,
    R32G32_FLOAT,
    R32G32B32_FLOAT,
    R32G32B32A32_FLOAT,

    // Depth / Stencil
    D32_FLOAT,
    D24_UNORM_S8_UINT,

    MAX,
};

enum class EShaderStage : uint8
{
    None        = 0,
    Vertex      = 1 << 0,
    Pixel       = 1 << 1,
    Compute     = 1 << 2,
    Geometry    = 1 << 3,
    Hull        = 1 << 4,
    Domain      = 1 << 5,
    Graphics    = Vertex | Pixel | Geometry | Hull | Domain,
    AllStage    = Graphics | Compute,
};

inline EShaderStage operator|(EShaderStage A, EShaderStage B)
{
    return static_cast<EShaderStage>(static_cast<uint8>(A) | static_cast<uint8>(B));
}

inline bool EnumHasAnyFlags(EShaderStage Value, EShaderStage Flags)
{
    return (static_cast<uint8>(Value) & static_cast<uint8>(Flags)) != 0;
}

enum class EResourceUsage : uint8
{
    Local,      // GPU local memory only, the fastest access, CPU has not accessed to read
    Upload,     // CPU write -> GPU read, usually used form uniform buffer and dynamic vertex buffer
    Readback,   // GPU write -> CPU read, usually used for readback buffer
};

enum class ECullMode : uint8
{
    None,
    Front,
    Back,
};

enum class EFillMode : uint8
{
    Solid,
    Wireframe,
};

enum class ECompareFunction : uint8
{
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always,
};
