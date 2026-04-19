#pragma once

#include "CoreMinimal.h"
#include "RenderPipeline.h"

#include <memory>

class FRALBuffer;
class FRALBindGroup;
class FRALPipeline_Graphics;
class FRALSwapchain;
class FRALTexture;
class FRALTextureView;

struct FTriangleBackBufferPipelineDesc
{
    FRALSwapchain* Swapchain = nullptr;
};

struct FTriangleCompositePipelineDesc
{
    FRALSwapchain* Swapchain = nullptr;
    FRALPipeline_Graphics* CompositePipeline = nullptr;
    FRALTexture* SceneColorTexture = nullptr;
    FRALTextureView* SceneColorView = nullptr;
    FRALBindGroup* CompositeBindGroup = nullptr;
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
    std::unique_ptr<FTriangleBackBufferPipelineImpl> Impl;
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
    std::unique_ptr<FTriangleCompositePipelineImpl> Impl;
};
