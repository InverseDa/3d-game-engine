#pragma once

#include "Asset/RenderAssetTypes.h"
#include "RAL/RALTypes.h"
#include "Renderer/RenderScene.h"

namespace LE
{

class FRALBuffer;
class FRALPipeline_Graphics;

// Renderer owns the complete GPU-facing definitions of the logical asset
// types declared by Asset. The payloads borrow RAL resources from their
// provider; FAssetRegistry never destroys those resources directly.
struct FMeshAsset
{
    FRALBuffer* VertexBuffer = nullptr;
    uint32 VertexCount = 0;
    FRALBuffer* IndexBuffer = nullptr;
    uint32 IndexCount = 0;
    EPixelFormat IndexFormat = EPixelFormat::Unknown;
    FRenderBounds LocalBounds;
};

struct FMaterialAsset
{
    FRALPipeline_Graphics* GraphicsPipeline = nullptr;
    ERenderMeshPassMask PassMask = ERenderMeshPassMask::All;
    // BasePass is opaque: RGB may be HDR but must be finite and non-negative;
    // alpha is intentionally ignored and emitted as one.
    Math::Vector4f BaseColor{1.0f};
};

} // namespace LE
