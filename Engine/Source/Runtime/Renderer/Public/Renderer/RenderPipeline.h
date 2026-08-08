#pragma once

#include "CoreMinimal.h"
#include "ShadingPath.h"


namespace LE
{

class IRenderPass;
struct FRendererFrameContext;
class FRenderScene;
struct FRenderView;

struct FRenderPipelinePlan
{
    LE::Array<IRenderPass*> Passes;

    void Reset()
    {
        Passes.Clear();
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

} // namespace LE
