#pragma once

#include "CoreMinimal.h"
#include "Renderer/RenderScene.h"

namespace LE
{

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

private:
    friend class FWorldRenderer;

    void Reset() noexcept;

    IAllocator* Allocator = nullptr;
    FRenderScene RenderScene;
    uint64 FrameIndex = 0;
    bool bPublished = false;
};

} // namespace LE
