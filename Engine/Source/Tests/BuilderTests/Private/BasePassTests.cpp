#include "Renderer/BasePass.h"
#include "Renderer/BasePassCompositePipeline.h"

#include "Compile/RFGCompiledPlan.h"
#include "Compile/RFGCompiler.h"
#include "Execute/RFGPassContext.h"
#include "RAL/RALBindGroup.h"
#include "RAL/RALBuffer.h"
#include "RAL/RALCommandList.h"
#include "RAL/RALPipeline.h"
#include "RAL/RALTexture.h"
#include "Record/RFGBuilder.h"
#include "Record/RFGRecordedGraph.h"
#include "Renderer/RenderGraphBuilderBridge.h"
#include "Renderer/RendererFrameContext.h"
#include "Renderer/RenderView.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace LE
{
namespace
{

int Expect(const bool bCondition, const char* const Name)
{
    if (bCondition)
    {
        return 0;
    }
    std::fprintf(stderr, "FAILED: %s\n", Name);
    return 1;
}

constexpr EShaderStage BasePassStages = static_cast<EShaderStage>(
    static_cast<uint8>(EShaderStage::Vertex) |
    static_cast<uint8>(EShaderStage::Pixel));

class FBasePassTestBuffer final : public FRALBuffer
{
public:
    void* Map(uint64, uint64) override { return nullptr; }
    void Unmap() override {}
    const FRALBufferDesc& GetDesc() const override { return Desc; }

    FRALBufferDesc Desc;
};

class FBasePassTestTexture final : public FRALTexture
{
public:
    const FRALTextureDesc& GetDesc() const override { return Desc; }

    FRALTextureDesc Desc;
};

class FBasePassTestTextureView final : public FRALTextureView
{
public:
    FRALTexture* GetTexture() const override { return Desc.Texture; }
    const FRALTextureViewDesc& GetDesc() const override { return Desc; }

    FRALTextureViewDesc Desc;
};

class FBasePassTestPipeline final : public FRALPipeline_Graphics
{
public:
    const FRALPipelineDesc_Graphics& GetDesc() const override { return Desc; }

    FRALPipelineDesc_Graphics Desc;
};

class FBasePassTestBindGroupLayout final : public FRALBindGroupLayout
{
public:
    const FRALBindGroupLayoutDesc& GetDesc() const override { return Desc; }

    FRALBindGroupLayoutDesc Desc;
};

class FBasePassTestBindGroup final : public FRALBindGroup
{
public:
    const FRALBindGroupDesc& GetDesc() const override { return Desc; }

    FRALBindGroupDesc Desc;
};

enum class EBasePassCommandEvent : uint8
{
    BindPipeline,
    BeginRenderPass,
    SetViewport,
    SetScissor,
    SetVertex,
    SetIndex,
    PushConstants,
    DrawIndexed,
    EndRenderPass,
};

struct FRecordedIndexBinding
{
    FRALBuffer* Buffer = nullptr;
    EPixelFormat Format = EPixelFormat::Unknown;
};

struct FRecordedPushConstants
{
    EShaderStage Stage = EShaderStage::None;
    uint32 Size = 0;
    uint8 Bytes[sizeof(FBasePassObjectConstants)]{};
};

class FBasePassTestCommandList final : public FRALCommandList
{
public:
    void Begin() override {}
    void End() override {}
    void ResourceBarriers(const FRALBarrierBatch&) override {}

    void BeginRenderPass(const FRALRenderPassDesc& Desc) override
    {
        Events.PushBack(EBasePassCommandEvent::BeginRenderPass);
        RenderPass = Desc;
        ++BeginRenderPassCount;
    }
    void EndRenderPass() override
    {
        Events.PushBack(EBasePassCommandEvent::EndRenderPass);
        ++EndRenderPassCount;
    }
    void SetGraphicsPipeline(FRALPipeline_Graphics* Pipeline) override
    {
        Events.PushBack(EBasePassCommandEvent::BindPipeline);
        Pipelines.PushBack(Pipeline);
    }
    void SetViewport(const FRALViewport& InViewport) override
    {
        Events.PushBack(EBasePassCommandEvent::SetViewport);
        Viewport = InViewport;
    }
    void SetScissorRect(const FRALScissorRect& InScissor) override
    {
        Events.PushBack(EBasePassCommandEvent::SetScissor);
        Scissor = InScissor;
    }
    void SetVertexBuffer(uint32, FRALBuffer* Buffer, uint64) override
    {
        Events.PushBack(EBasePassCommandEvent::SetVertex);
        VertexBuffers.PushBack(Buffer);
    }
    void SetIndexBuffer(FRALBuffer* Buffer, uint64, EPixelFormat Format) override
    {
        Events.PushBack(EBasePassCommandEvent::SetIndex);
        IndexBuffers.PushBack(FRecordedIndexBinding{Buffer, Format});
    }
    void SetBindGroup(uint32, FRALBindGroup*) override {}
    void SetPushConstants(EShaderStage Stage, const void* Data, uint32 Size) override
    {
        Events.PushBack(EBasePassCommandEvent::PushConstants);
        FRecordedPushConstants Captured;
        Captured.Stage = Stage;
        Captured.Size = Size;
        if (Data != nullptr && Size == sizeof(Captured.Bytes))
        {
            std::memcpy(Captured.Bytes, Data, Size);
        }
        Pushes.PushBack(Captured);
    }
    void Draw(uint32, uint32, uint32) override {}
    void DrawIndexed(uint32 IndexCount, uint32, uint32, int32, uint32) override
    {
        Events.PushBack(EBasePassCommandEvent::DrawIndexed);
        DrawIndexCounts.PushBack(IndexCount);
    }

    Array<EBasePassCommandEvent> Events;
    Array<FRALPipeline_Graphics*> Pipelines;
    Array<FRALBuffer*> VertexBuffers;
    Array<FRecordedIndexBinding> IndexBuffers;
    Array<FRecordedPushConstants> Pushes;
    Array<uint32> DrawIndexCounts;
    FRALRenderPassDesc RenderPass;
    FRALViewport Viewport;
    FRALScissorRect Scissor;
    uint32 BeginRenderPassCount = 0;
    uint32 EndRenderPassCount = 0;
};

struct FBasePassFixture
{
    FBasePassFixture()
    {
        Color.Desc.Width = 640;
        Color.Desc.Height = 480;
        Color.Desc.Format = EPixelFormat::B8G8R8A8_SRGB;
        Color.Desc.bIsRenderTarget = true;
        Color.Desc.bIsShaderResource = true;
        ColorView.Desc.Texture = &Color;
        ColorView.Desc.Format = EPixelFormat::B8G8R8A8_SRGB;

        Depth.Desc.Width = 640;
        Depth.Desc.Height = 480;
        Depth.Desc.Format = EPixelFormat::D32_FLOAT;
        Depth.Desc.bIsDepthStencil = true;
        DepthView.Desc.Texture = &Depth;
        DepthView.Desc.Format = EPixelFormat::D32_FLOAT;

        Resources.SceneColorTexture = &Color;
        Resources.SceneColorView = &ColorView;
        Resources.SceneDepthTexture = &Depth;
        Resources.SceneDepthView = &DepthView;

        ConfigurePipeline(Pipelines[0]);
        ConfigurePipeline(Pipelines[1]);

        VertexBuffers[0].Desc.Size = 256;
        VertexBuffers[1].Desc.Size = 512;
        VertexBuffers[0].Desc.UsageFlag = static_cast<uint32>(EBufferUsageFlags::VertexBuffer);
        VertexBuffers[1].Desc.UsageFlag = static_cast<uint32>(EBufferUsageFlags::VertexBuffer);
        IndexBuffers[0].Desc.Size = 6;
        IndexBuffers[1].Desc.Size = 24;
        IndexBuffers[0].Desc.UsageFlag = static_cast<uint32>(EBufferUsageFlags::IndexBuffer);
        IndexBuffers[1].Desc.UsageFlag = static_cast<uint32>(EBufferUsageFlags::IndexBuffer);
        FAssetId::TryParse("ba5e0000-0000-4000-8000-000000000001", MaterialIds[0]);
        FAssetId::TryParse("ba5e0000-0000-4000-8000-000000000002", MaterialIds[1]);

        Scene.Cameras.TryEmplaceBack();
        FRenderCameraProxy& Camera = Scene.Cameras.Back();
        Camera.PrimitiveId = {7, 1};
        Camera.ViewportWidth = 640;
        Camera.ViewportHeight = 480;
        Camera.View = Math::Matrix4f::Identity();
        Camera.Projection = Math::Matrix4f::Identity();
        Camera.ViewProjection = Math::Matrix4f::Identity();

        Scene.Meshes.TryEmplaceBack();
        Scene.Meshes.TryEmplaceBack();
        ConfigureMesh(Scene.Meshes[0], 2, 0, EPixelFormat::R16_UINT, 3);
        ConfigureMesh(Scene.Meshes[1], 3, 1, EPixelFormat::R32_UINT, 6);
        Scene.Meshes[0].LocalToWorld(0, 3) = 2.0f;
        Scene.Meshes[0].BaseColor = Math::Vector4f(1.0f, 0.25f, 0.5f, 19.0f);
        Scene.Meshes[1].LocalToWorld(1, 3) = -3.0f;
        Scene.Meshes[1].BaseColor = Math::Vector4f(4.0f, 2.0f, 1.0f, -7.0f);
    }

    static void ConfigurePipeline(FBasePassTestPipeline& Pipeline)
    {
        Pipeline.Desc.RenderTargetCount = 1;
        Pipeline.Desc.RenderTargetFormats[0] = EPixelFormat::B8G8R8A8_SRGB;
        Pipeline.Desc.DepthStencilFormat = EPixelFormat::D32_FLOAT;
        Pipeline.Desc.DepthStencilState.bDepthTestEnable = true;
        Pipeline.Desc.DepthStencilState.bDepthWriteEnable = true;
        Pipeline.Desc.DepthStencilState.DepthFunc = ECompareFunction::Less;
        Pipeline.Desc.VertexBindings.TryEmplaceBack();
        Pipeline.Desc.VertexBindings.Back().Binding = 0;
        Pipeline.Desc.VertexBindings.Back().Stride = 24;
        Pipeline.Desc.VertexBindings.Back().bPerInstance = false;
        Pipeline.Desc.PushConstants.StageFlags = BasePassStages;
        Pipeline.Desc.PushConstants.Size = sizeof(FBasePassObjectConstants);
    }

    void ConfigureMesh(
        FRenderMeshProxy& Mesh,
        const uint64 PrimitiveValue,
        const uint32 ResourceIndex,
        const EPixelFormat IndexFormat,
        const uint32 IndexCount)
    {
        Mesh.PrimitiveId = {7, PrimitiveValue};
        Mesh.GraphicsPipeline = &Pipelines[ResourceIndex];
        Mesh.VertexBuffer = &VertexBuffers[ResourceIndex];
        Mesh.VertexCount = 3;
        Mesh.IndexBuffer = &IndexBuffers[ResourceIndex];
        Mesh.IndexCount = IndexCount;
        Mesh.IndexFormat = IndexFormat;
        Mesh.MaterialAssetId = MaterialIds[ResourceIndex];
        Mesh.PassMask = ERenderMeshPassMask::SceneColor;
        Mesh.LocalToWorld = Math::Matrix4f::Identity();
    }

    FBasePassTestTexture Color;
    FBasePassTestTexture Depth;
    FBasePassTestTextureView ColorView;
    FBasePassTestTextureView DepthView;
    FBasePassResources Resources;
    FBasePassTestPipeline Pipelines[2];
    FBasePassTestBuffer VertexBuffers[2];
    FBasePassTestBuffer IndexBuffers[2];
    FAssetId MaterialIds[2];
    FRenderScene Scene;
};

bool ProducesBasePassPlan(FBasePassFixture& Fixture)
{
    FRendererFrameContext Frame;
    FRenderView View;
    FBasePassPipeline Pipeline(Fixture.Resources);
    FRenderPipelinePlan Plan;
    Pipeline.BuildPasses(Frame, Fixture.Scene, View, Plan);
    return Plan.Passes.Size() == 1;
}

void RecordBasePass(
    FBasePass& Pass,
    const FRenderScene& Scene,
    FBasePassTestCommandList& CommandList)
{
    FRFGCompiledPlan Plan;
    Plan.GetMutablePasses().TryEmplaceBack();
    Plan.GetMutablePasses().Back().Queue = ERFGQueueType::Graphics;
    FRFGExecutionContext Execution;
    Execution.CommandList = &CommandList;
    FRFGPassContext PassContext;
    PassContext.SetExecutionContext(&Execution);
    PassContext.SetCompiledPlan(&Plan);
    PassContext.SetPassIndex(0);
    FRenderPassRecordContext RecordContext;
    RecordContext.PassContext = &PassContext;
    RecordContext.RenderScene = &Scene;
    Pass.Record(RecordContext);
}

bool MatrixEquals(const Math::Matrix4f& Left, const Math::Matrix4f& Right) noexcept
{
    for (std::size_t Index = 0; Index < 16; ++Index)
    {
        if (Left.Values[Index] != Right.Values[Index])
        {
            return false;
        }
    }
    return true;
}

int RunBasePassRecordTests()
{
    int Failures = 0;
    FBasePassFixture Fixture;
    FBasePass Pass(Fixture.Resources);
    FBasePassTestCommandList Commands;
    RecordBasePass(Pass, Fixture.Scene, Commands);

    const bool bAttachments = Commands.BeginRenderPassCount == 1 &&
        Commands.EndRenderPassCount == 1 &&
        Commands.RenderPass.RenderArea.Width == 640 &&
        Commands.RenderPass.RenderArea.Height == 480 &&
        Commands.RenderPass.ColorAttachmentCount == 1 &&
        Commands.RenderPass.ColorAttachments[0].RenderTarget == &Fixture.ColorView &&
        Commands.RenderPass.ColorAttachments[0].LoadOp == EAttachmentLoadOp::Clear &&
        Commands.RenderPass.ColorAttachments[0].StoreOp == EAttachmentStoreOp::Store &&
        Commands.RenderPass.bHasDepthStencil &&
        Commands.RenderPass.DepthStencilAttachment.DepthStencilTarget == &Fixture.DepthView &&
        Commands.RenderPass.DepthStencilAttachment.LoadOp == EAttachmentLoadOp::Clear &&
        Commands.RenderPass.DepthStencilAttachment.StoreOp == EAttachmentStoreOp::Store &&
        Commands.RenderPass.DepthStencilAttachment.ClearDepth == 1.0f &&
        Commands.Viewport.Width == 640.0f && Commands.Viewport.Height == 480.0f &&
        Commands.Scissor.Width == 640 && Commands.Scissor.Height == 480;
    Failures += Expect(bAttachments,
        "BasePass clears and stores matching SceneColor and depth attachments");

    Failures += Expect(Commands.VertexBuffers.Size() == 2 &&
        Commands.VertexBuffers[0] == &Fixture.VertexBuffers[0] &&
        Commands.VertexBuffers[1] == &Fixture.VertexBuffers[1] &&
        Commands.IndexBuffers.Size() == 2 &&
        Commands.IndexBuffers[0].Buffer == &Fixture.IndexBuffers[0] &&
        Commands.IndexBuffers[0].Format == EPixelFormat::R16_UINT &&
        Commands.IndexBuffers[1].Buffer == &Fixture.IndexBuffers[1] &&
        Commands.IndexBuffers[1].Format == EPixelFormat::R32_UINT &&
        Commands.DrawIndexCounts.Size() == 2 &&
        Commands.DrawIndexCounts[0] == 3 && Commands.DrawIndexCounts[1] == 6,
        "BasePass preserves immutable snapshot order for indexed R16 and R32 draws");

    bool bPushConstantsValid = Commands.Pushes.Size() == 2;
    if (bPushConstantsValid)
    {
        FBasePassObjectConstants First;
        FBasePassObjectConstants Second;
        std::memcpy(&First, Commands.Pushes[0].Bytes, sizeof(First));
        std::memcpy(&Second, Commands.Pushes[1].Bytes, sizeof(Second));
        bPushConstantsValid = Commands.Pushes[0].Stage == BasePassStages &&
            Commands.Pushes[0].Size == 80 && Commands.Pushes[1].Stage == BasePassStages &&
            Commands.Pushes[1].Size == 80 &&
            MatrixEquals(First.LocalToClip, Fixture.Scene.Meshes[0].LocalToWorld) &&
            MatrixEquals(Second.LocalToClip, Fixture.Scene.Meshes[1].LocalToWorld) &&
            First.BaseColor == Math::Vector4f(1.0f, 0.25f, 0.5f, 1.0f) &&
            Second.BaseColor == Math::Vector4f(4.0f, 2.0f, 1.0f, 1.0f);
    }
    Failures += Expect(bPushConstantsValid,
        "BasePass uploads row-major LocalToClip and forces opaque alpha in exactly 80 bytes");

    const EBasePassCommandEvent ExpectedEvents[] = {
        EBasePassCommandEvent::BindPipeline,
        EBasePassCommandEvent::BeginRenderPass,
        EBasePassCommandEvent::SetViewport,
        EBasePassCommandEvent::SetScissor,
        EBasePassCommandEvent::BindPipeline,
        EBasePassCommandEvent::SetVertex,
        EBasePassCommandEvent::SetIndex,
        EBasePassCommandEvent::PushConstants,
        EBasePassCommandEvent::DrawIndexed,
        EBasePassCommandEvent::BindPipeline,
        EBasePassCommandEvent::SetVertex,
        EBasePassCommandEvent::SetIndex,
        EBasePassCommandEvent::PushConstants,
        EBasePassCommandEvent::DrawIndexed,
        EBasePassCommandEvent::EndRenderPass,
    };
    bool bEventOrder = Commands.Events.Size() == sizeof(ExpectedEvents) / sizeof(ExpectedEvents[0]);
    for (std::size_t Index = 0; bEventOrder && Index < Commands.Events.Size(); ++Index)
    {
        bEventOrder = Commands.Events[Index] == ExpectedEvents[Index];
    }
    Failures += Expect(bEventOrder,
        "BasePass records attachment, state, per-object binding, push, and indexed draw order");

    Fixture.Scene.Meshes.Clear();
    FBasePassTestCommandList EmptyCommands;
    RecordBasePass(Pass, Fixture.Scene, EmptyCommands);
    Failures += Expect(EmptyCommands.BeginRenderPassCount == 1 &&
        EmptyCommands.EndRenderPassCount == 1 && EmptyCommands.DrawIndexCounts.IsEmpty() &&
        EmptyCommands.Pipelines.IsEmpty(),
        "zero-visible BasePass remains valid and clears color plus depth without a pipeline");
    return Failures;
}

int RunBasePassPlanValidationTests()
{
    int Failures = 0;

    {
        FBasePassFixture Fixture;
        Failures += Expect(ProducesBasePassPlan(Fixture),
            "valid BasePass configuration produces exactly one render pass");
        Fixture.Scene.Meshes.Clear();
        Failures += Expect(ProducesBasePassPlan(Fixture),
            "zero-visible BasePass still produces a clear pass");
    }
    {
        FBasePassFixture Fixture;
        Fixture.Resources.SceneDepthView = nullptr;
        Failures += Expect(!ProducesBasePassPlan(Fixture),
            "invalid BasePass attachment produces an empty renderer plan");
    }
    {
        FBasePassFixture Fixture;
        Fixture.Scene.Cameras.Clear();
        Failures += Expect(!ProducesBasePassPlan(Fixture),
            "missing primary camera produces an empty renderer plan");
    }
    {
        FBasePassFixture Fixture;
        Fixture.Scene.Meshes[0].IndexFormat = EPixelFormat::R8_UINT;
        Failures += Expect(!ProducesBasePassPlan(Fixture),
            "invalid indexed proxy produces an empty renderer plan");
    }
    {
        FBasePassFixture Fixture;
        Fixture.Pipelines[0].Desc.BlendState.bEnable = true;
        Failures += Expect(!ProducesBasePassPlan(Fixture),
            "blended material pipeline is rejected by the opaque BasePass");
    }
    {
        FBasePassFixture Fixture;
        Fixture.Pipelines[0].Desc.DepthStencilState.DepthFunc = ECompareFunction::LessEqual;
        Failures += Expect(!ProducesBasePassPlan(Fixture),
            "BasePass rejects a pipeline outside the exact Less depth contract");
    }
    {
        FBasePassFixture Fixture;
        Fixture.Scene.Meshes[0].MaterialAssetId = FAssetId{};
        Failures += Expect(!ProducesBasePassPlan(Fixture),
            "BasePass requires persistent material identity on every proxy");
    }
    {
        FBasePassFixture Fixture;
        Fixture.VertexBuffers[0].Desc.UsageFlag = 0;
        Failures += Expect(!ProducesBasePassPlan(Fixture),
            "BasePass rejects a vertex buffer without VertexBuffer usage");
    }
    {
        FBasePassFixture Fixture;
        Fixture.IndexBuffers[0].Desc.UsageFlag = 0;
        Failures += Expect(!ProducesBasePassPlan(Fixture),
            "BasePass rejects an index buffer without IndexBuffer usage");
    }
    {
        FBasePassFixture Fixture;
        Fixture.VertexBuffers[0].Desc.Size = 71;
        Failures += Expect(!ProducesBasePassPlan(Fixture),
            "BasePass rejects vertex count times stride beyond buffer capacity");
    }
    {
        FBasePassFixture Fixture;
        Fixture.IndexBuffers[0].Desc.Size = 5;
        Failures += Expect(!ProducesBasePassPlan(Fixture),
            "BasePass rejects index bytes beyond buffer capacity");
    }
    {
        FBasePassFixture Fixture;
        Fixture.Pipelines[0].Desc.VertexBindings[0].Stride = 0;
        Failures += Expect(!ProducesBasePassPlan(Fixture),
            "BasePass requires a positive binding-zero vertex stride");
    }
    {
        FBasePassFixture Fixture;
        Fixture.Pipelines[0].Desc.VertexBindings.TryEmplaceBack();
        Fixture.Pipelines[0].Desc.VertexBindings.Back().Binding = 1;
        Fixture.Pipelines[0].Desc.VertexBindings.Back().Stride = 16;
        Failures += Expect(!ProducesBasePassPlan(Fixture),
            "single-slot BasePass rejects pipelines requiring unbound vertex slots");
    }
    return Failures;
}

int RunBasePassCompositePipelineTests()
{
    int Failures = 0;
    FBasePassFixture Fixture;
    FBasePassTestTexture BackBuffer;
    BackBuffer.Desc.Width = 640;
    BackBuffer.Desc.Height = 480;
    BackBuffer.Desc.Format = EPixelFormat::B8G8R8A8_SRGB;
    BackBuffer.Desc.bIsRenderTarget = true;
    FBasePassTestTextureView BackBufferView;
    BackBufferView.Desc.Texture = &BackBuffer;
    BackBufferView.Desc.Format = EPixelFormat::B8G8R8A8_SRGB;
    FBasePassTestPipeline CompositePipeline;
    CompositePipeline.Desc.RenderTargetCount = 1;
    CompositePipeline.Desc.RenderTargetFormats[0] = EPixelFormat::B8G8R8A8_SRGB;
    CompositePipeline.Desc.DepthStencilFormat = EPixelFormat::Unknown;
    FBasePassTestBindGroupLayout CompositeBindGroupLayout;
    CompositeBindGroupLayout.Desc.SetIndex = 0;
    CompositePipeline.Desc.BindGroupLayouts.PushBack(&CompositeBindGroupLayout);
    FBasePassTestBindGroup CompositeBindGroup;
    CompositeBindGroup.Desc.Layout = &CompositeBindGroupLayout;

    FBasePassCompositePipelineDesc Desc;
    Desc.BasePassResources = Fixture.Resources;
    Desc.BackBufferView = &BackBufferView;
    Desc.CompositePipeline = &CompositePipeline;
    Desc.CompositeBindGroup = &CompositeBindGroup;
    FRendererFrameContext Frame;
    FRenderView View;
    FRenderPipelinePlan Plan;
    FBasePassCompositePipeline Pipeline(Desc);
    Pipeline.BuildPasses(Frame, Fixture.Scene, View, Plan);
    Failures += Expect(Plan.Passes.Size() == 2 &&
        StringView(Plan.Passes[0]->GetPassName()) == StringView("BasePass") &&
        StringView(Plan.Passes[1]->GetPassName()) ==
            StringView("CompositeToBackBufferPass"),
        "combined renderer pipeline emits formal BasePass then SceneColor composite");

    Fixture.Scene.Meshes.Clear();
    Pipeline.BuildPasses(Frame, Fixture.Scene, View, Plan);
    Failures += Expect(Plan.Passes.Size() == 2,
        "zero-visible combined pipeline still clears BasePass and composites SceneColor");

    Desc.CompositeBindGroup = nullptr;
    FBasePassCompositePipeline MissingBindGroup(Desc);
    MissingBindGroup.BuildPasses(Frame, Fixture.Scene, View, Plan);
    Failures += Expect(Plan.Passes.IsEmpty(),
        "combined pipeline rejects a missing composite resource");

    Desc.CompositeBindGroup = &CompositeBindGroup;
    CompositePipeline.Desc.RenderTargetFormats[0] = EPixelFormat::R8G8B8A8_SRGB;
    FBasePassCompositePipeline MismatchedBackBuffer(Desc);
    MismatchedBackBuffer.BuildPasses(Frame, Fixture.Scene, View, Plan);
    Failures += Expect(Plan.Passes.IsEmpty(),
        "combined pipeline rejects composite/backbuffer format mismatch");

    CompositePipeline.Desc.RenderTargetFormats[0] = EPixelFormat::B8G8R8A8_SRGB;
    BackBuffer.Desc.bIsRenderTarget = false;
    FBasePassCompositePipeline NonRenderTargetBackBuffer(Desc);
    NonRenderTargetBackBuffer.BuildPasses(Frame, Fixture.Scene, View, Plan);
    Failures += Expect(Plan.Passes.IsEmpty(),
        "combined pipeline rejects a backbuffer without render-target usage");

    BackBuffer.Desc.bIsRenderTarget = true;
    BackBuffer.Desc.Format = EPixelFormat::Unknown;
    BackBufferView.Desc.Format = EPixelFormat::Unknown;
    FBasePassCompositePipeline InvalidBackBufferFormat(Desc);
    InvalidBackBufferFormat.BuildPasses(Frame, Fixture.Scene, View, Plan);
    Failures += Expect(Plan.Passes.IsEmpty(),
        "combined pipeline rejects an invalid effective backbuffer format");

    BackBuffer.Desc.Format = EPixelFormat::B8G8R8A8_SRGB;
    BackBufferView.Desc.Format = EPixelFormat::R8G8B8A8_SRGB;
    FBasePassCompositePipeline MismatchedBackBufferView(Desc);
    MismatchedBackBufferView.BuildPasses(Frame, Fixture.Scene, View, Plan);
    Failures += Expect(Plan.Passes.IsEmpty(),
        "combined pipeline validates the effective backbuffer view format");

    BackBufferView.Desc.Format = EPixelFormat::B8G8R8A8_SRGB;
    CompositeBindGroup.Desc.Layout = nullptr;
    FBasePassCompositePipeline NullBindGroupLayout(Desc);
    NullBindGroupLayout.BuildPasses(Frame, Fixture.Scene, View, Plan);
    Failures += Expect(Plan.Passes.IsEmpty(),
        "combined pipeline rejects a bind group without its declared layout");

    FBasePassTestBindGroupLayout OtherLayout;
    OtherLayout.Desc.SetIndex = 0;
    CompositeBindGroup.Desc.Layout = &OtherLayout;
    FBasePassCompositePipeline MismatchedBindGroupLayout(Desc);
    MismatchedBindGroupLayout.BuildPasses(Frame, Fixture.Scene, View, Plan);
    Failures += Expect(Plan.Passes.IsEmpty(),
        "combined pipeline rejects a bind group from a different layout");

    CompositeBindGroup.Desc.Layout = &CompositeBindGroupLayout;
    CompositeBindGroupLayout.Desc.SetIndex = 1;
    FBasePassCompositePipeline WrongSetIndex(Desc);
    WrongSetIndex.BuildPasses(Frame, Fixture.Scene, View, Plan);
    Failures += Expect(Plan.Passes.IsEmpty(),
        "combined pipeline requires the composite layout at set zero");
    return Failures;
}

bool HasAccess(
    const FRFGPassNode& Pass,
    const FRFGResourceHandle Resource,
    const ERFGAccessType Access,
    const ERALResourceState State) noexcept
{
    for (const FRFGPassResourceAccess& Candidate : Pass.ResourceAccesses)
    {
        if (Candidate.Resource == Resource && Candidate.Access.Access == Access &&
            Candidate.Access.State == State)
        {
            return true;
        }
    }
    return false;
}

bool HasTransition(
    const FRFGCompiledPass& Pass,
    const FRFGResourceHandle Resource,
    const ERALResourceState Before,
    const ERALResourceState After) noexcept
{
    for (const FRFGBarrierTransition& Barrier : Pass.PreBarriers)
    {
        if (Barrier.Resource == Resource && Barrier.BeforeState == Before &&
            Barrier.AfterState == After)
        {
            return true;
        }
    }
    return false;
}

int RunBasePassGraphContractTests()
{
    int Failures = 0;
    FBasePassFixture Fixture;
    FBasePass Pass(Fixture.Resources);
    FRFGBuilder Builder;
    FRenderGraphBuilderBridge Bridge(Builder);
    const FRFGPassHandle BaseHandle = Builder.AddPass(
        "BasePass", "BasePass", {}, ERFGPassFlags::None, ERFGQueueType::Graphics);
    FRenderPassSetupContext Setup;
    Setup.GraphBridge = &Bridge;
    Setup.PassHandle = BaseHandle;
    Setup.RenderScene = &Fixture.Scene;
    Pass.Setup(Setup);

    const FRFGResourceHandle SceneColor = Bridge.ImportTexture(
        "SceneColor", &Fixture.Color, ERALResourceState::Undefined);
    const FRFGResourceHandle SceneDepth = Bridge.ImportTexture(
        "Depth", &Fixture.Depth, ERALResourceState::Undefined);
    const FRFGResourceHandle Vertex0 = Bridge.ImportBuffer(
        "BasePassVertexBuffer", &Fixture.VertexBuffers[0], ERALResourceState::VertexBuffer);
    const FRFGResourceHandle Index0 = Bridge.ImportBuffer(
        "BasePassIndexBuffer", &Fixture.IndexBuffers[0], ERALResourceState::IndexBuffer);

    const FRFGPassHandle CompositeHandle = Builder.AddPass(
        "Composite", "Composite", {}, ERFGPassFlags::None, ERFGQueueType::Graphics);
    FBasePassTestTexture BackBuffer;
    BackBuffer.Desc.Width = 640;
    BackBuffer.Desc.Height = 480;
    BackBuffer.Desc.Format = EPixelFormat::B8G8R8A8_SRGB;
    BackBuffer.Desc.bIsRenderTarget = true;
    const FRFGResourceHandle BackBufferHandle = Bridge.ImportTexture(
        "BackBuffer", &BackBuffer, ERALResourceState::Undefined);
    FRFGAccessDesc ColorRead;
    ColorRead.Access = ERFGAccessType::Read;
    ColorRead.State = ERALResourceState::ShaderResource;
    ColorRead.ShaderStage = EShaderStage::Pixel;
    ColorRead.PipelineStage = ERFGPipelineStage::Graphics;
    Bridge.Read(CompositeHandle, SceneColor, ColorRead);
    FRFGAccessDesc BackBufferWrite = ColorRead;
    BackBufferWrite.Access = ERFGAccessType::Write;
    BackBufferWrite.State = ERALResourceState::RenderTarget;
    Bridge.Write(CompositeHandle, BackBufferHandle, BackBufferWrite);
    Bridge.MarkOutput(BackBufferHandle);

    const FRFGRecordedGraph& Graph = Builder.GetRecordedGraph();
    const FRFGPassNode& BaseNode = Graph.GetPassNode(BaseHandle);
    Failures += Expect(
        Graph.GetResourceNode(SceneColor).InitialState == ERALResourceState::Undefined &&
        Graph.GetResourceNode(SceneDepth).InitialState == ERALResourceState::Undefined &&
        Graph.GetResourceNode(Vertex0).InitialState == ERALResourceState::VertexBuffer &&
        Graph.GetResourceNode(Index0).InitialState == ERALResourceState::IndexBuffer &&
        HasAccess(BaseNode, SceneColor, ERFGAccessType::Write, ERALResourceState::RenderTarget) &&
        HasAccess(BaseNode, SceneDepth, ERFGAccessType::Write, ERALResourceState::DepthStencilWrite) &&
        HasAccess(BaseNode, Vertex0, ERFGAccessType::Read, ERALResourceState::VertexBuffer) &&
        HasAccess(BaseNode, Index0, ERFGAccessType::Read, ERALResourceState::IndexBuffer) &&
        Graph.IsOutputResource(SceneDepth),
        "BasePass RFG setup imports exact attachment, vertex, and index states");

    FRFGCompiler Compiler;
    const FRFGCompileResult Compiled = Compiler.Compile(Graph, Builder.BuildSignature());
    bool bDependencyAndBarriers = Compiled.Plan && Compiled.Plan->GetPasses().Size() == 2;
    const FRFGCompiledPass* CompiledBase = nullptr;
    const FRFGCompiledPass* CompiledComposite = nullptr;
    if (bDependencyAndBarriers)
    {
        for (const FRFGCompiledPass& CompiledPass : Compiled.Plan->GetPasses())
        {
            if (CompiledPass.Handle == BaseHandle) CompiledBase = &CompiledPass;
            if (CompiledPass.Handle == CompositeHandle) CompiledComposite = &CompiledPass;
        }
        bDependencyAndBarriers = CompiledBase != nullptr && CompiledComposite != nullptr;
    }
    bool bIncomingBase = false;
    if (bDependencyAndBarriers)
    {
        for (const FRFGDependencyEdge& Edge : CompiledComposite->IncomingEdges)
        {
            bIncomingBase |= Edge.SourcePass == BaseHandle;
        }
        bDependencyAndBarriers = bIncomingBase &&
            HasTransition(*CompiledBase, SceneColor, ERALResourceState::Undefined, ERALResourceState::RenderTarget) &&
            HasTransition(*CompiledBase, SceneDepth, ERALResourceState::Undefined, ERALResourceState::DepthStencilWrite) &&
            HasTransition(*CompiledComposite, SceneColor, ERALResourceState::RenderTarget, ERALResourceState::ShaderResource);
    }
    Failures += Expect(bDependencyAndBarriers,
        "SceneColor alone carries BasePass to Composite dependency while depth remains a stored output");
    return Failures;
}

int RunRALIndexedAndPushConstantContractTests()
{
    int Failures = 0;
    FRALBufferDesc DefaultBuffer;
    FRALPipelineDesc_Graphics DefaultPipeline;
    bool bDefaultRenderTargetsUnknown = true;
    for (const EPixelFormat Format : DefaultPipeline.RenderTargetFormats)
    {
        bDefaultRenderTargetsUnknown &= Format == EPixelFormat::Unknown;
    }
    Failures += Expect(DefaultBuffer.Usage == 0 && DefaultBuffer.UsageFlag == 0 &&
        DefaultPipeline.RenderTargetCount == 0 && bDefaultRenderTargetsUnknown,
        "default RAL descriptors expose deterministic zero usage and attachment formats");

    FRALPushConstantRangeDesc Disabled;
    FRALPushConstantRangeDesc Valid;
    Valid.StageFlags = BasePassStages;
    Valid.Size = 80;
    FRALPushConstantRangeDesc Misaligned = Valid;
    Misaligned.Size = 78;
    FRALPushConstantRangeDesc Oversized = Valid;
    Oversized.Size = 132;
    FRALPushConstantRangeDesc MissingStage = Valid;
    MissingStage.StageFlags = EShaderStage::None;
    Failures += Expect(Disabled.IsDisabled() && !Disabled.IsValid() && Valid.IsValid() &&
        Valid.AcceptsWrite(BasePassStages, 80) &&
        !Valid.AcceptsWrite(EShaderStage::Vertex, 80) &&
        !Valid.AcceptsWrite(BasePassStages, 64) &&
        !Misaligned.IsValid() && !Oversized.IsValid() && !MissingStage.IsValid(),
        "RAL push constant contract accepts only staged aligned ranges up to 128 bytes");
    Failures += Expect(IsValidIndexFormat(EPixelFormat::R16_UINT) &&
        IsValidIndexFormat(EPixelFormat::R32_UINT) &&
        !IsValidIndexFormat(EPixelFormat::R8_UINT) &&
        !IsValidIndexFormat(EPixelFormat::Unknown),
        "RAL index contract accepts only R16_UINT and R32_UINT");

    FRALPipelineDesc_Graphics AttachmentPipeline;
    AttachmentPipeline.RenderTargetCount = 1;
    AttachmentPipeline.RenderTargetFormats[0] = EPixelFormat::B8G8R8A8_SRGB;
    AttachmentPipeline.DepthStencilFormat = EPixelFormat::D32_FLOAT;
    FRALRenderingAttachmentSignature Signature;
    Signature.RenderTargetCount = 1;
    Signature.RenderTargetFormats[0] = EPixelFormat::B8G8R8A8_SRGB;
    Signature.DepthStencilFormat = EPixelFormat::D32_FLOAT;
    const bool bCompatible = IsRenderingSignatureCompatible(Signature, AttachmentPipeline);
    AttachmentPipeline.RenderTargetFormats[0] = EPixelFormat::R8G8B8A8_SRGB;
    const bool bRejectsColor = !IsRenderingSignatureCompatible(Signature, AttachmentPipeline);
    AttachmentPipeline.RenderTargetFormats[0] = EPixelFormat::B8G8R8A8_SRGB;
    AttachmentPipeline.DepthStencilFormat = EPixelFormat::Unknown;
    const bool bRejectsDepth = !IsRenderingSignatureCompatible(Signature, AttachmentPipeline);
    AttachmentPipeline.DepthStencilFormat = EPixelFormat::D32_FLOAT;
    AttachmentPipeline.RenderTargetCount = 0;
    const bool bRejectsCount = !IsRenderingSignatureCompatible(Signature, AttachmentPipeline);
    Signature.Reset();
    Failures += Expect(bCompatible && bRejectsColor && bRejectsDepth && bRejectsCount &&
        Signature.RenderTargetCount == 0 &&
        Signature.RenderTargetFormats[0] == EPixelFormat::Unknown &&
        Signature.DepthStencilFormat == EPixelFormat::Unknown,
        "active rendering signature gates every pipeline bind and resets between command scopes");
    return Failures;
}

} // namespace

int RunBasePassTests()
{
    static_assert(sizeof(FBasePassObjectConstants) == 80);
    int Failures = 0;
    Failures += RunRALIndexedAndPushConstantContractTests();
    Failures += RunBasePassPlanValidationTests();
    Failures += RunBasePassCompositePipelineTests();
    Failures += RunBasePassRecordTests();
    Failures += RunBasePassGraphContractTests();
    return Failures;
}

} // namespace LE
