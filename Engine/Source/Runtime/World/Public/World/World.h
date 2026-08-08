#pragma once

#include "CoreMinimal.h"
#include "Renderer/RenderScene.h"

namespace LE
{

class FRALBuffer;
class FRALPipeline_Graphics;

struct FWorldPrimitive
{
    LE::String DebugName;
};

struct FWorldMeshComponent : public FWorldPrimitive
{
    LE::FRALPipeline_Graphics* GraphicsPipeline = nullptr;
    LE::FRALBuffer* VertexBuffer = nullptr;
    uint32 VertexCount = 0;
    LE::ERenderMeshPassMask PassMask = LE::ERenderMeshPassMask::All;
    uint64 SortKey = 0;
};

class WORLD_API FWorld
{
public:
    void Reset()
    {
        MeshComponents.Clear();
    }

public:
    LE::Array<FWorldMeshComponent> MeshComponents;
};

} // namespace LE
