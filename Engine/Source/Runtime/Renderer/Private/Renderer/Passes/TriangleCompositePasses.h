#pragma once

#include "Renderer/DemoRenderPipelines.h"
#include "Renderer/RenderGraphBuilderBridge.h"
#include "Renderer/RenderPass.h"
#include "Renderer/RenderScene.h"
#include "RAL/RALCommandList.h"
#include "RAL/RALDescription.h"
#include "RAL/RALBindGroup.h"
#include "RAL/RALSwapchain.h"
#include "Execute/RFGPassContext.h"

#include <algorithm>

namespace LE
{

namespace RendererDemoPasses
{
inline bool HasSceneColorMesh(const FRenderScene* RenderScene)
{
    if (RenderScene == nullptr)
    {
        return false;
    }

    return std::find_if(
        RenderScene->Meshes.begin(),
        RenderScene->Meshes.end(),
        [](const FRenderMeshProxy& Mesh)
        {
            return HasRenderMeshPass(Mesh.PassMask, ERenderMeshPassMask::SceneColor) &&
                Mesh.GraphicsPipeline != nullptr &&
                Mesh.VertexBuffer != nullptr &&
                Mesh.VertexCount > 0;
        }) != RenderScene->Meshes.end();
}

inline bool IsCompositeConfigurationValid(
    const FTriangleCompositePipelineDesc& Desc,
    const FRenderScene* RenderScene)
{
    return Desc.CompositePipeline != nullptr &&
        Desc.SceneColorTexture != nullptr &&
        Desc.SceneColorView != nullptr &&
        Desc.CompositeBindGroup != nullptr &&
        HasSceneColorMesh(RenderScene);
}
}

class FOffscreenTrianglePass final : public IRenderPass
{
public:
    explicit FOffscreenTrianglePass(const FTriangleCompositePipelineDesc& InDesc) noexcept
        : Desc(InDesc)
    {
    }

public:
    const char* GetPassName() const override
    {
        return "OffscreenColorPass";
    }

    LE::ERFGQueueType GetQueueType() const override
    {
        return LE::ERFGQueueType::Graphics;
    }

    void Setup(FRenderPassSetupContext& Context) override
    {
        if (!RendererDemoPasses::IsCompositeConfigurationValid(Desc, Context.RenderScene))
        {
            return;
        }

        LE::FRFGAccessDesc GraphicsWrite;
        GraphicsWrite.Access = LE::ERFGAccessType::Write;
        GraphicsWrite.State = LE::ERALResourceState::RenderTarget;
        GraphicsWrite.PipelineStage = LE::ERFGPipelineStage::Graphics;

        const LE::FRFGResourceHandle SceneColorHandle = Context.GraphBridge->ImportTexture("SceneColor", Desc.SceneColorTexture, LE::ERALResourceState::Undefined);
        Context.GraphBridge->Write(Context.PassHandle, SceneColorHandle, GraphicsWrite);
    }

    void Record(FRenderPassRecordContext& Context) override
    {
        if (!RendererDemoPasses::IsCompositeConfigurationValid(Desc, Context.RenderScene))
        {
            return;
        }

        LE::Array<const FRenderMeshProxy*> MeshesToDraw;
        MeshesToDraw.Reserve(Context.RenderScene->Meshes.Size());
        for (const FRenderMeshProxy& Mesh : Context.RenderScene->Meshes)
        {
            if (!HasRenderMeshPass(Mesh.PassMask, ERenderMeshPassMask::SceneColor))
            {
                continue;
            }

            if (Mesh.GraphicsPipeline == nullptr || Mesh.VertexBuffer == nullptr || Mesh.VertexCount == 0)
            {
                continue;
            }

            MeshesToDraw.PushBack(&Mesh);
        }

        if (MeshesToDraw.Size() > 1) std::sort(
            MeshesToDraw.Data(),
            MeshesToDraw.Data() + MeshesToDraw.Size(),
            [](const FRenderMeshProxy* Lhs, const FRenderMeshProxy* Rhs)
            {
                return Lhs->SortKey < Rhs->SortKey;
            });

        if (MeshesToDraw.IsEmpty())
        {
            return;
        }

        LE::FRALCommandList* GraphCmdList = Context.PassContext->GetCommandList();
        if (GraphCmdList == nullptr)
        {
            return;
        }

        GraphCmdList->SetGraphicsPipeline(MeshesToDraw.Front()->GraphicsPipeline);

        const LE::FRALTextureDesc& TexDesc = Desc.SceneColorView->GetTexture()->GetDesc();
        LE::FRALRenderPassDesc RenderPassDesc{};
        RenderPassDesc.RenderArea.Width = TexDesc.Width;
        RenderPassDesc.RenderArea.Height = TexDesc.Height;
        RenderPassDesc.ColorAttachmentCount = 1;
        RenderPassDesc.ColorAttachments[0].RenderTarget = Desc.SceneColorView;
        RenderPassDesc.ColorAttachments[0].LoadOp = LE::EAttachmentLoadOp::Clear;
        RenderPassDesc.ColorAttachments[0].StoreOp = LE::EAttachmentStoreOp::Store;
        RenderPassDesc.ColorAttachments[0].ClearColor[0] = 0.04f;
        RenderPassDesc.ColorAttachments[0].ClearColor[1] = 0.08f;
        RenderPassDesc.ColorAttachments[0].ClearColor[2] = 0.12f;
        RenderPassDesc.ColorAttachments[0].ClearColor[3] = 1.0f;
        RenderPassDesc.bHasDepthStencil = false;
        GraphCmdList->BeginRenderPass(RenderPassDesc);

        LE::FRALViewport Viewport;
        Viewport.X = 0.0f;
        Viewport.Y = 0.0f;
        Viewport.Width = static_cast<float>(TexDesc.Width);
        Viewport.Height = static_cast<float>(TexDesc.Height);
        Viewport.MinDepth = 0.0f;
        Viewport.MaxDepth = 1.0f;
        GraphCmdList->SetViewport(Viewport);

        LE::FRALScissorRect Scissor;
        Scissor.X = 0;
        Scissor.Y = 0;
        Scissor.Width = TexDesc.Width;
        Scissor.Height = TexDesc.Height;
        GraphCmdList->SetScissorRect(Scissor);

        for (const FRenderMeshProxy* Mesh : MeshesToDraw)
        {
            GraphCmdList->SetGraphicsPipeline(Mesh->GraphicsPipeline);
            GraphCmdList->SetVertexBuffer(0, Mesh->VertexBuffer, 0);
            GraphCmdList->Draw(Mesh->VertexCount, 1, 0);
        }
        GraphCmdList->EndRenderPass();
    }

private:
    FTriangleCompositePipelineDesc Desc;
};

class FCompositePass final : public IRenderPass
{
public:
    explicit FCompositePass(const FTriangleCompositePipelineDesc& InDesc) noexcept
        : Desc(InDesc)
    {
    }

public:
    const char* GetPassName() const override
    {
        return "CompositeToBackBufferPass";
    }

    LE::ERFGQueueType GetQueueType() const override
    {
        return LE::ERFGQueueType::Graphics;
    }

    void Setup(FRenderPassSetupContext& Context) override
    {
        LE::FRALTextureView* BackBufferView = Desc.Swapchain != nullptr ? Desc.Swapchain->GetCurrentBackBufferView() : nullptr;
        if (BackBufferView == nullptr || BackBufferView->GetTexture() == nullptr ||
            !RendererDemoPasses::IsCompositeConfigurationValid(Desc, Context.RenderScene))
        {
            return;
        }

        LE::FRFGAccessDesc GraphicsWrite;
        GraphicsWrite.Access = LE::ERFGAccessType::Write;
        GraphicsWrite.State = LE::ERALResourceState::RenderTarget;
        GraphicsWrite.PipelineStage = LE::ERFGPipelineStage::Graphics;

        LE::FRFGAccessDesc GraphicsRead;
        GraphicsRead.Access = LE::ERFGAccessType::Read;
        GraphicsRead.State = LE::ERALResourceState::ShaderResource;
        GraphicsRead.ShaderStage = LE::EShaderStage::Pixel;
        GraphicsRead.PipelineStage = LE::ERFGPipelineStage::Graphics;

        const LE::FRFGResourceHandle BackBufferHandle = Context.GraphBridge->ImportTexture("BackBuffer", BackBufferView->GetTexture(), LE::ERALResourceState::Undefined);
        const LE::FRFGResourceHandle SceneColorHandle = Context.GraphBridge->ImportTexture("SceneColor", Desc.SceneColorTexture, LE::ERALResourceState::Undefined);
        Context.GraphBridge->Read(Context.PassHandle, SceneColorHandle, GraphicsRead);
        Context.GraphBridge->Write(Context.PassHandle, BackBufferHandle, GraphicsWrite);
        Context.GraphBridge->MarkOutput(BackBufferHandle);
    }

    void Record(FRenderPassRecordContext& Context) override
    {
        LE::FRALTextureView* BackBufferView = Desc.Swapchain != nullptr ? Desc.Swapchain->GetCurrentBackBufferView() : nullptr;
        if (BackBufferView == nullptr || BackBufferView->GetTexture() == nullptr ||
            !RendererDemoPasses::IsCompositeConfigurationValid(Desc, Context.RenderScene))
        {
            return;
        }

        LE::FRALCommandList* GraphCmdList = Context.PassContext->GetCommandList();
        if (GraphCmdList == nullptr)
        {
            return;
        }

        GraphCmdList->SetGraphicsPipeline(Desc.CompositePipeline);
        GraphCmdList->SetBindGroup(0, Desc.CompositeBindGroup);

        const LE::FRALTextureDesc& TexDesc = BackBufferView->GetTexture()->GetDesc();
        LE::FRALRenderPassDesc RenderPassDesc{};
        RenderPassDesc.RenderArea.Width = TexDesc.Width;
        RenderPassDesc.RenderArea.Height = TexDesc.Height;
        RenderPassDesc.ColorAttachmentCount = 1;
        RenderPassDesc.ColorAttachments[0].RenderTarget = BackBufferView;
        RenderPassDesc.ColorAttachments[0].LoadOp = LE::EAttachmentLoadOp::Clear;
        RenderPassDesc.ColorAttachments[0].StoreOp = LE::EAttachmentStoreOp::Store;
        RenderPassDesc.ColorAttachments[0].ClearColor[0] = 0.0f;
        RenderPassDesc.ColorAttachments[0].ClearColor[1] = 0.0f;
        RenderPassDesc.ColorAttachments[0].ClearColor[2] = 0.0f;
        RenderPassDesc.ColorAttachments[0].ClearColor[3] = 1.0f;
        RenderPassDesc.bHasDepthStencil = false;
        GraphCmdList->BeginRenderPass(RenderPassDesc);

        LE::FRALViewport Viewport;
        Viewport.X = 0.0f;
        Viewport.Y = 0.0f;
        Viewport.Width = static_cast<float>(TexDesc.Width);
        Viewport.Height = static_cast<float>(TexDesc.Height);
        Viewport.MinDepth = 0.0f;
        Viewport.MaxDepth = 1.0f;
        GraphCmdList->SetViewport(Viewport);

        LE::FRALScissorRect Scissor;
        Scissor.X = 0;
        Scissor.Y = 0;
        Scissor.Width = TexDesc.Width;
        Scissor.Height = TexDesc.Height;
        GraphCmdList->SetScissorRect(Scissor);

        GraphCmdList->Draw(3, 1, 0);
        GraphCmdList->EndRenderPass();
    }

private:
    FTriangleCompositePipelineDesc Desc;
};

} // namespace LE
