#include "Renderer/BasePassCompositePipeline.h"

#include "Renderer/Passes/SceneColorCompositePass.h"

namespace LE
{

class FBasePassCompositePipelineImpl
{
public:
    explicit FBasePassCompositePipelineImpl(const FBasePassCompositePipelineDesc& InDesc) noexcept : Desc(InDesc), BasePass(InDesc.BasePassResources), CompositePass(InDesc) {}

    FBasePassCompositePipelineDesc Desc;
    FBasePass BasePass;
    FSceneColorCompositePass CompositePass;
};

FBasePassCompositePipeline::FBasePassCompositePipeline(const FBasePassCompositePipelineDesc& InDesc) : Impl(MakeUnique<FBasePassCompositePipelineImpl>(InDesc)) {}

FBasePassCompositePipeline::~FBasePassCompositePipeline() = default;

const char* FBasePassCompositePipeline::GetName() const
{
    return "BasePassCompositePipeline";
}

EShadingPath FBasePassCompositePipeline::GetShadingPath() const
{
    return EShadingPath::Deferred;
}

void FBasePassCompositePipeline::BuildPasses(const FRendererFrameContext& FrameContext, const FRenderScene& RenderScene, const FRenderView& RenderView, FRenderPipelinePlan& OutPlan)
{
    static_cast<void>(FrameContext);
    static_cast<void>(RenderView);
    OutPlan.Reset();
    if (!Impl || !FBasePass::IsConfigurationValid(Impl->Desc.BasePassResources, RenderScene) || !IsSceneColorCompositeConfigurationValid(Impl->Desc) ||
        (Impl->Desc.ShadowPass != nullptr && !OutPlan.Passes.TryPushBack(Impl->Desc.ShadowPass)) ||
        !OutPlan.Passes.TryPushBack(&Impl->BasePass) || !OutPlan.Passes.TryPushBack(&Impl->CompositePass))
    {
        OutPlan.Reset();
    }
}

} // namespace LE
