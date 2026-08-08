#include "Renderer/DemoRenderPipelines.h"
#include "Renderer/Passes/TriangleBackBufferPass.h"
#include "Renderer/Passes/TriangleCompositePasses.h"

namespace LE
{

class FTriangleBackBufferPipelineImpl
{
public:
    explicit FTriangleBackBufferPipelineImpl(const FTriangleBackBufferPipelineDesc& InDesc) noexcept
        : TriangleBackBufferPass(InDesc)
    {
    }

public:
    FTriangleBackBufferPass TriangleBackBufferPass;
};

class FTriangleCompositePipelineImpl
{
public:
    explicit FTriangleCompositePipelineImpl(const FTriangleCompositePipelineDesc& InDesc) noexcept
        : OffscreenPass(InDesc)
        , CompositePass(InDesc)
    {
    }

public:
    FOffscreenTrianglePass OffscreenPass;
    FCompositePass CompositePass;
};

FTriangleBackBufferPipeline::FTriangleBackBufferPipeline(const FTriangleBackBufferPipelineDesc& InDesc)
    : Impl(LE::MakeUnique<FTriangleBackBufferPipelineImpl>(InDesc))
{
}

FTriangleBackBufferPipeline::~FTriangleBackBufferPipeline() = default;

const char* FTriangleBackBufferPipeline::GetName() const
{
    return "TriangleBackBufferPipeline";
}

EShadingPath FTriangleBackBufferPipeline::GetShadingPath() const
{
    return EShadingPath::Deferred;
}

void FTriangleBackBufferPipeline::BuildPasses(
    const FRendererFrameContext& FrameContext,
    const FRenderScene& RenderScene,
    const FRenderView& RenderView,
    FRenderPipelinePlan& OutPlan)
{
    (void)FrameContext;
    (void)RenderScene;
    (void)RenderView;

    OutPlan.Reset();
    OutPlan.Passes.PushBack(&Impl->TriangleBackBufferPass);
}

FTriangleCompositePipeline::FTriangleCompositePipeline(const FTriangleCompositePipelineDesc& InDesc)
    : Impl(LE::MakeUnique<FTriangleCompositePipelineImpl>(InDesc))
{
}

FTriangleCompositePipeline::~FTriangleCompositePipeline() = default;

const char* FTriangleCompositePipeline::GetName() const
{
    return "TriangleCompositePipeline";
}

EShadingPath FTriangleCompositePipeline::GetShadingPath() const
{
    return EShadingPath::Deferred;
}

void FTriangleCompositePipeline::BuildPasses(
    const FRendererFrameContext& FrameContext,
    const FRenderScene& RenderScene,
    const FRenderView& RenderView,
    FRenderPipelinePlan& OutPlan)
{
    (void)FrameContext;
    (void)RenderScene;
    (void)RenderView;

    OutPlan.Reset();
    OutPlan.Passes.PushBack(&Impl->OffscreenPass);
    OutPlan.Passes.PushBack(&Impl->CompositePass);
}

} // namespace LE
