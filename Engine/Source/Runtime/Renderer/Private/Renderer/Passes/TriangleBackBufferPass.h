#pragma once

#include "Renderer/DemoRenderPipelines.h"
#include "Renderer/RenderGraphBuilderBridge.h"
#include "Renderer/RenderPass.h"
#include "Renderer/RenderScene.h"
#include "RAL/RALCommandList.h"
#include "RAL/RALDescription.h"
#include "RAL/RALSwapchain.h"
#include "Execute/RFGPassContext.h"

#include <algorithm>
#include <vector>

class FTriangleBackBufferPass final : public IRenderPass
{
public:
    explicit FTriangleBackBufferPass(const FTriangleBackBufferPipelineDesc& InDesc)
        : Desc(InDesc)
    {
    }

public:
    const char* GetPassName() const override
    {
        return "TrianglePass";
    }

    ERFGQueueType GetQueueType() const override
    {
        return ERFGQueueType::Graphics;
    }

    void Setup(FRenderPassSetupContext& Context) override
    {
        FRALTextureView* BackBufferView = Desc.Swapchain != nullptr ? Desc.Swapchain->GetCurrentBackBufferView() : nullptr;
        if (BackBufferView == nullptr || BackBufferView->GetTexture() == nullptr)
        {
            return;
        }

        FRFGAccessDesc BackBufferWrite;
        BackBufferWrite.Access = ERFGAccessType::Write;
        BackBufferWrite.PipelineStage = ERFGPipelineStage::Graphics;

        const FRFGResourceHandle BackBufferHandle = Context.GraphBridge->ImportTexture("BackBuffer", BackBufferView->GetTexture());
        Context.GraphBridge->Write(Context.PassHandle, BackBufferHandle, BackBufferWrite);
        Context.GraphBridge->MarkOutput(BackBufferHandle);
    }

    void Record(FRenderPassRecordContext& Context) override
    {
        FRALTextureView* BackBufferView = Desc.Swapchain != nullptr ? Desc.Swapchain->GetCurrentBackBufferView() : nullptr;
        if (BackBufferView == nullptr || BackBufferView->GetTexture() == nullptr)
        {
            return;
        }

        if (Context.RenderScene == nullptr || Context.RenderScene->Meshes.empty())
        {
            return;
        }

        std::vector<const FRenderMeshProxy*> MeshesToDraw;
        MeshesToDraw.reserve(Context.RenderScene->Meshes.size());
        for (const FRenderMeshProxy& Mesh : Context.RenderScene->Meshes)
        {
            if (!HasRenderMeshPass(Mesh.PassMask, ERenderMeshPassMask::BackBuffer))
            {
                continue;
            }

            if (Mesh.GraphicsPipeline == nullptr || Mesh.VertexBuffer == nullptr || Mesh.VertexCount == 0)
            {
                continue;
            }

            MeshesToDraw.push_back(&Mesh);
        }

        std::sort(
            MeshesToDraw.begin(),
            MeshesToDraw.end(),
            [](const FRenderMeshProxy* Lhs, const FRenderMeshProxy* Rhs)
            {
                return Lhs->SortKey < Rhs->SortKey;
            });

        if (MeshesToDraw.empty())
        {
            return;
        }

        FRALCommandList* GraphCmdList = Context.PassContext->GetCommandList();
        if (GraphCmdList == nullptr)
        {
            return;
        }

        GraphCmdList->SetGraphicsPipeline(MeshesToDraw.front()->GraphicsPipeline);

        FRALRenderPassDesc RenderPassDesc{};
        RenderPassDesc.ColorAttachmentCount = 1;
        RenderPassDesc.ColorAttachments[0].RenderTarget = BackBufferView;
        RenderPassDesc.ColorAttachments[0].LoadOp = EAttachmentLoadOp::Clear;
        RenderPassDesc.ColorAttachments[0].StoreOp = EAttachmentStoreOp::Store;
        RenderPassDesc.ColorAttachments[0].ClearColor[0] = 0.1f;
        RenderPassDesc.ColorAttachments[0].ClearColor[1] = 0.1f;
        RenderPassDesc.ColorAttachments[0].ClearColor[2] = 0.1f;
        RenderPassDesc.ColorAttachments[0].ClearColor[3] = 1.0f;
        RenderPassDesc.bHasDepthStencil = false;
        GraphCmdList->BeginRenderPass(RenderPassDesc);

        const FRALTextureDesc& TexDesc = BackBufferView->GetTexture()->GetDesc();
        FRALViewport Viewport;
        Viewport.X = 0.0f;
        Viewport.Y = 0.0f;
        Viewport.Width = static_cast<float>(TexDesc.Width);
        Viewport.Height = static_cast<float>(TexDesc.Height);
        Viewport.MinDepth = 0.0f;
        Viewport.MaxDepth = 1.0f;
        GraphCmdList->SetViewport(Viewport);

        FRALScissorRect Scissor;
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
    FTriangleBackBufferPipelineDesc Desc;
};
