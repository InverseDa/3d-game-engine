#pragma once

#include "Asset/AssetRegistry.h"
#include "WorldRenderer/WorldRenderSnapshot.h"

#include <cstddef>

namespace LE
{

class FWorld;

enum class EWorldRenderSnapshotBuildResult : uint8
{
    Success = 0,
    InvalidWorld,
    InvalidRegistry,
    WorldNotCommitted,
    InvalidViewport,
    StorageMismatch,
    TransformUpdateFailed,
    MissingCamera,
    InvalidCamera,
    InvalidCameraTransform,
    MeshResolveFailed,
    MaterialResolveFailed,
    InvalidMeshPayload,
    InvalidMaterialPayload,
    InvalidTransform,
    InvalidBounds,
    InvalidClipCoordinates,
    AllocationFailed,
    MissingMeshUV,
    InvalidDirectionalLight,
    MultipleDirectionalLights,
    InvalidNormalTransform,
};

struct FWorldRenderSnapshotBuildContext
{
    uint64 FrameIndex = 0;
    uint32 ViewportWidth = 0;
    uint32 ViewportHeight = 0;
    // Invalid or stale identity leaves camera selection and pose authored.
    FWorldRenderCameraPose CameraOverride;
};

struct FWorldRenderSnapshotBuildReport
{
    EWorldRenderSnapshotBuildResult Result = EWorldRenderSnapshotBuildResult::Success;
    std::size_t MeshIndex = 0;
    FRenderPrimitiveId PrimitiveId;
    EAssetResolveResult AssetResolveResult = EAssetResolveResult::Success;
    std::size_t SourceMeshCount = 0;
    std::size_t VisibleMeshCount = 0;
    std::size_t CulledMeshCount = 0;
};

class WORLDRENDERER_API FWorldRenderer final
{
public:
    EWorldRenderSnapshotBuildResult BuildSnapshot(const FWorld& World, const FAssetRegistry& Assets, const FWorldRenderSnapshotBuildContext& Context, FWorldRenderSnapshot& OutSnapshot, FWorldRenderSnapshotBuildReport* OutReport = nullptr) const noexcept;
};

} // namespace LE
