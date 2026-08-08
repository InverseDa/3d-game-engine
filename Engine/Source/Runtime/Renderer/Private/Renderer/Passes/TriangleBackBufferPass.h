#pragma once

#include "Renderer/DemoRenderPipelines.h"
#include "Renderer/RenderGraphBuilderBridge.h"
#include "Renderer/RenderPass.h"
#include "Renderer/RenderScene.h"
#include "RAL/RALCommandList.h"
#include "RAL/RALDescription.h"
#include "RAL/RALTexture.h"
#include "Execute/RFGPassContext.h"

#include <algorithm>

namespace LE
{

class FTriangleBackBufferPass final : public IRenderPass
{
public:
    explicit FTriangleBackBufferPass(const FTriangleBackBufferPipelineDesc& InDesc) noexcept
        : Desc(InDesc)
    {
    }

public:
    const char* GetPassName() const override
    {
        return "TrianglePass";
    }

    LE::ERFGQueueType GetQueueType() const override
    {
        return LE::ERFGQueueType::Graphics;
    }

    void Setup(FRenderPassSetupContext& Context) override
    {
        LE::FRALTextureView* BackBufferView = Desc.BackBufferView;
        if (BackBufferView == nullptr || BackBufferView->GetTexture() == nullptr)
        {
            return;
        }

        LE::FRFGAccessDesc BackBufferWrite;
        BackBufferWrite.Access = LE::ERFGAccessType::Write;
        BackBufferWrite.State = LE::ERALResourceState::RenderTarget;
        BackBufferWrite.PipelineStage = LE::ERFGPipelineStage::Graphics;

        const LE::FRFGResourceHandle BackBufferHandle = Context.GraphBridge->ImportTexture("BackBuffer", BackBufferView->GetTexture(), LE::ERALResourceState::Undefined);
        Context.GraphBridge->Write(Context.PassHandle, BackBufferHandle, BackBufferWrite);
        Context.GraphBridge->MarkOutput(BackBufferHandle);
    }

    void Record(FRenderPassRecordContext& Context) override
    {
        LE::FRALTextureView* BackBufferView = Desc.BackBufferView;
        if (BackBufferView == nullptr || BackBufferView->GetTexture() == nullptr)
        {
            return;
        }

        if (Context.RenderScene == nullptr || Context.RenderScene->Meshes.IsEmpty())
        {
            return;
        }

        LE::Array<const FRenderMeshProxy*> MeshesToDraw;
        MeshesToDraw.Reserve(Context.RenderScene->Meshes.Size());
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

        const LE::FRALTextureDesc& TexDesc = BackBufferView->GetTexture()->GetDesc();
        LE::FRALRenderPassDesc RenderPassDesc{};
        RenderPassDesc.RenderArea.Width = TexDesc.Width;
        RenderPassDesc.RenderArea.Height = TexDesc.Height;
        RenderPassDesc.ColorAttachmentCount = 1;
        RenderPassDesc.ColorAttachments[0].RenderTarget = BackBufferView;
        RenderPassDesc.ColorAttachments[0].LoadOp = LE::EAttachmentLoadOp::Clear;
        RenderPassDesc.ColorAttachments[0].StoreOp = LE::EAttachmentStoreOp::Store;
        RenderPassDesc.ColorAttachments[0].ClearColor[0] = 0.1f;
        RenderPassDesc.ColorAttachments[0].ClearColor[1] = 0.1f;
        RenderPassDesc.ColorAttachments[0].ClearColor[2] = 0.1f;
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
    FTriangleBackBufferPipelineDesc Desc;
};

} // namespace LE
