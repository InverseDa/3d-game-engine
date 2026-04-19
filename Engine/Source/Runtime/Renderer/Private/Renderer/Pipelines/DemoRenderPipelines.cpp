#include "Renderer/DemoRenderPipelines.h"
#include "Renderer/Passes/TriangleBackBufferPass.h"
#include "Renderer/Passes/TriangleCompositePasses.h"

class FTriangleBackBufferPipelineImpl
{
public:
    explicit FTriangleBackBufferPipelineImpl(const FTriangleBackBufferPipelineDesc& InDesc)
        : TriangleBackBufferPass(InDesc)
    {
    }

public:
    FTriangleBackBufferPass TriangleBackBufferPass;
};

class FTriangleCompositePipelineImpl
{
public:
    explicit FTriangleCompositePipelineImpl(const FTriangleCompositePipelineDesc& InDesc)
        : OffscreenPass(InDesc)
        , CompositePass(InDesc)
    {
    }

public:
    FOffscreenTrianglePass OffscreenPass;
    FCompositePass CompositePass;
};

FTriangleBackBufferPipeline::FTriangleBackBufferPipeline(const FTriangleBackBufferPipelineDesc& InDesc)
    : Impl(std::make_unique<FTriangleBackBufferPipelineImpl>(InDesc))
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
    OutPlan.Passes.push_back(&Impl->TriangleBackBufferPass);
}

FTriangleCompositePipeline::FTriangleCompositePipeline(const FTriangleCompositePipelineDesc& InDesc)
    : Impl(std::make_unique<FTriangleCompositePipelineImpl>(InDesc))
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
    OutPlan.Passes.push_back(&Impl->OffscreenPass);
    OutPlan.Passes.push_back(&Impl->CompositePass);
}
