#pragma once

#include "RenderPipeline.h"


namespace LE
{

class RENDERER_API FSimpleRenderPipeline final : public IRenderPipeline
{
public:
    explicit FSimpleRenderPipeline(const char* InName, EShadingPath InShadingPath)
        : Name(InName)
        , ShadingPath(InShadingPath)
    {
    }

public:
    const char* GetName() const override
    {
        return Name;
    }

    EShadingPath GetShadingPath() const override
    {
        return ShadingPath;
    }

    void AddPass(IRenderPass* Pass)
    {
        Passes.PushBack(Pass);
    }

    void BuildPasses(
        const FRendererFrameContext& FrameContext,
        const FRenderScene& RenderScene,
        const FRenderView& RenderView,
        FRenderPipelinePlan& OutPlan) override
    {
        (void)FrameContext;
        (void)RenderScene;
        (void)RenderView;

        OutPlan.Reset();
        OutPlan.Passes = Passes;
    }

private:
    const char* Name = "";
    EShadingPath ShadingPath = EShadingPath::Deferred;
    LE::Array<IRenderPass*> Passes;
};

} // namespace LE
