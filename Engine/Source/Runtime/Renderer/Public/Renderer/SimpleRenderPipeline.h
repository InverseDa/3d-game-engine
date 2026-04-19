#pragma once

#include "RenderPipeline.h"

#include <vector>

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
        Passes.push_back(Pass);
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
    std::vector<IRenderPass*> Passes;
};
