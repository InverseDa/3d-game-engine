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
#include "Vulkan/VulkanRAL.h"

#include <algorithm>
#include <vector>

namespace RendererDemoPasses
{
inline void TransitionTextureToShaderRead(
    FRALCommandList* CommandList,
    FRALTexture* Texture)
{
    FVulkanRALCommandList* VulkanCommandList = static_cast<FVulkanRALCommandList*>(CommandList);
    FVulkanRALTexture* VulkanTexture = static_cast<FVulkanRALTexture*>(Texture);
    if (VulkanCommandList == nullptr || VulkanTexture == nullptr || VulkanTexture->Image == VK_NULL_HANDLE)
    {
        return;
    }

    VkImageMemoryBarrier Barrier{};
    Barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    Barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    Barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    Barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image = VulkanTexture->Image;
    Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    Barrier.subresourceRange.baseMipLevel = 0;
    Barrier.subresourceRange.levelCount = 1;
    Barrier.subresourceRange.baseArrayLayer = 0;
    Barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        VulkanCommandList->Handle,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &Barrier);
}
}

class FOffscreenTrianglePass final : public IRenderPass
{
public:
    explicit FOffscreenTrianglePass(const FTriangleCompositePipelineDesc& InDesc)
        : Desc(InDesc)
    {
    }

public:
    const char* GetPassName() const override
    {
        return "OffscreenColorPass";
    }

    ERFGQueueType GetQueueType() const override
    {
        return ERFGQueueType::Graphics;
    }

    void Setup(FRenderPassSetupContext& Context) override
    {
        if (Desc.SceneColorTexture == nullptr || Desc.SceneColorView == nullptr || Desc.CompositeBindGroup == nullptr)
        {
            return;
        }

        FRFGAccessDesc GraphicsWrite;
        GraphicsWrite.Access = ERFGAccessType::Write;
        GraphicsWrite.PipelineStage = ERFGPipelineStage::Graphics;

        const FRFGResourceHandle SceneColorHandle = Context.GraphBridge->ImportTexture("SceneColor", Desc.SceneColorTexture);
        Context.GraphBridge->Write(Context.PassHandle, SceneColorHandle, GraphicsWrite);
    }

    void Record(FRenderPassRecordContext& Context) override
    {
        if (Context.RenderScene == nullptr || Context.RenderScene->Meshes.empty())
        {
            return;
        }

        std::vector<const FRenderMeshProxy*> MeshesToDraw;
        MeshesToDraw.reserve(Context.RenderScene->Meshes.size());
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
        RenderPassDesc.ColorAttachments[0].RenderTarget = Desc.SceneColorView;
        RenderPassDesc.ColorAttachments[0].LoadOp = EAttachmentLoadOp::Clear;
        RenderPassDesc.ColorAttachments[0].StoreOp = EAttachmentStoreOp::Store;
        RenderPassDesc.ColorAttachments[0].ClearColor[0] = 0.04f;
        RenderPassDesc.ColorAttachments[0].ClearColor[1] = 0.08f;
        RenderPassDesc.ColorAttachments[0].ClearColor[2] = 0.12f;
        RenderPassDesc.ColorAttachments[0].ClearColor[3] = 1.0f;
        RenderPassDesc.bHasDepthStencil = false;
        GraphCmdList->BeginRenderPass(RenderPassDesc);

        const FRALTextureDesc& TexDesc = Desc.SceneColorView->GetTexture()->GetDesc();
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
    FTriangleCompositePipelineDesc Desc;
};

class FCompositePass final : public IRenderPass
{
public:
    explicit FCompositePass(const FTriangleCompositePipelineDesc& InDesc)
        : Desc(InDesc)
    {
    }

public:
    const char* GetPassName() const override
    {
        return "CompositeToBackBufferPass";
    }

    ERFGQueueType GetQueueType() const override
    {
        return ERFGQueueType::Graphics;
    }

    void Setup(FRenderPassSetupContext& Context) override
    {
        FRALTextureView* BackBufferView = Desc.Swapchain != nullptr ? Desc.Swapchain->GetCurrentBackBufferView() : nullptr;
        if (BackBufferView == nullptr || BackBufferView->GetTexture() == nullptr || Desc.SceneColorTexture == nullptr)
        {
            return;
        }

        FRFGAccessDesc GraphicsWrite;
        GraphicsWrite.Access = ERFGAccessType::Write;
        GraphicsWrite.PipelineStage = ERFGPipelineStage::Graphics;

        FRFGAccessDesc GraphicsRead;
        GraphicsRead.Access = ERFGAccessType::Read;
        GraphicsRead.PipelineStage = ERFGPipelineStage::Graphics;

        const FRFGResourceHandle BackBufferHandle = Context.GraphBridge->ImportTexture("BackBuffer", BackBufferView->GetTexture());
        const FRFGResourceHandle SceneColorHandle = Context.GraphBridge->ImportTexture("SceneColor", Desc.SceneColorTexture);
        Context.GraphBridge->Read(Context.PassHandle, SceneColorHandle, GraphicsRead);
        Context.GraphBridge->Write(Context.PassHandle, BackBufferHandle, GraphicsWrite);
        Context.GraphBridge->MarkOutput(BackBufferHandle);
    }

    void Record(FRenderPassRecordContext& Context) override
    {
        FRALTextureView* BackBufferView = Desc.Swapchain != nullptr ? Desc.Swapchain->GetCurrentBackBufferView() : nullptr;
        if (BackBufferView == nullptr || BackBufferView->GetTexture() == nullptr)
        {
            return;
        }

        FRALCommandList* GraphCmdList = Context.PassContext->GetCommandList();
        if (GraphCmdList == nullptr)
        {
            return;
        }

        RendererDemoPasses::TransitionTextureToShaderRead(GraphCmdList, Desc.SceneColorTexture);
        GraphCmdList->SetGraphicsPipeline(Desc.CompositePipeline);
        GraphCmdList->SetBindGroup(0, Desc.CompositeBindGroup);

        FRALRenderPassDesc RenderPassDesc{};
        RenderPassDesc.ColorAttachmentCount = 1;
        RenderPassDesc.ColorAttachments[0].RenderTarget = BackBufferView;
        RenderPassDesc.ColorAttachments[0].LoadOp = EAttachmentLoadOp::Clear;
        RenderPassDesc.ColorAttachments[0].StoreOp = EAttachmentStoreOp::Store;
        RenderPassDesc.ColorAttachments[0].ClearColor[0] = 0.0f;
        RenderPassDesc.ColorAttachments[0].ClearColor[1] = 0.0f;
        RenderPassDesc.ColorAttachments[0].ClearColor[2] = 0.0f;
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

        GraphCmdList->Draw(3, 1, 0);
        GraphCmdList->EndRenderPass();
    }

private:
    FTriangleCompositePipelineDesc Desc;
};
