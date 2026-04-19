#include "World/WorldRenderSceneExtractor.h"

#include "Renderer/RenderScene.h"
#include "World/World.h"

void FWorldRenderSceneExtractor::ExtractRenderScene(const FWorld& World, FRenderScene& OutRenderScene)
{
    OutRenderScene.Reset();
    OutRenderScene.Meshes.reserve(World.MeshComponents.size());

    for (const FWorldMeshComponent& MeshComponent : World.MeshComponents)
    {
        FRenderMeshProxy& RenderMesh = OutRenderScene.Meshes.emplace_back();
        RenderMesh.DebugName = MeshComponent.DebugName;
        RenderMesh.GraphicsPipeline = MeshComponent.GraphicsPipeline;
        RenderMesh.VertexBuffer = MeshComponent.VertexBuffer;
        RenderMesh.VertexCount = MeshComponent.VertexCount;
        RenderMesh.PassMask = MeshComponent.PassMask;
        RenderMesh.SortKey = MeshComponent.SortKey;
    }
}
