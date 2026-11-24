#include "Renderer/SceneRenderer.h"

#include "Renderer/BasePassCompositePipeline.h"
#include "Renderer/Passes/DirectionalShadow.h"
#include "Renderer/RenderAssetProvider.h"
#include "Renderer/RenderResourceOwnership.h"
#include "Renderer/Renderer.h"
#include "RHI/RHIBindGroup.h"
#include "RHI/RHIPipeline.h"
#include "RHI/RHISampler.h"
#include "RHI/RHIShader.h"
#include "RHI/RHITexture.h"

namespace LE
{
namespace
{

constexpr EPixelFormat SceneColorFormat = EPixelFormat::B8G8R8A8_SRGB;
constexpr EPixelFormat SceneDepthFormat = EPixelFormat::D32_FLOAT;

UniquePtr<FRHIShader> CreateShader(FRHIDevice& Device, const EShaderStage Stage, const FSceneShaderBytecode& Bytecode)
{
    String EntryPoint;
    if (Bytecode.Code.IsEmpty() || Bytecode.EntryPoint.IsEmpty() || !EntryPoint.TryAssign(Bytecode.EntryPoint))
    {
        return {};
    }
    return OwnRHI(Device.CreateShaderFromFile(Stage, Bytecode.Code.Data(), Bytecode.Code.Size(), EntryPoint));
}

UniquePtr<FRHIPipeline_Graphics> CreateBasePassPipeline(FRHIDevice& Device, FRHIShader& VertexShader, FRHIShader& PixelShader,
                                                       FRHIBindGroupLayout& MaterialLayout, FRHIBindGroupLayout& LightLayout,
                                                       FRHIBindGroupLayout* ShadowLayout)
{
    FRHIPipelineDesc_Graphics Desc;
    if (!Desc.Name.TryAssign("BasePassPipeline") ||
        !Desc.VertexBindings.TryPushBack({0, sizeof(FBasePassVertex), false}) ||
        !Desc.VertexAttributes.TryPushBack({0, 0, EPixelFormat::R32G32B32_FLOAT, offsetof(FBasePassVertex, Position)}) ||
        !Desc.VertexAttributes.TryPushBack({1, 0, EPixelFormat::R32G32B32_FLOAT, offsetof(FBasePassVertex, Color)}) ||
        !Desc.VertexAttributes.TryPushBack({2, 0, EPixelFormat::R32G32_FLOAT, offsetof(FBasePassVertex, UV)}) ||
        !Desc.VertexAttributes.TryPushBack({3, 0, EPixelFormat::R32G32B32_FLOAT, offsetof(FBasePassVertex, Normal)}) ||
        !Desc.BindGroupLayouts.TryPushBack(&MaterialLayout) || !Desc.BindGroupLayouts.TryPushBack(&LightLayout) ||
        (ShadowLayout != nullptr && !Desc.BindGroupLayouts.TryPushBack(ShadowLayout)))
    {
        return {};
    }
    Desc.VertexShader = &VertexShader;
    Desc.PixelShader = &PixelShader;
    Desc.RenderTargetCount = 1;
    Desc.RenderTargetFormats[0] = SceneColorFormat;
    Desc.DepthStencilFormat = SceneDepthFormat;
    Desc.DepthStencilState.bDepthTestEnable = true;
    Desc.DepthStencilState.bDepthWriteEnable = true;
    Desc.DepthStencilState.DepthFunc = ECompareFunction::Less;
    Desc.RasterizerState.CullMode = ECullMode::None;
    Desc.RasterizerState.FillMode = EFillMode::Solid;
    Desc.BlendState.bEnable = false;
    Desc.PushConstants.StageFlags = EShaderStage::Vertex | EShaderStage::Pixel;
    Desc.PushConstants.Size = sizeof(FBasePassObjectConstants);
    return OwnRHI(Device.CreateGraphicsPipeline(Desc));
}

UniquePtr<FRHIBindGroupLayout> CreateCompositeLayout(FRHIDevice& Device)
{
    FRHIBindGroupLayoutDesc Desc;
    Desc.SetIndex = 0;
    if (!Desc.Name.TryAssign("CompositeSceneColorLayout") ||
        !Desc.Bindings.TryPushBack({0, 1, ERHIBindGroupItemType::SampledImage, EShaderStage::Pixel}) ||
        !Desc.Bindings.TryPushBack({1, 1, ERHIBindGroupItemType::Sampler, EShaderStage::Pixel}))
    {
        return {};
    }
    return OwnRHI(Device.CreateBindGroupLayout(Desc));
}

UniquePtr<FRHISampler> CreateCompositeSampler(FRHIDevice& Device)
{
    FRHISamplerDesc Desc;
    if (!Desc.Name.TryAssign("RFGSceneColorSampler"))
    {
        return {};
    }
    Desc.AddressU = ESamplerAddressMode::ClampToEdge;
    Desc.AddressV = ESamplerAddressMode::ClampToEdge;
    Desc.AddressW = ESamplerAddressMode::ClampToEdge;
    return OwnRHI(Device.CreateSampler(Desc));
}

UniquePtr<FRHIBindGroup> CreateCompositeBinding(FRHIDevice& Device, FRHIBindGroupLayout& Layout, FRHISampler& Sampler,
                                                FRHITextureView& ColorView)
{
    FRHIBindGroupDesc Desc;
    Desc.Layout = &Layout;
    FRHIBindGroupItem Image;
    Image.Binding = 0;
    Image.TextureView = &ColorView;
    FRHIBindGroupItem Sampling;
    Sampling.Binding = 1;
    Sampling.Sampler = &Sampler;
    if (!Desc.Name.TryAssign("CompositeSceneColorBindGroup") ||
        !Desc.Items.TryPushBack(Image) || !Desc.Items.TryPushBack(Sampling))
    {
        return {};
    }
    return OwnRHI(Device.CreateBindGroup(Desc));
}

UniquePtr<FRHIPipeline_Graphics> CreateCompositePipeline(FRHIDevice& Device, FRHIShader& VertexShader, FRHIShader& PixelShader,
                                                         FRHIBindGroupLayout& Layout, const EPixelFormat Format)
{
    FRHIPipelineDesc_Graphics Desc;
    if (!Desc.Name.TryAssign("CompositePipeline") || !Desc.BindGroupLayouts.TryPushBack(&Layout))
    {
        return {};
    }
    Desc.VertexShader = &VertexShader;
    Desc.PixelShader = &PixelShader;
    Desc.RenderTargetCount = 1;
    Desc.RenderTargetFormats[0] = Format;
    Desc.DepthStencilFormat = EPixelFormat::Unknown;
    Desc.DepthStencilState.bDepthTestEnable = false;
    Desc.DepthStencilState.bDepthWriteEnable = false;
    Desc.RasterizerState.CullMode = ECullMode::None;
    Desc.RasterizerState.FillMode = EFillMode::Solid;
    Desc.BlendState.bEnable = false;
    return OwnRHI(Device.CreateGraphicsPipeline(Desc));
}

struct FSceneTargets
{
    UniquePtr<FRHITexture> Color;
    UniquePtr<FRHITexture> Depth;
    UniquePtr<FRHITextureView> ColorView;
    UniquePtr<FRHITextureView> DepthView;
    UniquePtr<FRHIBindGroup> CompositeBinding;
    UniquePtr<FRHIPipeline_Graphics> CompositePipeline;
    FSceneRenderTargetDesc Target;
};

struct FBasePassShaderPipeline
{
    // Destruction releases the pipeline before the shader it references and
    // the shader before the aligned bytecode its RHI descriptor borrows.
    Array<uint32> Bytecode;
    UniquePtr<FRHIShader> Shader;
    UniquePtr<FRHIPipeline_Graphics> Pipeline;
};

UniquePtr<FRHITextureView> CreateTargetView(FRHIDevice& Device, FRHITexture& Texture)
{
    FRHITextureViewDesc Desc;
    Desc.Texture = &Texture;
    Desc.Format = Texture.GetDesc().Format;
    return OwnRHI(Device.CreateTextureView(Desc));
}

} // namespace

struct FSceneRenderer::FState
{
    explicit FState(FRHIDevice& InDevice) noexcept : Device(InDevice) {}

    EBasePassPixelShaderInstallResult CreateBasePass(const FSceneShaderBytecode& Bytecode, UniquePtr<FBasePassShaderPipeline>& OutPipeline)
    {
        using EResult = EBasePassPixelShaderInstallResult;
        if (Bytecode.Code.IsEmpty() || Bytecode.EntryPoint.IsEmpty())
        {
            return EResult::InvalidArguments;
        }
        auto Candidate = TryMakeUnique<FBasePassShaderPipeline>();
        const auto Size = Bytecode.Code.Size();
        const auto StorageSize = Size / sizeof(uint32) + (Size % sizeof(uint32) != 0);
        if (!Candidate || !Candidate->Bytecode.TryResize(StorageSize))
        {
            return EResult::ShaderCreationFailed;
        }
        auto* const OwnedBytes = reinterpret_cast<char*>(Candidate->Bytecode.Data());
        for (std::size_t Index = 0; Index < Size; ++Index)
        {
            OwnedBytes[Index] = Bytecode.Code.Data()[Index];
        }
        Candidate->Shader = CreateShader(Device, EShaderStage::Pixel, {{OwnedBytes, Size}, Bytecode.EntryPoint});
        if (!Candidate->Shader)
        {
            return EResult::ShaderCreationFailed;
        }
        Candidate->Pipeline = CreateBasePassPipeline(Device, *BasePassVertex, *Candidate->Shader, *MaterialLayout,
                                                      *FrameLighting.GetLayout(), ShadowMaps.GetLayout());
        if (!Candidate->Pipeline)
        {
            return EResult::PipelineCreationFailed;
        }
        OutPipeline = std::move(Candidate);
        return EResult::Success;
    }

    UniquePtr<FSceneTargets> CreateTargets(const FSceneRenderTargetDesc& Target)
    {
        if (Target.Width == 0 || Target.Height == 0 || Target.Format == EPixelFormat::Unknown)
        {
            return {};
        }
        auto Candidate = TryMakeUnique<FSceneTargets>();
        FRHITextureDesc Color;
        FRHITextureDesc Depth;
        if (!Candidate || !Color.Name.TryAssign("SceneColor") || !Depth.Name.TryAssign("SceneDepth"))
        {
            return {};
        }
        Color.Width = Target.Width;
        Color.Height = Target.Height;
        Color.Format = SceneColorFormat;
        Color.bIsRenderTarget = true;
        Color.bIsShaderResource = true;
        Depth.Width = Target.Width;
        Depth.Height = Target.Height;
        Depth.Format = SceneDepthFormat;
        Depth.bIsDepthStencil = true;
        Candidate->Color = OwnRHI(Device.CreateTexture(Color));
        Candidate->Depth = OwnRHI(Device.CreateTexture(Depth));
        if (!Candidate->Color || !Candidate->Depth)
        {
            return {};
        }
        Candidate->ColorView = CreateTargetView(Device, *Candidate->Color);
        Candidate->DepthView = CreateTargetView(Device, *Candidate->Depth);
        if (!Candidate->ColorView || !Candidate->DepthView)
        {
            return {};
        }
        Candidate->CompositeBinding = CreateCompositeBinding(Device, *CompositeLayout, *CompositeSampler, *Candidate->ColorView);
        Candidate->CompositePipeline = CreateCompositePipeline(Device, *CompositeVertex, *CompositePixel, *CompositeLayout, Target.Format);
        if (!Candidate->CompositeBinding || !Candidate->CompositePipeline)
        {
            return {};
        }
        Candidate->Target = Target;
        return Candidate;
    }

    FRHIDevice& Device;
    FRenderFrameScheduler* FrameScheduler = nullptr;
    UniquePtr<FRHIShader> BasePassVertex;
    UniquePtr<FRHIShader> CompositeVertex;
    UniquePtr<FRHIShader> CompositePixel;
    UniquePtr<FRHIShader> ShadowDepthVertex;
    UniquePtr<FRHIBindGroupLayout> MaterialLayout;
    UniquePtr<FRHIBindGroupLayout> CompositeLayout;
    UniquePtr<FRHISampler> CompositeSampler;
    FBasePassFrameLighting FrameLighting;
    FDirectionalShadowMaps ShadowMaps;
    UniquePtr<FBasePassShaderPipeline> BasePass;
    UniquePtr<FSceneTargets> Targets;
    FRenderer Graph;
};

FSceneRenderer::FSceneRenderer() = default;
FSceneRenderer::~FSceneRenderer() = default;

bool FSceneRenderer::Initialize(FRHIDevice& Device, const FSceneRenderShaders& Shaders, const FSceneRenderTargetDesc& Target)
{
    if (State)
    {
        return false;
    }
    auto Candidate = TryMakeUnique<FState>(Device);
    if (!Candidate)
    {
        return false;
    }
    Candidate->BasePassVertex = CreateShader(Device, EShaderStage::Vertex, Shaders.BasePassVertex);
    Candidate->CompositeVertex = CreateShader(Device, EShaderStage::Vertex, Shaders.CompositeVertex);
    Candidate->CompositePixel = CreateShader(Device, EShaderStage::Pixel, Shaders.CompositePixel);
    Candidate->MaterialLayout = OwnRHI(CreateBasePassMaterialLayout(Device));
    Candidate->CompositeLayout = CreateCompositeLayout(Device);
    Candidate->CompositeSampler = CreateCompositeSampler(Device);
    if (!Candidate->BasePassVertex || !Candidate->CompositeVertex || !Candidate->CompositePixel ||
        !Candidate->MaterialLayout || !Candidate->CompositeLayout || !Candidate->CompositeSampler || !Candidate->FrameLighting.Initialize(Device))
    {
        return false;
    }
    if (!Shaders.ShadowDepthVertex.Code.IsEmpty())
    {
        Candidate->ShadowDepthVertex = CreateShader(Device, EShaderStage::Vertex, Shaders.ShadowDepthVertex);
        if (!Candidate->ShadowDepthVertex || !Candidate->ShadowMaps.Initialize(Device, *Candidate->ShadowDepthVertex))
        {
            return false;
        }
    }
    if (Candidate->CreateBasePass(Shaders.BasePassPixel, Candidate->BasePass) != EBasePassPixelShaderInstallResult::Success)
    {
        return false;
    }
    Candidate->Targets = Candidate->CreateTargets(Target);
    if (!Candidate->Targets)
    {
        return false;
    }
    Candidate->Graph.Initialize();
    State = std::move(Candidate);
    return true;
}

bool FSceneRenderer::InitializeAssetProvider(FRenderAssetProvider& Provider) const noexcept
{
    return State && Provider.Initialize(&State->Device, State->MaterialLayout.Get());
}

FSceneRenderTargetDesc FSceneRenderer::GetTargetDesc() const noexcept
{
    return State ? State->Targets->Target : FSceneRenderTargetDesc{};
}

bool FSceneRenderer::Resize(FRenderFrameScheduler& Scheduler, const FSceneRenderTargetDesc& Target)
{
    if (!State)
    {
        return false;
    }
    const FSceneRenderTargetDesc& Current = State->Targets->Target;
    if (Current.Width == Target.Width && Current.Height == Target.Height && Current.Format == Target.Format)
    {
        return true;
    }
    auto Candidate = State->CreateTargets(Target);
    if (!Candidate)
    {
        return false;
    }
    Scheduler.WaitForAllFrames();
    State->Targets = std::move(Candidate);
    return true;
}

EBasePassPixelShaderInstallResult FSceneRenderer::InstallBasePassPixelShader(FRenderFrameScheduler& Scheduler, const FSceneShaderBytecode& Bytecode)
{
    using EResult = EBasePassPixelShaderInstallResult;
    if (!State || (State->FrameScheduler != nullptr && State->FrameScheduler != &Scheduler) ||
        !Scheduler.IsAtFrameBoundaryFor(State->Device))
    {
        return EResult::InvalidArguments;
    }
    UniquePtr<FBasePassShaderPipeline> Candidate;
    const EResult Created = State->CreateBasePass(Bytecode, Candidate);
    if (Created != EResult::Success)
    {
        return Created;
    }
    Scheduler.WaitForAllFrames();
    if (!Scheduler.IsAtFrameBoundaryFor(State->Device))
    {
        return EResult::FrameRetirementFailed;
    }
    State->BasePass = std::move(Candidate);
    return EResult::Success;
}

ERenderFrameRecordResult FSceneRenderer::RecordFrame(FRenderFrameScope& Frame, FRHISwapchain& Swapchain, const FRenderScene& Scene)
{
    if (!State || Frame.Scheduler == nullptr || Frame.Scheduler->Device != &State->Device ||
        (State->FrameScheduler != nullptr && State->FrameScheduler != Frame.Scheduler))
    {
        return ERenderFrameRecordResult::InvalidArguments;
    }
    State->FrameScheduler = Frame.Scheduler;
    const FSceneTargets& Targets = *State->Targets;
    FBasePassCompositePipelineDesc Desc;
    Desc.BasePassResources.SceneColorTexture = Targets.Color.Get();
    Desc.BasePassResources.SceneColorView = Targets.ColorView.Get();
    Desc.BasePassResources.SceneDepthTexture = Targets.Depth.Get();
    Desc.BasePassResources.SceneDepthView = Targets.DepthView.Get();
    Desc.BasePassResources.Pipeline = State->BasePass->Pipeline.Get();
    if (!State->FrameLighting.WriteSlot(Frame.GetSlotIndex(), {Scene.Lights.Data(), Scene.Lights.Size()}, Desc.BasePassResources.Light))
    {
        return ERenderFrameRecordResult::Error;
    }
    Desc.BackBufferView = Frame.GetBackBufferView();
    Desc.CompositePipeline = Targets.CompositePipeline.Get();
    Desc.CompositeBindGroup = Targets.CompositeBinding.Get();
    FDirectionalShadowFrame ShadowFrame;
    const bool bHasShadowPass = State->ShadowMaps.GetLayout() != nullptr;
    if (bHasShadowPass && !State->ShadowMaps.PrepareFrame(Frame.GetSlotIndex(), Scene, ShadowFrame))
    {
        return ERenderFrameRecordResult::Error;
    }
    FDirectionalShadowPass ShadowPass(ShadowFrame);
    if (bHasShadowPass)
    {
        Desc.BasePassResources.Shadow = ShadowFrame.Receiver;
        Desc.ShadowPass = &ShadowPass;
    }
    FBasePassCompositePipeline Pipeline(Desc);

    FRendererFrameContext Context;
    Context.FrameIndex = Frame.GetFrameIndex();
    Context.Device = &State->Device;
    Context.Swapchain = &Swapchain;
    Context.CommandList = Frame.GetCommandList();
    Context.FrameScope = &Frame;
    Context.ViewFamily.PrimarySwapchain = &Swapchain;
    Context.Pipeline = &Pipeline;
    return State->Graph.RenderFrame(Context, &Scene);
}

void FSceneRenderer::Shutdown() noexcept
{
    State.Reset();
}

} // namespace LE
