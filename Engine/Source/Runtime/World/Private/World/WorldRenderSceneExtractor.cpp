#include "World/WorldRenderSceneExtractor.h"

#include "Renderer/RenderScene.h"
#include "World/World.h"

namespace LE
{

void FWorldRenderSceneExtractor::ExtractRenderScene(const FWorld& World, LE::FRenderScene& OutRenderScene)
{
    OutRenderScene.Reset();
    OutRenderScene.Meshes.Reserve(World.MeshComponents.Size());

    for (const FWorldMeshComponent& MeshComponent : World.MeshComponents)
    {
        LE::FRenderMeshProxy& RenderMesh = OutRenderScene.Meshes.EmplaceBack();
        RenderMesh.DebugName = MeshComponent.DebugName;
        RenderMesh.GraphicsPipeline = MeshComponent.GraphicsPipeline;
        RenderMesh.VertexBuffer = MeshComponent.VertexBuffer;
        RenderMesh.VertexCount = MeshComponent.VertexCount;
        RenderMesh.PassMask = MeshComponent.PassMask;
        RenderMesh.SortKey = MeshComponent.SortKey;
    }
}

} // namespace LE
