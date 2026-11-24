#pragma once

#include "CoreMinimal.h"
#include "Renderer/RenderScene.h"
#include "World/Entity.h"

namespace LE
{

struct FWorldRenderCameraPose
{
    FRenderPrimitiveId PrimitiveId;
    Math::Matrix4f CameraToWorld = Math::Matrix4f::Identity();
    FEntity Entity;
};

// An owning, immutable-to-consumers CPU snapshot for one synchronous render
// recording scope. GPU resources inside its proxies remain provider-owned.
class WORLDRENDERER_API FWorldRenderSnapshot final
{
public:
    FWorldRenderSnapshot() noexcept;
    explicit FWorldRenderSnapshot(IAllocator& Allocator) noexcept;
    ~FWorldRenderSnapshot() = default;

    FWorldRenderSnapshot(const FWorldRenderSnapshot&) = delete;
    FWorldRenderSnapshot& operator=(const FWorldRenderSnapshot&) = delete;
    FWorldRenderSnapshot(FWorldRenderSnapshot&& Other) noexcept;
    FWorldRenderSnapshot& operator=(FWorldRenderSnapshot&& Other) noexcept;

    bool IsPublished() const noexcept { return bPublished; }
    uint64 GetFrameIndex() const noexcept { return FrameIndex; }
    const FRenderScene& GetRenderScene() const noexcept { return RenderScene; }
    // The authored pose is valid only on a published snapshot and remains the
    // authored world transform even when RenderScene uses a session override.
    const FWorldRenderCameraPose& GetAuthoredPrimaryCameraPose() const noexcept { return AuthoredPrimaryCameraPose; }

private:
    friend class FWorldRenderer;

    void Reset() noexcept;

    IAllocator* Allocator = nullptr;
    FRenderScene RenderScene;
    FWorldRenderCameraPose AuthoredPrimaryCameraPose;
    uint64 FrameIndex = 0;
    bool bPublished = false;
};

} // namespace LE
