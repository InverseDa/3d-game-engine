#include "World/WorldRenderSceneExtractor.h"

#include "Renderer/RenderScene.h"
#include "World/World.h"

namespace LE
{

void FWorldRenderSceneExtractor::ExtractRenderScene(const FWorld& World, LE::FRenderScene& OutRenderScene)
{
    OutRenderScene.Reset();
    const TComponentStorage<FWorldMeshComponent>* const MeshStorage = World.GetMeshComponents();
    if (MeshStorage == nullptr)
    {
        return;
    }
    const Span<const FWorldMeshComponent> MeshComponents = MeshStorage->GetComponents();
    const Span<const FEntity> MeshEntities = MeshStorage->GetEntities();
    if (MeshComponents.Size() != MeshEntities.Size())
    {
        HandleContractViolation(
            "World mesh Entity/component spans have different sizes", __FILE__, __LINE__);
    }
    if (!World.UpdateTransforms())
    {
        HandleContractViolation(
            "World transform cache could not be updated for extraction", __FILE__, __LINE__);
    }
    OutRenderScene.Meshes.Reserve(MeshComponents.Size());

    for (std::size_t Index = 0; Index < MeshComponents.Size(); ++Index)
    {
        const FWorldMeshComponent& MeshComponent = MeshComponents[Index];
        const FEntity MeshEntity = MeshEntities[Index];
        if (!World.IsAlive(MeshEntity))
        {
            HandleContractViolation("World mesh storage contains a dead Entity", __FILE__, __LINE__);
        }
        LE::FRenderMeshProxy& RenderMesh = OutRenderScene.Meshes.EmplaceBack();
        if (World.HasTransform(MeshEntity) &&
            !World.TryGetWorldMatrix(MeshEntity, RenderMesh.LocalToWorld))
        {
            HandleContractViolation(
                "World mesh Transform query failed during extraction", __FILE__, __LINE__);
        }
        RenderMesh.DebugName = MeshComponent.DebugName;
        RenderMesh.GraphicsPipeline = MeshComponent.GraphicsPipeline;
        RenderMesh.VertexBuffer = MeshComponent.VertexBuffer;
        RenderMesh.VertexCount = MeshComponent.VertexCount;
        RenderMesh.PassMask = MeshComponent.PassMask;
        RenderMesh.SortKey = MeshComponent.SortKey;
    }
}

} // namespace LE
