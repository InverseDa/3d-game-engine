#pragma once

#include "CoreMinimal.h"
#include "RHI/RHITypes.h"
#include "Renderer/RenderFrameScheduler.h"

namespace LE
{

class FRenderAssetProvider;
class FRenderScene;

struct FSceneShaderBytecode
{
    Span<const char> Code;
    StringView EntryPoint;
};

struct FSceneRenderShaders
{
    FSceneShaderBytecode BasePassVertex;
    FSceneShaderBytecode BasePassPixel;
    FSceneShaderBytecode CompositeVertex;
    FSceneShaderBytecode CompositePixel;
    // Supply the shadow variants of BasePass when this vertex shader is present.
    FSceneShaderBytecode ShadowDepthVertex;
};

struct FSceneRenderTargetDesc
{
    uint32 Width = 0;
    uint32 Height = 0;
    EPixelFormat Format = EPixelFormat::Unknown;
};

enum class EBasePassPixelShaderInstallResult : uint8
{
    Success,
    InvalidArguments,
    ShaderCreationFailed,
    PipelineCreationFailed,
    FrameRetirementFailed,
};

// Owns the default scene pipeline and attachments. The application owns the
// device, swapchain, frame scheduler and scene asset sessions.
class RENDERER_API FSceneRenderer final : public FNonCopyable
{
public:
    FSceneRenderer();
    ~FSceneRenderer();

    bool Initialize(FRHIDevice& Device, const FSceneRenderShaders& Shaders, const FSceneRenderTargetDesc& Target);
    bool InitializeAssetProvider(FRenderAssetProvider& Provider) const noexcept;
    FSceneRenderTargetDesc GetTargetDesc() const noexcept;

    // A failed resize preserves the previous offscreen resources. A successful
    // resize retires them only after all submitted frame slots have drained.
    bool Resize(FRenderFrameScheduler& Scheduler, const FSceneRenderTargetDesc& Target);

    // Call between frames with this scene renderer's scheduler. The caller
    // validates bytecode compatibility with the current BasePass interface.
    // The renderer copies the bytecode and preserves the pipeline on failure.
    EBasePassPixelShaderInstallResult InstallBasePassPixelShader(FRenderFrameScheduler& Scheduler, const FSceneShaderBytecode& Bytecode);
    ERenderFrameRecordResult RecordFrame(FRenderFrameScope& Frame, FRHISwapchain& Swapchain, const FRenderScene& Scene);

    // Drain submitted frames and release all asset providers before shutdown.
    void Shutdown() noexcept;

private:
    struct FState;
    UniquePtr<FState> State;
};

} // namespace LE
