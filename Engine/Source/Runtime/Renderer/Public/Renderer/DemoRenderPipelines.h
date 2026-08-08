#pragma once

#include "CoreMinimal.h"
#include "RenderPipeline.h"

namespace LE
{

class FRALBuffer;
class FRALBindGroup;
class FRALPipeline_Graphics;
class FRALSwapchain;
class FRALTexture;
class FRALTextureView;

struct FTriangleBackBufferPipelineDesc
{
    LE::FRALSwapchain* Swapchain = nullptr;
};

struct FTriangleCompositePipelineDesc
{
    LE::FRALSwapchain* Swapchain = nullptr;
    LE::FRALPipeline_Graphics* CompositePipeline = nullptr;
    LE::FRALTexture* SceneColorTexture = nullptr;
    LE::FRALTextureView* SceneColorView = nullptr;
    LE::FRALBindGroup* CompositeBindGroup = nullptr;
};

class FTriangleBackBufferPipelineImpl;
class FTriangleCompositePipelineImpl;

class RENDERER_API FTriangleBackBufferPipeline final : public IRenderPipeline
{
public:
    explicit FTriangleBackBufferPipeline(const FTriangleBackBufferPipelineDesc& InDesc);
    ~FTriangleBackBufferPipeline() override;

public:
    const char* GetName() const override;
    EShadingPath GetShadingPath() const override;
    void BuildPasses(
        const FRendererFrameContext& FrameContext,
        const FRenderScene& RenderScene,
        const FRenderView& RenderView,
        FRenderPipelinePlan& OutPlan) override;

private:
    LE::UniquePtr<FTriangleBackBufferPipelineImpl> Impl;
};

class RENDERER_API FTriangleCompositePipeline final : public IRenderPipeline
{
public:
    explicit FTriangleCompositePipeline(const FTriangleCompositePipelineDesc& InDesc);
    ~FTriangleCompositePipeline() override;

public:
    const char* GetName() const override;
    EShadingPath GetShadingPath() const override;
    void BuildPasses(
        const FRendererFrameContext& FrameContext,
        const FRenderScene& RenderScene,
        const FRenderView& RenderView,
        FRenderPipelinePlan& OutPlan) override;

private:
    LE::UniquePtr<FTriangleCompositePipelineImpl> Impl;
};

} // namespace LE
