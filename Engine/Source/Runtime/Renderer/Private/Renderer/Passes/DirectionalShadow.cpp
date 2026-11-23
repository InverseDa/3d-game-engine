#include "Renderer/Passes/DirectionalShadow.h"

#include "Execute/RFGPassContext.h"
#include "RHI/RHIBindGroup.h"
#include "RHI/RHIBuffer.h"
#include "RHI/RHICommandList.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHISampler.h"
#include "RHI/RHITexture.h"
#include "Renderer/RenderAssetProvider.h"
#include "Renderer/RenderGraphBuilderBridge.h"

#include <cstring>

namespace LE
{
namespace
{

struct alignas(16) FShadowConstants
{
    Math::Matrix4f CameraClipToLight;
    Math::Vector4f BiasAndEnabled;
};

static_assert(sizeof(FShadowConstants) == FBasePassShadowBinding::ConstantBytes);
static_assert(sizeof(Math::Matrix4f) == 64);

Math::Matrix4f BuildLightProjection(const FRenderScene& Scene) noexcept
{
    const auto& ToLight = Scene.Lights.Front().DirectionToLight;
    const Math::Vector3d Ray(-ToLight[0], -ToLight[1], -ToLight[2]);
    const Math::Vector3d UpReference = Ray[1] * Ray[1] > 0.9 ? Math::Vector3d(1.0, 0.0, 0.0) : Math::Vector3d(0.0, 1.0, 0.0);
    const Math::Vector3d Right = Math::Vector3d::Cross(UpReference, Ray).Normalized();
    const Math::Vector3d Axes[] = {Right, Math::Vector3d::Cross(Ray, Right), Ray};
    Math::Matrix4f Projection = Math::Matrix4f::Identity();

    for (std::size_t Row = 0; Row < 3; ++Row)
    {
        double Min = 0.0;
        double Max = 0.0;
        bool bFirst = true;
        for (const FRenderShadowCaster& Caster : Scene.ShadowCasters)
        {
            double Low = 0.0;
            double High = 0.0;
            for (std::size_t Axis = 0; Axis < 3; ++Axis)
            {
                const double LowSide = Axes[Row][Axis] * Caster.WorldBounds.Min[Axis];
                const double HighSide = Axes[Row][Axis] * Caster.WorldBounds.Max[Axis];
                Low += LowSide < HighSide ? LowSide : HighSide;
                High += LowSide > HighSide ? LowSide : HighSide;
            }
            if (bFirst || Low < Min)
            {
                Min = Low;
            }
            if (bFirst || High > Max)
            {
                Max = High;
            }
            bFirst = false;
        }

        const double Length = Max - Min;
        const double Padding = 0.1 + Length * 0.01;
        const double Span = Length + 2.0 * Padding;
        const double Scale = (Row == 0 ? 2.0 : Row == 1 ? -2.0 : 1.0) / Span;
        const double Start = Row == 0 ? -1.0 : Row == 1 ? 1.0 : 0.0;
        for (std::size_t Axis = 0; Axis < 3; ++Axis)
        {
            Projection(Row, Axis) = static_cast<float>(Axes[Row][Axis] * Scale);
        }
        Projection(Row, 3) = static_cast<float>(Start - (Min - Padding) * Scale);
    }
    return Projection;
}

} // namespace

FDirectionalShadowMaps::~FDirectionalShadowMaps() noexcept
{
    Shutdown();
}

bool FDirectionalShadowMaps::Initialize(FRHIDevice& Device, FRHIShader& VertexShader) noexcept
{
    if (Layout != nullptr)
    {
        return false;
    }
    FRHIBindGroupLayoutDesc LayoutDesc;
    LayoutDesc.Name = "BasePassShadowLayout";
    LayoutDesc.SetIndex = FBasePassShadowBinding::SetIndex;
    LayoutDesc.Bindings.PushBack({0, 1, ERHIBindGroupItemType::UniformBuffer, EShaderStage::Pixel});
    LayoutDesc.Bindings.PushBack({1, 1, ERHIBindGroupItemType::SampledImage, EShaderStage::Pixel});
    LayoutDesc.Bindings.PushBack({2, 1, ERHIBindGroupItemType::Sampler, EShaderStage::Pixel});
    Layout = Device.CreateBindGroupLayout(LayoutDesc);

    FRHISamplerDesc SamplerDesc;
    SamplerDesc.Name = "ShadowDepthPoint";
    SamplerDesc.MinFilter = ESamplerFilter::Nearest;
    SamplerDesc.MagFilter = ESamplerFilter::Nearest;
    SamplerDesc.MipmapMode = ESamplerMipmapMode::Nearest;
    SamplerDesc.AddressU = ESamplerAddressMode::ClampToEdge;
    SamplerDesc.AddressV = ESamplerAddressMode::ClampToEdge;
    SamplerDesc.AddressW = ESamplerAddressMode::ClampToEdge;
    Sampler = Device.CreateSampler(SamplerDesc);
    if (Layout == nullptr || Sampler == nullptr)
    {
        Shutdown();
        return false;
    }

    FRHIPipelineDesc_Graphics PipelineDesc;
    PipelineDesc.Name = "ShadowDepthPipeline";
    PipelineDesc.VertexShader = &VertexShader;
    PipelineDesc.VertexBindings.PushBack({0, sizeof(FBasePassVertex), false});
    PipelineDesc.VertexAttributes.PushBack({0, 0, EPixelFormat::R32G32B32_FLOAT, offsetof(FBasePassVertex, Position)});
    PipelineDesc.RenderTargetCount = 0;
    PipelineDesc.DepthStencilFormat = EPixelFormat::D32_FLOAT;
    PipelineDesc.DepthStencilState.bDepthTestEnable = true;
    PipelineDesc.DepthStencilState.bDepthWriteEnable = true;
    PipelineDesc.DepthStencilState.DepthFunc = ECompareFunction::Less;
    PipelineDesc.RasterizerState.CullMode = ECullMode::None;
    PipelineDesc.PushConstants.StageFlags = EShaderStage::Vertex;
    PipelineDesc.PushConstants.Size = sizeof(Math::Matrix4f);
    DepthPipeline = Device.CreateGraphicsPipeline(PipelineDesc);
    if (DepthPipeline == nullptr)
    {
        Shutdown();
        return false;
    }

    for (FSlot& Slot : Slots)
    {
        FRHITextureDesc TextureDesc;
        TextureDesc.Name = "DirectionalShadowDepth";
        TextureDesc.Width = Resolution;
        TextureDesc.Height = Resolution;
        TextureDesc.Format = EPixelFormat::D32_FLOAT;
        TextureDesc.bIsDepthStencil = true;
        TextureDesc.bIsShaderResource = true;
        Slot.Receiver.DepthTexture = Device.CreateTexture(TextureDesc);

        FRHIBufferDesc BufferDesc;
        BufferDesc.Name = "BasePassFrameShadow";
        BufferDesc.Size = sizeof(FShadowConstants);
        BufferDesc.Usage = static_cast<uint32>(EResourceUsage::Upload);
        BufferDesc.UsageFlag = static_cast<uint32>(EBufferUsageFlags::UniformBuffer);
        Slot.Receiver.Constants = Device.CreateBuffer(BufferDesc);
        if (Slot.Receiver.DepthTexture == nullptr || Slot.Receiver.Constants == nullptr)
        {
            Shutdown();
            return false;
        }

        FRHITextureViewDesc ViewDesc;
        ViewDesc.Format = EPixelFormat::D32_FLOAT;
        ViewDesc.Texture = Slot.Receiver.DepthTexture;
        Slot.DepthView = Device.CreateTextureView(ViewDesc);
        if (Slot.DepthView == nullptr)
        {
            Shutdown();
            return false;
        }

        FRHIBindGroupDesc GroupDesc;
        GroupDesc.Name = "BasePassSlotShadow";
        GroupDesc.Layout = Layout;
        auto& ConstantItem = GroupDesc.Items.EmplaceBack();
        ConstantItem.Binding = 0;
        ConstantItem.Buffer = Slot.Receiver.Constants;
        ConstantItem.Range = sizeof(FShadowConstants);
        auto& DepthItem = GroupDesc.Items.EmplaceBack();
        DepthItem.Binding = 1;
        DepthItem.TextureView = Slot.DepthView;
        auto& SamplerItem = GroupDesc.Items.EmplaceBack();
        SamplerItem.Binding = 2;
        SamplerItem.Sampler = Sampler;
        Slot.Receiver.BindGroup = Device.CreateBindGroup(GroupDesc);
        if (Slot.Receiver.BindGroup == nullptr)
        {
            Shutdown();
            return false;
        }
    }
    return true;
}

void FDirectionalShadowMaps::Shutdown() noexcept
{
    for (FSlot& Slot : Slots)
    {
        RHI::DestroyResource(Slot.Receiver.BindGroup);
        RHI::DestroyResource(Slot.Receiver.Constants);
        RHI::DestroyResource(Slot.DepthView);
        RHI::DestroyResource(Slot.Receiver.DepthTexture);
        Slot = {};
    }
    RHI::DestroyResource(DepthPipeline);
    RHI::DestroyResource(Sampler);
    RHI::DestroyResource(Layout);
    DepthPipeline = nullptr;
    Sampler = nullptr;
    Layout = nullptr;
}

bool FDirectionalShadowMaps::PrepareFrame(const uint32 SlotIndex, const FRenderScene& Scene, FDirectionalShadowFrame& Out) const noexcept
{
    if (DepthPipeline == nullptr || SlotIndex >= FRenderFrameScheduler::FrameSlotCount || Scene.Cameras.Size() != 1)
    {
        return false;
    }
    const FSlot& Slot = Slots[SlotIndex];
    FDirectionalShadowFrame Candidate{Slot.Receiver, Slot.DepthView, DepthPipeline};
    FShadowConstants Constants{};
    if (!Scene.Lights.IsEmpty() && !Scene.ShadowCasters.IsEmpty())
    {
        Math::Matrix4f ClipToWorld;
        if (Math::TryInvertMatrix(Scene.Cameras.Front().ViewProjection, ClipToWorld))
        {
            Candidate.WorldToLight = BuildLightProjection(Scene);
            Constants.CameraClipToLight = Candidate.WorldToLight * ClipToWorld;
            Constants.BiasAndEnabled = Math::Vector4f(0.0025f, 0.0f, 0.0f, 1.0f);
            Candidate.bDrawCasters = true;
        }
    }

    void* const Mapping = Slot.Receiver.Constants->Map(0, sizeof(Constants));
    if (Mapping == nullptr)
    {
        return false;
    }
    // Copy the shader ABI bytes into the borrowed RHI mapping.
    std::memcpy(Mapping, &Constants, sizeof(Constants));
    Slot.Receiver.Constants->Unmap();
    Out = Candidate;
    return true;
}

void FDirectionalShadowPass::Setup(FRenderPassSetupContext& Context)
{
    FRFGAccessDesc DepthWrite;
    DepthWrite.Access = ERFGAccessType::Write;
    DepthWrite.State = ERHIResourceState::DepthStencilWrite;
    DepthWrite.PipelineStage = ERFGPipelineStage::Graphics;
    const auto Shadow = Context.GraphBridge->ImportTexture("DirectionalShadowDepth", Frame.Receiver.DepthTexture, ERHIResourceState::Undefined);
    Context.GraphBridge->Write(Context.PassHandle, Shadow, DepthWrite);
    if (!Frame.bDrawCasters)
    {
        return;
    }

    FRFGAccessDesc VertexRead;
    VertexRead.Access = ERFGAccessType::Read;
    VertexRead.State = ERHIResourceState::VertexBuffer;
    VertexRead.ShaderStage = EShaderStage::Vertex;
    VertexRead.PipelineStage = ERFGPipelineStage::Graphics;
    FRFGAccessDesc IndexRead = VertexRead;
    IndexRead.State = ERHIResourceState::IndexBuffer;
    HashSet<const FMeshAsset*> SeenMeshes;
    for (const FRenderShadowCaster& Caster : Context.RenderScene->ShadowCasters)
    {
        if (SeenMeshes.Insert(Caster.Mesh))
        {
            const auto Vertex = Context.GraphBridge->ImportBuffer("ShadowVertex", Caster.Mesh->VertexBuffer, VertexRead.State);
            const auto Index = Context.GraphBridge->ImportBuffer("ShadowIndex", Caster.Mesh->IndexBuffer, IndexRead.State);
            Context.GraphBridge->Read(Context.PassHandle, Vertex, VertexRead);
            Context.GraphBridge->Read(Context.PassHandle, Index, IndexRead);
        }
    }
}

void FDirectionalShadowPass::Record(FRenderPassRecordContext& Context)
{
    FRHICommandList* const Commands = Context.PassContext->GetCommandList();
    if (Commands == nullptr)
    {
        return;
    }
    Commands->SetGraphicsPipeline(Frame.DepthPipeline);
    constexpr uint32 Size = FDirectionalShadowMaps::Resolution;
    FRHIRenderPassDesc Pass;
    Pass.RenderArea.Width = Size;
    Pass.RenderArea.Height = Size;
    Pass.bHasDepthStencil = true;
    Pass.DepthStencilAttachment.DepthStencilTarget = Frame.DepthView;
    Pass.DepthStencilAttachment.LoadOp = EAttachmentLoadOp::Clear;
    Pass.DepthStencilAttachment.StoreOp = EAttachmentStoreOp::Store;
    Pass.DepthStencilAttachment.ClearDepth = 1.0f;
    Commands->BeginRenderPass(Pass);

    FRHIViewport Viewport;
    Viewport.Width = static_cast<float>(Size);
    Viewport.Height = static_cast<float>(Size);
    Commands->SetViewport(Viewport);
    FRHIScissorRect Scissor;
    Scissor.Width = static_cast<int32>(Size);
    Scissor.Height = static_cast<int32>(Size);
    Commands->SetScissorRect(Scissor);
    if (Frame.bDrawCasters)
    {
        for (const FRenderShadowCaster& Caster : Context.RenderScene->ShadowCasters)
        {
            const FMeshAsset& Mesh = *Caster.Mesh;
            const Math::Matrix4f LocalToLight = Frame.WorldToLight * Caster.LocalToWorld;
            Commands->SetVertexBuffer(0, Mesh.VertexBuffer, 0);
            Commands->SetIndexBuffer(Mesh.IndexBuffer, 0, Mesh.IndexFormat);
            Commands->SetPushConstants(EShaderStage::Vertex, &LocalToLight, sizeof(LocalToLight));
            Commands->DrawIndexed(Mesh.IndexCount, 1, 0, 0, 0);
        }
    }
    Commands->EndRenderPass();
}

} // namespace LE
