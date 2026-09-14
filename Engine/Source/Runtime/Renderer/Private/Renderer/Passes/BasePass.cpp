#include "Renderer/BasePass.h"

#include "Execute/RFGPassContext.h"
#include "RAL/RALBuffer.h"
#include "RAL/RALCommandList.h"
#include "RAL/RALPipeline.h"
#include "RAL/RALTexture.h"
#include "Renderer/RenderGraphBuilderBridge.h"

#include <cmath>

namespace LE
{
namespace
{

constexpr EPixelFormat SceneColorFormat = EPixelFormat::B8G8R8A8_SRGB;
constexpr EPixelFormat SceneDepthFormat = EPixelFormat::D32_FLOAT;
constexpr EShaderStage BasePassPushStages = static_cast<EShaderStage>(
    static_cast<uint8>(EShaderStage::Vertex) |
    static_cast<uint8>(EShaderStage::Pixel));

bool IsFiniteMatrix(const Math::Matrix4f& Matrix) noexcept
{
    for (const float Value : Matrix.Values)
    {
        if (!std::isfinite(Value))
        {
            return false;
        }
    }
    return true;
}

bool IsFiniteNonNegativeRgb(const Math::Vector4f& Color) noexcept
{
    for (std::size_t Component = 0; Component < 3; ++Component)
    {
        if (!std::isfinite(Color.Values[Component]) || Color.Values[Component] < 0.0f)
        {
            return false;
        }
    }
    return true;
}

bool IsPipelineValid(const FRALPipeline_Graphics& Pipeline) noexcept
{
    const FRALPipelineDesc_Graphics& Desc = Pipeline.GetDesc();
    return Desc.RenderTargetCount == 1 &&
        Desc.RenderTargetFormats[0] == SceneColorFormat &&
        Desc.DepthStencilFormat == SceneDepthFormat &&
        !Desc.BlendState.bEnable &&
        Desc.DepthStencilState.bDepthTestEnable &&
        Desc.DepthStencilState.bDepthWriteEnable &&
        Desc.DepthStencilState.DepthFunc == ECompareFunction::Less &&
        Desc.VertexBindings.Size() == 1 &&
        Desc.VertexBindings[0].Binding == 0 &&
        Desc.VertexBindings[0].Stride > 0 &&
        !Desc.VertexBindings[0].bPerInstance &&
        Desc.PushConstants.StageFlags == BasePassPushStages &&
        Desc.PushConstants.Size == sizeof(FBasePassObjectConstants) &&
        Desc.PushConstants.IsValid();
}

bool HasBufferUsage(const FRALBuffer& Buffer, const EBufferUsageFlags Usage) noexcept
{
    return (Buffer.GetDesc().UsageFlag & static_cast<uint32>(Usage)) != 0;
}

bool FitsBuffer(
    const uint32 ElementCount,
    const uint32 ElementSize,
    const uint64 BufferSize) noexcept
{
    return ElementCount > 0 && ElementSize > 0 && BufferSize > 0 &&
        static_cast<uint64>(ElementCount) <= BufferSize / ElementSize;
}

bool IsTextureContractValid(
    const FBasePassResources& Resources,
    uint32& OutWidth,
    uint32& OutHeight) noexcept
{
    if (Resources.SceneColorTexture == nullptr || Resources.SceneColorView == nullptr ||
        Resources.SceneDepthTexture == nullptr || Resources.SceneDepthView == nullptr ||
        Resources.SceneColorView->GetTexture() != Resources.SceneColorTexture ||
        Resources.SceneDepthView->GetTexture() != Resources.SceneDepthTexture)
    {
        return false;
    }

    const FRALTextureDesc& Color = Resources.SceneColorTexture->GetDesc();
    const FRALTextureDesc& Depth = Resources.SceneDepthTexture->GetDesc();
    const FRALTextureViewDesc& ColorView = Resources.SceneColorView->GetDesc();
    const FRALTextureViewDesc& DepthView = Resources.SceneDepthView->GetDesc();
    if (Color.Width == 0 || Color.Height == 0 || Color.Width != Depth.Width ||
        Color.Height != Depth.Height || Color.Format != SceneColorFormat ||
        Depth.Format != SceneDepthFormat || ColorView.Format != SceneColorFormat ||
        DepthView.Format != SceneDepthFormat || !Color.bIsRenderTarget ||
        !Depth.bIsDepthStencil)
    {
        return false;
    }
    OutWidth = Color.Width;
    OutHeight = Color.Height;
    return true;
}

} // namespace

FBasePass::FBasePass(const FBasePassResources& InResources) noexcept
    : Resources(InResources)
{
}

bool FBasePass::IsConfigurationValid(
    const FBasePassResources& InResources,
    const FRenderScene& RenderScene) noexcept
{
    uint32 Width = 0;
    uint32 Height = 0;
    if (!IsTextureContractValid(InResources, Width, Height) ||
        RenderScene.Cameras.Size() != 1)
    {
        return false;
    }

    const FRenderCameraProxy& Camera = RenderScene.Cameras[0];
    if (!Camera.PrimitiveId.IsValid() || Camera.ViewportWidth != Width ||
        Camera.ViewportHeight != Height || !IsFiniteMatrix(Camera.View) ||
        !IsFiniteMatrix(Camera.Projection) || !IsFiniteMatrix(Camera.ViewProjection))
    {
        return false;
    }

    for (const FRenderMeshProxy& Mesh : RenderScene.Meshes)
    {
        if (!Mesh.PrimitiveId.IsValid() ||
            !Mesh.MaterialAssetId.IsValid() ||
            !HasRenderMeshPass(Mesh.PassMask, ERenderMeshPassMask::SceneColor) ||
            Mesh.GraphicsPipeline == nullptr || Mesh.VertexBuffer == nullptr ||
            Mesh.VertexCount == 0 || Mesh.IndexBuffer == nullptr ||
            Mesh.IndexCount == 0 || !IsValidIndexFormat(Mesh.IndexFormat) ||
            !IsFiniteMatrix(Mesh.LocalToWorld) || !IsFiniteNonNegativeRgb(Mesh.BaseColor) ||
            !IsPipelineValid(*Mesh.GraphicsPipeline))
        {
            return false;
        }
        const FRALPipelineDesc_Graphics& PipelineDesc = Mesh.GraphicsPipeline->GetDesc();
        const uint32 VertexStride = PipelineDesc.VertexBindings[0].Stride;
        const uint32 IndexStride = Mesh.IndexFormat == EPixelFormat::R16_UINT ? 2u : 4u;
        if (!HasBufferUsage(*Mesh.VertexBuffer, EBufferUsageFlags::VertexBuffer) ||
            !HasBufferUsage(*Mesh.IndexBuffer, EBufferUsageFlags::IndexBuffer) ||
            !FitsBuffer(Mesh.VertexCount, VertexStride, Mesh.VertexBuffer->GetDesc().Size) ||
            !FitsBuffer(Mesh.IndexCount, IndexStride, Mesh.IndexBuffer->GetDesc().Size))
        {
            return false;
        }
        const Math::Matrix4f LocalToClip = Camera.ViewProjection * Mesh.LocalToWorld;
        if (!IsFiniteMatrix(LocalToClip))
        {
            return false;
        }
    }
    return true;
}

const char* FBasePass::GetPassName() const
{
    return "BasePass";
}

ERFGQueueType FBasePass::GetQueueType() const
{
    return ERFGQueueType::Graphics;
}

void FBasePass::Setup(FRenderPassSetupContext& Context)
{
    if (Context.GraphBridge == nullptr || Context.RenderScene == nullptr ||
        !IsConfigurationValid(Resources, *Context.RenderScene))
    {
        return;
    }

    FRFGAccessDesc ColorWrite;
    ColorWrite.Access = ERFGAccessType::Write;
    ColorWrite.State = ERALResourceState::RenderTarget;
    ColorWrite.PipelineStage = ERFGPipelineStage::Graphics;
    FRFGAccessDesc DepthAccess = ColorWrite;
    DepthAccess.State = ERALResourceState::DepthStencilWrite;

    const FRFGResourceHandle SceneColor = Context.GraphBridge->ImportTexture(
        "SceneColor", Resources.SceneColorTexture, ERALResourceState::Undefined);
    const FRFGResourceHandle SceneDepth = Context.GraphBridge->ImportTexture(
        "Depth", Resources.SceneDepthTexture, ERALResourceState::Undefined);
    Context.GraphBridge->Write(Context.PassHandle, SceneColor, ColorWrite);
    Context.GraphBridge->Write(Context.PassHandle, SceneDepth, DepthAccess);
    Context.GraphBridge->MarkOutput(SceneColor);
    Context.GraphBridge->MarkOutput(SceneDepth);

    FRFGAccessDesc VertexRead;
    VertexRead.Access = ERFGAccessType::Read;
    VertexRead.State = ERALResourceState::VertexBuffer;
    VertexRead.ShaderStage = EShaderStage::Vertex;
    VertexRead.PipelineStage = ERFGPipelineStage::Graphics;
    FRFGAccessDesc IndexRead = VertexRead;
    IndexRead.State = ERALResourceState::IndexBuffer;
    for (const FRenderMeshProxy& Mesh : Context.RenderScene->Meshes)
    {
        const FRFGResourceHandle VertexBuffer = Context.GraphBridge->ImportBuffer(
            "BasePassVertexBuffer", Mesh.VertexBuffer, ERALResourceState::VertexBuffer);
        const FRFGResourceHandle IndexBuffer = Context.GraphBridge->ImportBuffer(
            "BasePassIndexBuffer", Mesh.IndexBuffer, ERALResourceState::IndexBuffer);
        Context.GraphBridge->Read(Context.PassHandle, VertexBuffer, VertexRead);
        Context.GraphBridge->Read(Context.PassHandle, IndexBuffer, IndexRead);
    }
}

void FBasePass::Record(FRenderPassRecordContext& Context)
{
    if (Context.PassContext == nullptr || Context.RenderScene == nullptr ||
        !IsConfigurationValid(Resources, *Context.RenderScene))
    {
        return;
    }
    FRALCommandList* const CommandList = Context.PassContext->GetCommandList();
    if (CommandList == nullptr)
    {
        return;
    }

    const FRenderScene& RenderScene = *Context.RenderScene;
    if (!RenderScene.Meshes.IsEmpty())
    {
        // Vulkan dynamic rendering validates attachment formats against the
        // current pipeline at BeginRenderPass. Each object is still rebound
        // explicitly below so snapshot order is the sole draw order.
        CommandList->SetGraphicsPipeline(RenderScene.Meshes.Front().GraphicsPipeline);
    }

    const FRALTextureDesc& ColorDesc = Resources.SceneColorTexture->GetDesc();
    FRALRenderPassDesc RenderPass;
    RenderPass.RenderArea.Width = ColorDesc.Width;
    RenderPass.RenderArea.Height = ColorDesc.Height;
    RenderPass.ColorAttachmentCount = 1;
    RenderPass.ColorAttachments[0].RenderTarget = Resources.SceneColorView;
    RenderPass.ColorAttachments[0].LoadOp = EAttachmentLoadOp::Clear;
    RenderPass.ColorAttachments[0].StoreOp = EAttachmentStoreOp::Store;
    RenderPass.ColorAttachments[0].ClearColor[0] = 0.04f;
    RenderPass.ColorAttachments[0].ClearColor[1] = 0.08f;
    RenderPass.ColorAttachments[0].ClearColor[2] = 0.12f;
    RenderPass.ColorAttachments[0].ClearColor[3] = 1.0f;
    RenderPass.bHasDepthStencil = true;
    RenderPass.DepthStencilAttachment.DepthStencilTarget = Resources.SceneDepthView;
    RenderPass.DepthStencilAttachment.LoadOp = EAttachmentLoadOp::Clear;
    RenderPass.DepthStencilAttachment.StoreOp = EAttachmentStoreOp::Store;
    RenderPass.DepthStencilAttachment.ClearDepth = 1.0f;
    RenderPass.DepthStencilAttachment.ClearStencil = 0;
    CommandList->BeginRenderPass(RenderPass);

    FRALViewport Viewport;
    Viewport.Width = static_cast<float>(ColorDesc.Width);
    Viewport.Height = static_cast<float>(ColorDesc.Height);
    CommandList->SetViewport(Viewport);
    FRALScissorRect Scissor;
    Scissor.Width = static_cast<int32>(ColorDesc.Width);
    Scissor.Height = static_cast<int32>(ColorDesc.Height);
    CommandList->SetScissorRect(Scissor);

    const FRenderCameraProxy& Camera = RenderScene.Cameras[0];
    for (const FRenderMeshProxy& Mesh : RenderScene.Meshes)
    {
        FBasePassObjectConstants Constants;
        Constants.LocalToClip = Camera.ViewProjection * Mesh.LocalToWorld;
        Constants.BaseColor = Math::Vector4f(
            Mesh.BaseColor.Values[0], Mesh.BaseColor.Values[1],
            Mesh.BaseColor.Values[2], 1.0f);
        CommandList->SetGraphicsPipeline(Mesh.GraphicsPipeline);
        CommandList->SetVertexBuffer(0, Mesh.VertexBuffer, 0);
        CommandList->SetIndexBuffer(Mesh.IndexBuffer, 0, Mesh.IndexFormat);
        CommandList->SetPushConstants(
            BasePassPushStages, &Constants, sizeof(FBasePassObjectConstants));
        CommandList->DrawIndexed(Mesh.IndexCount, 1, 0, 0, 0);
    }
    CommandList->EndRenderPass();
}

FBasePassPipeline::FBasePassPipeline(const FBasePassResources& InResources) noexcept
    : Resources(InResources)
    , Pass(InResources)
{
}

const char* FBasePassPipeline::GetName() const
{
    return "BasePassPipeline";
}

EShadingPath FBasePassPipeline::GetShadingPath() const
{
    return EShadingPath::Deferred;
}

void FBasePassPipeline::BuildPasses(
    const FRendererFrameContext& FrameContext,
    const FRenderScene& RenderScene,
    const FRenderView& RenderView,
    FRenderPipelinePlan& OutPlan)
{
    static_cast<void>(FrameContext);
    static_cast<void>(RenderView);
    OutPlan.Reset();
    if (FBasePass::IsConfigurationValid(Resources, RenderScene))
    {
        static_cast<void>(OutPlan.Passes.TryPushBack(&Pass));
    }
}

} // namespace LE
