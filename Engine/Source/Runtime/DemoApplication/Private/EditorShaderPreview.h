#pragma once

#include "Containers/Span.h"
#include "Memory/UniquePtr.h"
#include "Types/EngineTypes.h"

#if WITH_EDITOR && PLATFORM_WINDOWS

namespace LE
{

class FRenderFrameScheduler;
class FSceneRenderer;

class FEditorShaderPreview final
{
public:
    FEditorShaderPreview() noexcept;
    ~FEditorShaderPreview();

    void Update(bool bRequested, Span<const char> Baseline, FRenderFrameScheduler& Scheduler, FSceneRenderer& Renderer) noexcept;
    uint64 GetPendingPresentation() const noexcept;
    void ConfirmPresentation(uint64 RecordedRequest) noexcept;
    void Shutdown() noexcept;

private:
    struct FState;
    UniquePtr<FState> State;
    uint64 LastRequest = 0;
};

} // namespace LE

#endif
