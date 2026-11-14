#pragma once

#include "CoreMinimal.h"
#include "RHI/RHIDescription.h"
#include "Renderer/RenderPass.h"
#include "Renderer/RenderPipeline.h"
#include "Renderer/RenderScene.h"

#include <cstddef>
#include <type_traits>

namespace LE
{

class FRHITexture;
class FRHITextureView;

struct FBasePassResources
{
    FRHITexture* SceneColorTexture = nullptr;
    FRHITextureView* SceneColorView = nullptr;
    FRHITexture* SceneDepthTexture = nullptr;
    FRHITextureView* SceneDepthView = nullptr;
};

// Shader-visible bytes. The vertex shader reads the four row-major vectors
// explicitly and dots each row with the local position.
struct alignas(16) FBasePassObjectConstants
{
    Math::Matrix4f LocalToClip = Math::Matrix4f::Identity();
    Math::Vector4f BaseColor{1.0f};
};

static_assert(sizeof(FBasePassObjectConstants) == 80,
    "BasePass push constants must remain exactly 80 bytes");
static_assert(offsetof(FBasePassObjectConstants, BaseColor) == 64,
    "BasePass color must follow the 64-byte matrix without padding");
static_assert(std::is_trivially_copyable<FBasePassObjectConstants>::value,
    "BasePass push constants must be copied directly into command recording");

class RENDERER_API FBasePass final : public IRenderPass
{
public:
    explicit FBasePass(const FBasePassResources& InResources) noexcept;

    static bool IsConfigurationValid(
        const FBasePassResources& Resources,
        const FRenderScene& RenderScene) noexcept;

    const char* GetPassName() const override;
    ERFGQueueType GetQueueType() const override;
    void Setup(FRenderPassSetupContext& Context) override;
    void Record(FRenderPassRecordContext& Context) override;

private:
    FBasePassResources Resources;
};

class RENDERER_API FBasePassPipeline final : public IRenderPipeline
{
public:
    explicit FBasePassPipeline(const FBasePassResources& InResources) noexcept;

    const char* GetName() const override;
    EShadingPath GetShadingPath() const override;
    void BuildPasses(
        const FRendererFrameContext& FrameContext,
        const FRenderScene& RenderScene,
        const FRenderView& RenderView,
        FRenderPipelinePlan& OutPlan) override;

private:
    FBasePassResources Resources;
    FBasePass Pass;
};

} // namespace LE
