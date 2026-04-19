#pragma once

#include "CoreMinimal.h"
#include "Renderer/RenderScene.h"

#include <vector>

class FRALBuffer;
class FRALPipeline_Graphics;

struct FWorldPrimitive
{
    FString DebugName;
};

struct FWorldMeshComponent : public FWorldPrimitive
{
    FRALPipeline_Graphics* GraphicsPipeline = nullptr;
    FRALBuffer* VertexBuffer = nullptr;
    uint32 VertexCount = 0;
    ERenderMeshPassMask PassMask = ERenderMeshPassMask::All;
    uint64 SortKey = 0;
};

class WORLD_API FWorld
{
public:
    void Reset()
    {
        MeshComponents.clear();
    }

public:
    std::vector<FWorldMeshComponent> MeshComponents;
};
