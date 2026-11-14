#pragma once

#include "Asset/RenderAssetTypes.h"
#include "RHI/RHITypes.h"
#include "Renderer/RenderScene.h"

namespace LE
{

class FRHIBuffer;
class FRHIPipeline_Graphics;
class FRHIBindGroup;
class FRHITexture;

// Renderer owns the complete GPU-facing definitions of the logical asset
// types declared by Asset. The payloads borrow RHI resources from their
// provider; FAssetRegistry never destroys those resources directly.
struct FMeshAsset
{
    FRHIBuffer* VertexBuffer = nullptr;
    uint32 VertexCount = 0;
    FRHIBuffer* IndexBuffer = nullptr;
    uint32 IndexCount = 0;
    EPixelFormat IndexFormat = EPixelFormat::Unknown;
    FRenderBounds LocalBounds;
    bool bHasUV = false;
};

struct FMaterialAsset
{
    FRHIPipeline_Graphics* GraphicsPipeline = nullptr;
    ERenderMeshPassMask PassMask = ERenderMeshPassMask::All;
    // BasePass is opaque: RGB may be HDR but must be finite and non-negative;
    // alpha is intentionally ignored and emitted as one.
    Math::Vector4f BaseColor{1.0f};
    FRHITexture* BaseColorTexture = nullptr;
    FRHIBindGroup* MaterialBindGroup = nullptr;
    bool bRequiresUV = false;
};

} // namespace LE
