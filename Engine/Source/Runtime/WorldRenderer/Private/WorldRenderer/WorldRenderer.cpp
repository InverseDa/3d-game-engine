#include "WorldRenderer/WorldRenderer.h"

#include "Renderer/RenderAssets.h"
#include "World/DirectionalLight.h"
#include "World/World.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace LE
{
namespace
{

FRenderPrimitiveId MakePrimitiveId(const FEntity Entity) noexcept
{
    FRenderPrimitiveId Result;
    Result.Scope = Entity.WorldDomain;
    Result.Value = (static_cast<uint64>(Entity.GetGeneration()) << 32) | static_cast<uint64>(Entity.GetIndex());
    return Result;
}

bool PrimitiveIdLess(const FRenderPrimitiveId& Left, const FRenderPrimitiveId& Right) noexcept
{
    return Left.Scope != Right.Scope ? Left.Scope < Right.Scope : Left.Value < Right.Value;
}

bool IsFiniteMatrix(const Math::Matrix4f& Matrix) noexcept
{
    for (const float Value : Matrix.Values)
    {
        if (!std::isfinite(Value))
        {
            return false;
        }
    }
    return true;
}

bool IsFiniteNonNegativeRgb(const Math::Vector4f& Color) noexcept
{
    for (std::size_t Component = 0; Component < 3; ++Component)
    {
        if (!std::isfinite(Color.Values[Component]) || Color.Values[Component] < 0.0f)
        {
            return false;
        }
    }
    return true;
}

bool TryTransformBounds(const FRenderBounds& LocalBounds, const Math::Matrix4f& LocalToWorld, FRenderBounds& OutWorldBounds) noexcept
{
    if (!LocalBounds.IsValid() || !IsFiniteMatrix(LocalToWorld))
    {
        return false;
    }

    const Math::Vector3f Center = (LocalBounds.Min + LocalBounds.Max) * 0.5f;
    const Math::Vector3f Extent = (LocalBounds.Max - LocalBounds.Min) * 0.5f;
    Math::Vector3f WorldCenter;
    Math::Vector3f WorldExtent;
    for (std::size_t Row = 0; Row < 3; ++Row)
    {
        WorldCenter.Values[Row] = LocalToWorld.Values[Row * 4 + 3];
        WorldExtent.Values[Row] = 0.0f;
        for (std::size_t Column = 0; Column < 3; ++Column)
        {
            const float Coefficient = LocalToWorld.Values[Row * 4 + Column];
            WorldCenter.Values[Row] += Coefficient * Center.Values[Column];
            WorldExtent.Values[Row] += std::fabs(Coefficient) * Extent.Values[Column];
        }
    }

    const FRenderBounds Candidate(WorldCenter - WorldExtent, WorldCenter + WorldExtent);
    if (!Candidate.IsValid())
    {
        return false;
    }
    OutWorldBounds = Candidate;
    return true;
}

bool NearlyEqual(const float Left, const float Right, const float Tolerance) noexcept
{
    return std::fabs(Left - Right) <= Tolerance;
}

bool AreCameraParametersValid(const FWorldCameraComponent& Camera) noexcept
{
    constexpr float Pi = 3.14159265358979323846f;
    return std::isfinite(Camera.VerticalFieldOfViewRadians) && std::isfinite(Camera.NearPlane) && std::isfinite(Camera.FarPlane) && Camera.VerticalFieldOfViewRadians > 0.0f && Camera.VerticalFieldOfViewRadians < Pi && Camera.NearPlane > 0.0f && Camera.FarPlane > Camera.NearPlane;
}

bool TryBuildViewMatrix(const Math::Matrix4f& CameraToWorld, Math::Matrix4f& OutView) noexcept
{
    if (!IsFiniteMatrix(CameraToWorld))
    {
        return false;
    }

    constexpr float RigidTolerance = 1.0e-4f;
    if (!NearlyEqual(CameraToWorld(3, 0), 0.0f, RigidTolerance) || !NearlyEqual(CameraToWorld(3, 1), 0.0f, RigidTolerance) || !NearlyEqual(CameraToWorld(3, 2), 0.0f, RigidTolerance) || !NearlyEqual(CameraToWorld(3, 3), 1.0f, RigidTolerance))
    {
        return false;
    }

    Math::Vector3f Basis[3];
    for (std::size_t Column = 0; Column < 3; ++Column)
    {
        Basis[Column] = Math::Vector3f(CameraToWorld(0, Column), CameraToWorld(1, Column), CameraToWorld(2, Column));
        if (!NearlyEqual(Basis[Column].LengthSquared(), 1.0f, RigidTolerance))
        {
            return false;
        }
    }
    if (!NearlyEqual(Math::Vector3f::Dot(Basis[0], Basis[1]), 0.0f, RigidTolerance) || !NearlyEqual(Math::Vector3f::Dot(Basis[0], Basis[2]), 0.0f, RigidTolerance) || !NearlyEqual(Math::Vector3f::Dot(Basis[1], Basis[2]), 0.0f, RigidTolerance) || !NearlyEqual(Math::Vector3f::Dot(Math::Vector3f::Cross(Basis[0], Basis[1]), Basis[2]), 1.0f, RigidTolerance))
    {
        return false;
    }

    const Math::Vector3f Translation(CameraToWorld(0, 3), CameraToWorld(1, 3), CameraToWorld(2, 3));
    Math::Matrix4f View = Math::Matrix4f::Identity();
    for (std::size_t Row = 0; Row < 3; ++Row)
    {
        for (std::size_t Column = 0; Column < 3; ++Column)
        {
            View(Row, Column) = CameraToWorld(Column, Row);
        }
        View(Row, 3) = -Math::Vector3f::Dot(Basis[Row], Translation);
    }

    if (!IsFiniteMatrix(View))
    {
        return false;
    }
    OutView = View;
    return true;
}

bool TryBuildProjectionMatrix(const FWorldCameraComponent& Camera, const uint32 ViewportWidth, const uint32 ViewportHeight, Math::Matrix4f& OutProjection) noexcept
{
    if (!AreCameraParametersValid(Camera))
    {
        return false;
    }
    const float Aspect = static_cast<float>(ViewportWidth) / static_cast<float>(ViewportHeight);
    const float TanHalfFov = std::tan(Camera.VerticalFieldOfViewRadians * 0.5f);
    if (!std::isfinite(Aspect) || Aspect <= 0.0f || !std::isfinite(TanHalfFov) || TanHalfFov <= 0.0f)
    {
        return false;
    }

    // Right-handed, local -Z forward. Vulkan clip depth is [0, 1]. The
    // negative Y scale explicitly compensates for a positive-height Vulkan
    // viewport so authored +Y remains screen-up.
    Math::Matrix4f Projection;
    Projection(0, 0) = 1.0f / (Aspect * TanHalfFov);
    Projection(1, 1) = -1.0f / TanHalfFov;
    Projection(2, 2) = Camera.FarPlane / (Camera.NearPlane - Camera.FarPlane);
    Projection(2, 3) = (Camera.FarPlane * Camera.NearPlane) / (Camera.NearPlane - Camera.FarPlane);
    Projection(3, 2) = -1.0f;

    if (!IsFiniteMatrix(Projection))
    {
        return false;
    }
    OutProjection = Projection;
    return true;
}

enum class EFrustumBoundsResult : uint8
{
    Visible,
    Culled,
    Invalid,
};

EFrustumBoundsResult TestBoundsAgainstFrustum(const FRenderBounds& Bounds, const Math::Matrix4f& ViewProjection) noexcept
{
    bool bOutsideLeft = true;
    bool bOutsideRight = true;
    bool bOutsideBottom = true;
    bool bOutsideTop = true;
    bool bOutsideNear = true;
    bool bOutsideFar = true;
    for (uint32 Corner = 0; Corner < 8; ++Corner)
    {
        const Math::Vector4f WorldPosition((Corner & 1u) != 0 ? Bounds.Max.Values[0] : Bounds.Min.Values[0], (Corner & 2u) != 0 ? Bounds.Max.Values[1] : Bounds.Min.Values[1], (Corner & 4u) != 0 ? Bounds.Max.Values[2] : Bounds.Min.Values[2], 1.0f);
        const Math::Vector4f Clip = ViewProjection * WorldPosition;
        for (const float Value : Clip.Values)
        {
            if (!std::isfinite(Value))
            {
                return EFrustumBoundsResult::Invalid;
            }
        }
        bOutsideLeft &= Clip.Values[0] < -Clip.Values[3];
        bOutsideRight &= Clip.Values[0] > Clip.Values[3];
        bOutsideBottom &= Clip.Values[1] < -Clip.Values[3];
        bOutsideTop &= Clip.Values[1] > Clip.Values[3];
        bOutsideNear &= Clip.Values[2] < 0.0f;
        bOutsideFar &= Clip.Values[2] > Clip.Values[3];
    }
    return bOutsideLeft || bOutsideRight || bOutsideBottom || bOutsideTop || bOutsideNear || bOutsideFar ? EFrustumBoundsResult::Culled : EFrustumBoundsResult::Visible;
}

bool MeshProxyLess(const FRenderMeshProxy& Left, const FRenderMeshProxy& Right) noexcept
{
    if (Left.SortKey != Right.SortKey)
    {
        return Left.SortKey < Right.SortKey;
    }
    if (Left.MaterialAssetId != Right.MaterialAssetId)
    {
        return Left.MaterialAssetId < Right.MaterialAssetId;
    }
    return PrimitiveIdLess(Left.PrimitiveId, Right.PrimitiveId);
}

} // namespace

FWorldRenderSnapshot::FWorldRenderSnapshot() noexcept : FWorldRenderSnapshot(GetDefaultAllocator()) {}

FWorldRenderSnapshot::FWorldRenderSnapshot(IAllocator& InAllocator) noexcept : Allocator(&InAllocator), RenderScene(InAllocator) {}

FWorldRenderSnapshot::FWorldRenderSnapshot(FWorldRenderSnapshot&& Other) noexcept : Allocator(Other.Allocator), RenderScene(std::move(Other.RenderScene)), AuthoredPrimaryCameraPose(Other.AuthoredPrimaryCameraPose), FrameIndex(Other.FrameIndex), bPublished(Other.bPublished)
{
    Other.Allocator = &Other.RenderScene.GetAllocator();
    Other.AuthoredPrimaryCameraPose = {};
    Other.FrameIndex = 0;
    Other.bPublished = false;
}

FWorldRenderSnapshot& FWorldRenderSnapshot::operator=(FWorldRenderSnapshot&& Other) noexcept
{
    if (this != &Other)
    {
        RenderScene = std::move(Other.RenderScene);
        Allocator = Other.Allocator;
        AuthoredPrimaryCameraPose = Other.AuthoredPrimaryCameraPose;
        FrameIndex = Other.FrameIndex;
        bPublished = Other.bPublished;
        Other.Allocator = &Other.RenderScene.GetAllocator();
        Other.AuthoredPrimaryCameraPose = {};
        Other.FrameIndex = 0;
        Other.bPublished = false;
    }
    return *this;
}

void FWorldRenderSnapshot::Reset() noexcept
{
    RenderScene.Reset();
    AuthoredPrimaryCameraPose = {};
    FrameIndex = 0;
    bPublished = false;
}

EWorldRenderSnapshotBuildResult FWorldRenderer::BuildSnapshot(const FWorld& World, const FAssetRegistry& Assets, const FWorldRenderSnapshotBuildContext& Context, FWorldRenderSnapshot& OutSnapshot, FWorldRenderSnapshotBuildReport* const OutReport) const noexcept
{
    FWorldRenderSnapshotBuildReport Report;
    OutSnapshot.Reset();

    auto Fail = [&Report, &OutSnapshot, OutReport](const EWorldRenderSnapshotBuildResult Result, const std::size_t MeshIndex, const FRenderPrimitiveId PrimitiveId = {}, const EAssetResolveResult AssetResult = EAssetResolveResult::Success) noexcept {
        OutSnapshot.Reset();
        Report.Result = Result;
        Report.MeshIndex = MeshIndex;
        Report.PrimitiveId = PrimitiveId;
        Report.AssetResolveResult = AssetResult;
        if (OutReport != nullptr)
        {
            *OutReport = Report;
        }
        return Result;
    };

    if (!World.IsValid())
    {
        return Fail(EWorldRenderSnapshotBuildResult::InvalidWorld, 0);
    }
    if (!Assets.IsValid())
    {
        return Fail(EWorldRenderSnapshotBuildResult::InvalidRegistry, 0);
    }
    if (World.GetTickState() != EWorldTickState::Idle)
    {
        return Fail(EWorldRenderSnapshotBuildResult::WorldNotCommitted, 0);
    }
    if (Context.ViewportWidth == 0 || Context.ViewportHeight == 0)
    {
        return Fail(EWorldRenderSnapshotBuildResult::InvalidViewport, 0);
    }

    const TComponentStorage<FWorldCameraComponent>* const CameraStorage = World.GetCameraComponents();
    const TComponentStorage<FWorldMeshComponent>* const MeshStorage = World.GetMeshComponents();
    const TComponentStorage<FWorldDirectionalLightComponent>* const LightStorage = World.GetDirectionalLightComponents();
    if (CameraStorage == nullptr || MeshStorage == nullptr || LightStorage == nullptr)
    {
        return Fail(EWorldRenderSnapshotBuildResult::InvalidWorld, 0);
    }
    const Span<const FWorldCameraComponent> CameraComponents = CameraStorage->GetComponents();
    const Span<const FEntity> CameraEntities = CameraStorage->GetEntities();
    const Span<const FWorldMeshComponent> MeshComponents = MeshStorage->GetComponents();
    const Span<const FEntity> MeshEntities = MeshStorage->GetEntities();
    const Span<const FWorldDirectionalLightComponent> LightComponents = LightStorage->GetComponents();
    const Span<const FEntity> LightEntities = LightStorage->GetEntities();
    if (CameraComponents.Size() != CameraEntities.Size() || MeshComponents.Size() != MeshEntities.Size() || LightComponents.Size() != LightEntities.Size())
    {
        return Fail(EWorldRenderSnapshotBuildResult::StorageMismatch, 0);
    }
    Report.SourceMeshCount = MeshComponents.Size();
    if (!World.UpdateTransforms())
    {
        return Fail(EWorldRenderSnapshotBuildResult::TransformUpdateFailed, 0);
    }

    std::size_t PrimaryCameraIndex = CameraComponents.Size();
    FRenderPrimitiveId PrimaryCameraId;
    for (std::size_t Index = 0; Index < CameraComponents.Size(); ++Index)
    {
        if (!CameraComponents[Index].bActive)
        {
            continue;
        }
        const FEntity Entity = CameraEntities[Index];
        const FRenderPrimitiveId PrimitiveId = MakePrimitiveId(Entity);
        if (!World.IsAlive(Entity) || !PrimitiveId.IsValid())
        {
            return Fail(EWorldRenderSnapshotBuildResult::StorageMismatch, 0, PrimitiveId);
        }
        if (PrimaryCameraIndex == CameraComponents.Size() || CameraComponents[Index].Priority > CameraComponents[PrimaryCameraIndex].Priority || (CameraComponents[Index].Priority == CameraComponents[PrimaryCameraIndex].Priority && PrimitiveIdLess(PrimitiveId, PrimaryCameraId)))
        {
            PrimaryCameraIndex = Index;
            PrimaryCameraId = PrimitiveId;
        }
    }
    if (PrimaryCameraIndex == CameraComponents.Size())
    {
        return Fail(EWorldRenderSnapshotBuildResult::MissingCamera, 0);
    }
    const FEntity PrimaryCameraEntity = CameraEntities[PrimaryCameraIndex];
    if (!World.HasTransform(PrimaryCameraEntity))
    {
        return Fail(EWorldRenderSnapshotBuildResult::InvalidCameraTransform, 0, PrimaryCameraId);
    }
    Math::Matrix4f CameraToWorld;
    if (!World.TryGetWorldMatrix(PrimaryCameraEntity, CameraToWorld))
    {
        return Fail(EWorldRenderSnapshotBuildResult::InvalidCameraTransform, 0, PrimaryCameraId);
    }
    Math::Matrix4f View;
    Math::Matrix4f Projection;
    Math::Matrix4f ViewProjection;
    if (!TryBuildViewMatrix(CameraToWorld, View))
    {
        return Fail(EWorldRenderSnapshotBuildResult::InvalidCameraTransform, 0, PrimaryCameraId);
    }
    if (Context.CameraOverride.PrimitiveId == PrimaryCameraId && !TryBuildViewMatrix(Context.CameraOverride.CameraToWorld, View))
    {
        return Fail(EWorldRenderSnapshotBuildResult::InvalidCameraTransform, 0, PrimaryCameraId);
    }
    if (!TryBuildProjectionMatrix(CameraComponents[PrimaryCameraIndex], Context.ViewportWidth, Context.ViewportHeight, Projection))
    {
        return Fail(EWorldRenderSnapshotBuildResult::InvalidCamera, 0, PrimaryCameraId);
    }
    ViewProjection = Projection * View;
    if (!IsFiniteMatrix(ViewProjection))
    {
        return Fail(EWorldRenderSnapshotBuildResult::InvalidCamera, 0, PrimaryCameraId);
    }

    FRenderScene Candidate(*OutSnapshot.Allocator);
    if (!Candidate.Cameras.TryReserve(1) || !Candidate.Cameras.TryEmplaceBack(*OutSnapshot.Allocator))
    {
        return Fail(EWorldRenderSnapshotBuildResult::AllocationFailed, 0);
    }
    FRenderCameraProxy& CameraProxy = Candidate.Cameras.Back();
    if (!CameraProxy.DebugName.TryAssign(CameraComponents[PrimaryCameraIndex].DebugName.View()))
    {
        return Fail(EWorldRenderSnapshotBuildResult::AllocationFailed, 0);
    }
    CameraProxy.PrimitiveId = PrimaryCameraId;
    CameraProxy.View = View;
    CameraProxy.Projection = Projection;
    CameraProxy.ViewProjection = ViewProjection;
    CameraProxy.ViewportWidth = Context.ViewportWidth;
    CameraProxy.ViewportHeight = Context.ViewportHeight;

    for (std::size_t Index = 0; Index < LightComponents.Size(); ++Index)
    {
        const FEntity Entity = LightEntities[Index];
        const FRenderPrimitiveId PrimitiveId = MakePrimitiveId(Entity);
        if (!World.IsAlive(Entity) || !PrimitiveId.IsValid())
        {
            return Fail(EWorldRenderSnapshotBuildResult::StorageMismatch, 0, PrimitiveId);
        }
        const FWorldDirectionalLightComponent& Light = LightComponents[Index];
        if (ValidateDirectionalLightParameters(Light.Direction, Light.Color, Light.Intensity) != EDirectionalLightParameterError::None)
        {
            return Fail(EWorldRenderSnapshotBuildResult::InvalidDirectionalLight, 0, PrimitiveId);
        }
        if (!Light.bActive)
        {
            continue;
        }
        if (!Candidate.Lights.IsEmpty())
        {
            return Fail(EWorldRenderSnapshotBuildResult::MultipleDirectionalLights, 0, PrimitiveId);
        }
        if (!Candidate.Lights.TryEmplaceBack(*OutSnapshot.Allocator))
        {
            return Fail(EWorldRenderSnapshotBuildResult::AllocationFailed, 0, PrimitiveId);
        }
        const Math::Vector3d Direction(Light.Direction[0], Light.Direction[1], Light.Direction[2]);
        const Math::Vector3d ToLight = Direction * (-1.0 / Direction.Length());
        FRenderLightProxy& Proxy = Candidate.Lights.Back();
        Proxy.PrimitiveId = PrimitiveId;
        Proxy.DirectionToLight = Math::Vector3f(ToLight[0], ToLight[1], ToLight[2]);
        Proxy.Radiance = Light.Color * Light.Intensity;
    }

    if (!Candidate.Meshes.TryReserve(MeshComponents.Size()) || (!Candidate.Lights.IsEmpty() && !Candidate.ShadowCasters.TryReserve(MeshComponents.Size())))
    {
        return Fail(EWorldRenderSnapshotBuildResult::AllocationFailed, 0);
    }

    for (std::size_t Index = 0; Index < MeshComponents.Size(); ++Index)
    {
        const FEntity Entity = MeshEntities[Index];
        const FRenderPrimitiveId PrimitiveId = MakePrimitiveId(Entity);
        if (!World.IsAlive(Entity) || !PrimitiveId.IsValid())
        {
            return Fail(EWorldRenderSnapshotBuildResult::StorageMismatch, Index, PrimitiveId);
        }

        const FWorldMeshComponent& Component = MeshComponents[Index];
        const FMeshAsset* ResolvedMesh = nullptr;
        const EAssetResolveResult MeshResult = Assets.Resolve(Component.Mesh, ResolvedMesh);
        if (MeshResult != EAssetResolveResult::Success)
        {
            return Fail(EWorldRenderSnapshotBuildResult::MeshResolveFailed, Index, PrimitiveId, MeshResult);
        }
        if (ResolvedMesh == nullptr || !ResolvedMesh->HasValidGeometry())
        {
            return Fail(EWorldRenderSnapshotBuildResult::InvalidMeshPayload, Index, PrimitiveId);
        }
        if (!ResolvedMesh->LocalBounds.IsValid())
        {
            return Fail(EWorldRenderSnapshotBuildResult::InvalidBounds, Index, PrimitiveId);
        }
        const FMaterialAsset* ResolvedMaterial = nullptr;
        const EAssetResolveResult MaterialResult = Assets.Resolve(Component.Material, ResolvedMaterial);
        if (MaterialResult != EAssetResolveResult::Success)
        {
            return Fail(EWorldRenderSnapshotBuildResult::MaterialResolveFailed, Index, PrimitiveId, MaterialResult);
        }
        if (ResolvedMaterial == nullptr || ResolvedMaterial->PassMask == ERenderMeshPassMask::None || !ResolvedMaterial->HasConsistentTextureBinding() || !IsFiniteNonNegativeRgb(ResolvedMaterial->BaseColor))
        {
            return Fail(EWorldRenderSnapshotBuildResult::InvalidMaterialPayload, Index, PrimitiveId);
        }
        if (ResolvedMaterial->bRequiresUV && !ResolvedMesh->bHasUV)
        {
            return Fail(EWorldRenderSnapshotBuildResult::MissingMeshUV, Index, PrimitiveId);
        }
        if (ResolvedMaterial->bRequiresUV && !ResolvedMaterial->HasColorTexture())
        {
            return Fail(EWorldRenderSnapshotBuildResult::InvalidMaterialPayload, Index, PrimitiveId);
        }
        FAssetId MaterialAssetId;
        const EAssetResolveResult MaterialIdResult = Assets.TryGetAssetId(Component.Material, MaterialAssetId);
        if (MaterialIdResult != EAssetResolveResult::Success)
        {
            return Fail(EWorldRenderSnapshotBuildResult::MaterialResolveFailed, Index, PrimitiveId, MaterialIdResult);
        }

        Math::Matrix4f LocalToWorld = Math::Matrix4f::Identity();
        if (World.HasTransform(Entity) && !World.TryGetWorldMatrix(Entity, LocalToWorld))
        {
            return Fail(EWorldRenderSnapshotBuildResult::TransformUpdateFailed, Index, PrimitiveId);
        }
        const bool bHasNormals = ResolvedMesh->bHasNormals;
        const bool bNeedsNormalTransform = bHasNormals && !Candidate.Lights.IsEmpty();
        if (!bNeedsNormalTransform && !IsFiniteMatrix(LocalToWorld))
        {
            return Fail(EWorldRenderSnapshotBuildResult::InvalidTransform, Index, PrimitiveId);
        }
        Math::Matrix3f NormalToWorld = Math::Matrix3f::Identity();
        if (bNeedsNormalTransform && !Math::TryBuildNormalMatrix(LocalToWorld, NormalToWorld))
        {
            return Fail(EWorldRenderSnapshotBuildResult::InvalidNormalTransform, Index, PrimitiveId);
        }
        FRenderBounds WorldBounds;
        if (!TryTransformBounds(ResolvedMesh->LocalBounds, LocalToWorld, WorldBounds))
        {
            return Fail(EWorldRenderSnapshotBuildResult::InvalidBounds, Index, PrimitiveId);
        }
        const EFrustumBoundsResult FrustumResult = TestBoundsAgainstFrustum(WorldBounds, ViewProjection);
        if (FrustumResult == EFrustumBoundsResult::Invalid)
        {
            return Fail(EWorldRenderSnapshotBuildResult::InvalidClipCoordinates, Index, PrimitiveId);
        }
        // Directional shadow geometry is independent of camera visibility and material shading.
        if (!Candidate.Lights.IsEmpty() && !Candidate.ShadowCasters.TryPushBack({ResolvedMesh, LocalToWorld, WorldBounds}))
        {
            return Fail(EWorldRenderSnapshotBuildResult::AllocationFailed, Index, PrimitiveId);
        }
        if (FrustumResult == EFrustumBoundsResult::Culled)
        {
            ++Report.CulledMeshCount;
            continue;
        }

        if (!Candidate.Meshes.TryEmplaceBack(*OutSnapshot.Allocator))
        {
            return Fail(EWorldRenderSnapshotBuildResult::AllocationFailed, Index, PrimitiveId);
        }
        FRenderMeshProxy& Proxy = Candidate.Meshes.Back();
        if (!Proxy.DebugName.TryAssign(Component.DebugName.View()))
        {
            return Fail(EWorldRenderSnapshotBuildResult::AllocationFailed, Index, PrimitiveId);
        }
        Proxy.PrimitiveId = PrimitiveId;
        Proxy.WorldBounds = WorldBounds;
        Proxy.LocalToWorld = LocalToWorld;
        Proxy.NormalToWorld = NormalToWorld;
        Proxy.Mesh = ResolvedMesh;
        Proxy.Material = ResolvedMaterial;
        Proxy.SortKey = Component.SortKey;
        Proxy.MaterialAssetId = MaterialAssetId;
        ++Report.VisibleMeshCount;
    }

    if (Candidate.Meshes.Size() > 1)
    {
        std::sort(Candidate.Meshes.Data(), Candidate.Meshes.Data() + Candidate.Meshes.Size(), &MeshProxyLess);
    }
    OutSnapshot.RenderScene = std::move(Candidate);
    OutSnapshot.AuthoredPrimaryCameraPose = {PrimaryCameraId, CameraToWorld};
    OutSnapshot.FrameIndex = Context.FrameIndex;
    OutSnapshot.bPublished = true;
    if (OutReport != nullptr)
    {
        *OutReport = Report;
    }
    return EWorldRenderSnapshotBuildResult::Success;
}

} // namespace LE
