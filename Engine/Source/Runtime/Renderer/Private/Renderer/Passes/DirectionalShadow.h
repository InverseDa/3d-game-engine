#pragma once

#include "Renderer/BasePass.h"

namespace LE
{

class FRHISampler;
class FRHIShader;

struct FDirectionalShadowFrame
{
    FBasePassShadowBinding Receiver;
    FRHITextureView* DepthView = nullptr;
    FRHIPipeline_Graphics* DepthPipeline = nullptr;
    Math::Matrix4f WorldToLight;
    bool bDrawCasters = false;
};

class FDirectionalShadowMaps final : public FNonCopyable
{
public:
    static constexpr uint32 Resolution = 1024;

    ~FDirectionalShadowMaps() noexcept;
    bool Initialize(FRHIDevice& Device, FRHIShader& VertexShader) noexcept;
    void Shutdown() noexcept;
    FRHIBindGroupLayout* GetLayout() const noexcept { return Layout; }
    bool PrepareFrame(uint32 SlotIndex, const FRenderScene& Scene, FDirectionalShadowFrame& Out) const noexcept;

private:
    struct FSlot
    {
        FBasePassShadowBinding Receiver;
        FRHITextureView* DepthView = nullptr;
    };

    FRHIBindGroupLayout* Layout = nullptr;
    FRHISampler* Sampler = nullptr;
    FRHIPipeline_Graphics* DepthPipeline = nullptr;
    FSlot Slots[FRenderFrameScheduler::FrameSlotCount];
};

class FDirectionalShadowPass final : public IRenderPass
{
public:
    explicit FDirectionalShadowPass(const FDirectionalShadowFrame& InFrame) noexcept : Frame(InFrame) {}
    const char* GetPassName() const override { return "ShadowDepth"; }
    ERFGQueueType GetQueueType() const override { return ERFGQueueType::Graphics; }
    void Setup(FRenderPassSetupContext& Context) override;
    void Record(FRenderPassRecordContext& Context) override;

private:
    FDirectionalShadowFrame Frame;
};

} // namespace LE
