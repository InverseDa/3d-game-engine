#pragma once

#include "Execute/RFGPassContext.h"
#include "RAL/RALBindGroup.h"
#include "RAL/RALCommandList.h"
#include "RAL/RALPipeline.h"
#include "RAL/RALTexture.h"
#include "Renderer/BasePassCompositePipeline.h"
#include "Renderer/RenderGraphBuilderBridge.h"
#include "Renderer/RenderPass.h"

namespace LE
{

inline bool IsSceneColorCompositeConfigurationValid(
    const FBasePassCompositePipelineDesc& Desc) noexcept
{
    if (Desc.BackBufferView == nullptr || Desc.BackBufferView->GetTexture() == nullptr ||
        Desc.CompositePipeline == nullptr || Desc.CompositeBindGroup == nullptr ||
        Desc.BasePassResources.SceneColorTexture == nullptr ||
        Desc.BasePassResources.SceneColorView == nullptr ||
        Desc.BasePassResources.SceneColorView->GetTexture() !=
            Desc.BasePassResources.SceneColorTexture)
    {
        return false;
    }
    const FRALTextureDesc& BackBuffer = Desc.BackBufferView->GetTexture()->GetDesc();
    const FRALTextureDesc& SceneColor =
        Desc.BasePassResources.SceneColorTexture->GetDesc();
    const FRALTextureViewDesc& BackBufferView = Desc.BackBufferView->GetDesc();
    const FRALPipelineDesc_Graphics& Pipeline = Desc.CompositePipeline->GetDesc();
    const FRALBindGroupDesc& BindGroup = Desc.CompositeBindGroup->GetDesc();
    const EPixelFormat BackBufferViewFormat = BackBufferView.Format == EPixelFormat::Unknown
        ? BackBuffer.Format
        : BackBufferView.Format;
    return BackBuffer.Width > 0 && BackBuffer.Height > 0 &&
        BackBuffer.bIsRenderTarget &&
        SceneColor.Width > 0 && SceneColor.Height > 0 &&
        SceneColor.Format == EPixelFormat::B8G8R8A8_SRGB &&
        SceneColor.bIsShaderResource && Pipeline.RenderTargetCount == 1 &&
        Pipeline.RenderTargetFormats[0] == BackBufferViewFormat &&
        Pipeline.DepthStencilFormat == EPixelFormat::Unknown &&
        Pipeline.BindGroupLayouts.Size() == 1 &&
        Pipeline.BindGroupLayouts[0] != nullptr &&
        BindGroup.Layout == Pipeline.BindGroupLayouts[0] &&
        Pipeline.BindGroupLayouts[0]->GetDesc().SetIndex == 0;
}

class FSceneColorCompositePass final : public IRenderPass
{
public:
    explicit FSceneColorCompositePass(
        const FBasePassCompositePipelineDesc& InDesc) noexcept
        : Desc(InDesc)
    {
    }

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
        if (Context.GraphBridge == nullptr ||
            !IsSceneColorCompositeConfigurationValid(Desc))
        {
            return;
        }
        FRFGAccessDesc GraphicsRead;
        GraphicsRead.Access = ERFGAccessType::Read;
        GraphicsRead.State = ERALResourceState::ShaderResource;
        GraphicsRead.ShaderStage = EShaderStage::Pixel;
        GraphicsRead.PipelineStage = ERFGPipelineStage::Graphics;
        FRFGAccessDesc GraphicsWrite;
        GraphicsWrite.Access = ERFGAccessType::Write;
        GraphicsWrite.State = ERALResourceState::RenderTarget;
        GraphicsWrite.PipelineStage = ERFGPipelineStage::Graphics;

        const FRFGResourceHandle SceneColor = Context.GraphBridge->ImportTexture(
            "SceneColor",
            Desc.BasePassResources.SceneColorTexture,
            ERALResourceState::Undefined);
        const FRFGResourceHandle BackBuffer = Context.GraphBridge->ImportTexture(
            "BackBuffer",
            Desc.BackBufferView->GetTexture(),
            ERALResourceState::Undefined);
        Context.GraphBridge->Read(Context.PassHandle, SceneColor, GraphicsRead);
        Context.GraphBridge->Write(Context.PassHandle, BackBuffer, GraphicsWrite);
        Context.GraphBridge->MarkOutput(BackBuffer);
    }

    void Record(FRenderPassRecordContext& Context) override
    {
        if (Context.PassContext == nullptr ||
            !IsSceneColorCompositeConfigurationValid(Desc))
        {
            return;
        }
        FRALCommandList* const CommandList = Context.PassContext->GetCommandList();
        if (CommandList == nullptr)
        {
            return;
        }
        CommandList->SetGraphicsPipeline(Desc.CompositePipeline);
        CommandList->SetBindGroup(0, Desc.CompositeBindGroup);

        const FRALTextureDesc& BackBuffer =
            Desc.BackBufferView->GetTexture()->GetDesc();
        FRALRenderPassDesc RenderPass;
        RenderPass.RenderArea.Width = BackBuffer.Width;
        RenderPass.RenderArea.Height = BackBuffer.Height;
        RenderPass.ColorAttachmentCount = 1;
        RenderPass.ColorAttachments[0].RenderTarget = Desc.BackBufferView;
        RenderPass.ColorAttachments[0].LoadOp = EAttachmentLoadOp::Clear;
        RenderPass.ColorAttachments[0].StoreOp = EAttachmentStoreOp::Store;
        RenderPass.ColorAttachments[0].ClearColor[3] = 1.0f;
        CommandList->BeginRenderPass(RenderPass);

        FRALViewport Viewport;
        Viewport.Width = static_cast<float>(BackBuffer.Width);
        Viewport.Height = static_cast<float>(BackBuffer.Height);
        CommandList->SetViewport(Viewport);
        FRALScissorRect Scissor;
        Scissor.Width = static_cast<int32>(BackBuffer.Width);
        Scissor.Height = static_cast<int32>(BackBuffer.Height);
        CommandList->SetScissorRect(Scissor);
        CommandList->Draw(3, 1, 0);
        CommandList->EndRenderPass();
    }

private:
    FBasePassCompositePipelineDesc Desc;
};

} // namespace LE
