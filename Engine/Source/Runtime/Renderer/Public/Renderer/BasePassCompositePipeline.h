#pragma once

#include "CoreMinimal.h"
#include "Renderer/BasePass.h"
#include "Renderer/RenderPipeline.h"

namespace LE
{

class FRALBindGroup;
class FRALPipeline_Graphics;
class FRALTextureView;

struct FBasePassCompositePipelineDesc
{
    FBasePassResources BasePassResources;
    FRALTextureView* BackBufferView = nullptr;
    FRALPipeline_Graphics* CompositePipeline = nullptr;
    FRALBindGroup* CompositeBindGroup = nullptr;
};

class FBasePassCompositePipelineImpl;

class RENDERER_API FBasePassCompositePipeline final : public IRenderPipeline
{
public:
    explicit FBasePassCompositePipeline(
        const FBasePassCompositePipelineDesc& InDesc);
    ~FBasePassCompositePipeline() override;

    const char* GetName() const override;
    EShadingPath GetShadingPath() const override;
    void BuildPasses(
        const FRendererFrameContext& FrameContext,
        const FRenderScene& RenderScene,
        const FRenderView& RenderView,
        FRenderPipelinePlan& OutPlan) override;

private:
    UniquePtr<FBasePassCompositePipelineImpl> Impl;
};

} // namespace LE
