#pragma once

#include "CoreMinimal.h"
#include "Reflection/ReflectionMacros.h"
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

LE_CLASS("50000000-0000-4000-8000-000000000001")
class WORLD_API FWorld
{
    LE_GENERATED_BODY()

public:
    void Reset()
    {
        MeshComponents.Clear();
    }

public:
    LE::Array<FWorldMeshComponent> MeshComponents;
};

} // namespace LE
