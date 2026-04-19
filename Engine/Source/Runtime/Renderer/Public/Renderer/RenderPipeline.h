#pragma once

#include "CoreMinimal.h"
#include "ShadingPath.h"

#include <vector>

class IRenderPass;
struct FRendererFrameContext;
class FRenderScene;
struct FRenderView;

struct FRenderPipelinePlan
{
    std::vector<IRenderPass*> Passes;

    void Reset()
    {
        Passes.clear();
    }
};

class RENDERER_API IRenderPipeline
{
public:
    virtual ~IRenderPipeline() = default;

public:
    virtual const char* GetName() const = 0;
    virtual EShadingPath GetShadingPath() const = 0;

public:
    virtual void BuildPasses(
        const FRendererFrameContext& FrameContext,
        const FRenderScene& RenderScene,
        const FRenderView& RenderView,
        FRenderPipelinePlan& OutPlan) = 0;
};
