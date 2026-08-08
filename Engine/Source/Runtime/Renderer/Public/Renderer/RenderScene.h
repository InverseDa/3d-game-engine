#pragma once

#include "CoreMinimal.h"


namespace LE
{

class FRALBuffer;
class FRALPipeline_Graphics;

enum class ERenderMeshPassMask : uint8
{
    None = 0,
    BackBuffer = 1 << 0,
    SceneColor = 1 << 1,
    All = BackBuffer | SceneColor,
};

inline constexpr ERenderMeshPassMask operator|(ERenderMeshPassMask Lhs, ERenderMeshPassMask Rhs)
{
    return static_cast<ERenderMeshPassMask>(static_cast<uint8>(Lhs) | static_cast<uint8>(Rhs));
}

inline constexpr bool HasRenderMeshPass(ERenderMeshPassMask Value, ERenderMeshPassMask Flag)
{
    return (static_cast<uint8>(Value) & static_cast<uint8>(Flag)) != 0;
}

struct FRenderPrimitive
{
    LE::String DebugName;
};

struct FRenderMeshProxy : public FRenderPrimitive
{
    LE::FRALPipeline_Graphics* GraphicsPipeline = nullptr;
    LE::FRALBuffer* VertexBuffer = nullptr;
    uint32 VertexCount = 0;
    ERenderMeshPassMask PassMask = ERenderMeshPassMask::All;
    uint64 SortKey = 0;
};

struct FRenderLightProxy : public FRenderPrimitive
{
};

struct FRenderCameraProxy : public FRenderPrimitive
{
};

class RENDERER_API FRenderScene
{
public:
    void Reset()
    {
        Meshes.Clear();
        Lights.Clear();
        Cameras.Clear();
    }

public:
    LE::Array<FRenderMeshProxy> Meshes;
    LE::Array<FRenderLightProxy> Lights;
    LE::Array<FRenderCameraProxy> Cameras;
};

} // namespace LE
