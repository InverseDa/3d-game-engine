#pragma once

#include "CoreMinimal.h"
#include "RHI/RHIDescription.h"
#include "Renderer/RenderPass.h"
#include "Renderer/RenderFrameScheduler.h"
#include "Renderer/RenderPipeline.h"
#include "Renderer/RenderScene.h"

#include <cstddef>
#include <type_traits>

namespace LE
{

class FRHITexture;
class FRHITextureView;
class FRHIBuffer;
class FRHIBindGroup;
class FRHIBindGroupLayout;
class FRHIDevice;

struct FBasePassLightBinding
{
    FRHIBuffer* Buffer = nullptr;
    FRHIBindGroup* BindGroup = nullptr;
};

struct FBasePassResources
{
    FRHITexture* SceneColorTexture = nullptr;
    FRHITextureView* SceneColorView = nullptr;
    FRHITexture* SceneDepthTexture = nullptr;
    FRHITextureView* SceneDepthView = nullptr;
    FBasePassLightBinding Light;
};

// Shader-visible bytes. The vertex shader reads the four row-major vectors
// explicitly and dots each row with the local position.
struct alignas(16) FBasePassObjectConstants
{
    Math::Matrix4f LocalToClip = Math::Matrix4f::Identity();
    Math::Vector4f BaseColor{1.0f};
    Math::Vector4f NormalToWorldRows[3]{};
};

static_assert(sizeof(FBasePassObjectConstants) == 128,
    "BasePass push constants must remain exactly 128 bytes");
static_assert(offsetof(FBasePassObjectConstants, BaseColor) == 64,
    "BasePass color must follow the 64-byte matrix without padding");
static_assert(offsetof(FBasePassObjectConstants, NormalToWorldRows) == 80);
static_assert(std::is_trivially_copyable<FBasePassObjectConstants>::value,
    "BasePass push constants must be copied directly into command recording");

struct alignas(16) FBasePassLightConstants
{
    Math::Vector4f DirectionAndEnabled;
    Math::Vector4f Radiance;
};

static_assert(sizeof(FBasePassLightConstants) == 32);
static_assert(offsetof(FBasePassLightConstants, Radiance) == 16);
static_assert(std::is_trivially_copyable<FBasePassLightConstants>::value);

// Release these resources after the scheduler has drained all submitted slots.
class RENDERER_API FBasePassFrameLighting final : public FNonCopyable
{
public:
    static constexpr uint32 SetIndex = 1;
    static constexpr uint32 BindingIndex = 0;

    ~FBasePassFrameLighting() noexcept;
    bool Initialize(FRHIDevice& Device) noexcept;
    void Shutdown() noexcept;
    FRHIBindGroupLayout* GetLayout() const noexcept { return Layout; }

    // The scheduler must have waited for this slot before it is written.
    bool WriteSlot(uint32 SlotIndex, Span<const FRenderLightProxy> Lights,
        FBasePassLightBinding& OutBinding) noexcept;

private:
    FRHIBindGroupLayout* Layout = nullptr;
    FBasePassLightBinding Slots[FRenderFrameScheduler::FrameSlotCount];
};

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
