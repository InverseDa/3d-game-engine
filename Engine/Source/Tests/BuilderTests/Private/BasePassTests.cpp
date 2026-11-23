#include "Renderer/BasePass.h"
#include "Renderer/BasePassCompositePipeline.h"

#include "Compile/RFGCompiledPlan.h"
#include "Compile/RFGCompiler.h"
#include "Execute/RFGPassContext.h"
#include "RHI/RHIBindGroup.h"
#include "RHI/RHIBuffer.h"
#include "RHI/RHICommandList.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHIPipeline.h"
#include "RHI/RHISampler.h"
#include "RHI/RHITexture.h"
#include "Record/RFGBuilder.h"
#include "Record/RFGRecordedGraph.h"
#include "Renderer/RenderAssetProvider.h"
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

constexpr EShaderStage BasePassStages = static_cast<EShaderStage>(static_cast<uint8>(EShaderStage::Vertex) | static_cast<uint8>(EShaderStage::Pixel));

class FBasePassTestBuffer final : public FRHIBuffer
{
public:
    void* Map(const uint64 Offset, const uint64 Size) override { return bMapFails || Offset != 0 || Size != sizeof(Bytes) ? nullptr : Bytes; }
    void Unmap() override {}
    const FRHIBufferDesc& GetDesc() const override { return Desc; }

    FRHIBufferDesc Desc;
    uint8 Bytes[sizeof(FBasePassLightConstants)]{};
    bool bMapFails = false;
};

class FBasePassTestTexture final : public FRHITexture
{
public:
    const FRHITextureDesc& GetDesc() const override { return Desc; }

    FRHITextureDesc Desc;
};

class FBasePassTestTextureView final : public FRHITextureView
{
public:
    FRHITexture* GetTexture() const override { return Desc.Texture; }
    const FRHITextureViewDesc& GetDesc() const override { return Desc; }

    FRHITextureViewDesc Desc;
};

class FBasePassTestPipeline final : public FRHIPipeline_Graphics
{
public:
    const FRHIPipelineDesc_Graphics& GetDesc() const override { return Desc; }

    FRHIPipelineDesc_Graphics Desc;
};

class FBasePassTestBindGroupLayout final : public FRHIBindGroupLayout
{
public:
    const FRHIBindGroupLayoutDesc& GetDesc() const override { return Desc; }

    FRHIBindGroupLayoutDesc Desc;
};

class FBasePassTestBindGroup final : public FRHIBindGroup
{
public:
    const FRHIBindGroupDesc& GetDesc() const override { return Desc; }

    FRHIBindGroupDesc Desc;
};

class FBasePassTestSampler final : public FRHISampler
{
public:
    const FRHISamplerDesc& GetDesc() const override { return Desc; }

private:
    FRHISamplerDesc Desc;
};

class FBasePassTestDevice final : public FRHIDevice
{
public:
    FRHIQueue* GetGraphicsQueue() const override { return nullptr; }
    FRHIBuffer* CreateBuffer(const FRHIBufferDesc& Desc) override
    {
        auto* Buffer = new FBasePassTestBuffer;
        Buffer->Desc = Desc;
        return Buffer;
    }
    FRHIBindGroup* CreateBindGroup(const FRHIBindGroupDesc& Desc) override
    {
        if (++GroupAttempts == FailGroupAttempt)
        {
            return nullptr;
        }
        auto* Group = new FBasePassTestBindGroup;
        Group->Desc = Desc;
        return Group;
    }
    FRHIBindGroupLayout* CreateBindGroupLayout(const FRHIBindGroupLayoutDesc& Desc) override
    {
        auto* Layout = new FBasePassTestBindGroupLayout;
        Layout->Desc = Desc;
        return Layout;
    }
    FRHITexture* CreateTexture(const FRHITextureDesc&) override { return nullptr; }
    FRHITextureView* CreateTextureView(const FRHITextureViewDesc&) override { return nullptr; }
    FRHIShader* CreateShaderFromFile(EShaderStage, const void*, uint64, const String&) override { return nullptr; }
    FRHIPipeline_Graphics* CreateGraphicsPipeline(const FRHIPipelineDesc_Graphics&) override { return nullptr; }
    FRHICommandAllocator* CreateCommandAllocator(EQueueType) override { return nullptr; }
    FRHICommandList* CreateCommandList(FRHICommandAllocator*) override { return nullptr; }
    FRHISemaphore* CreateBinarySemaphore() override { return nullptr; }
    FRHIFence* CreateFence(bool) override { return nullptr; }
    FRHISwapchain* CreateSwapchain(const FRHISwapchainDesc&) override { return nullptr; }
    FRHISampler* CreateSampler(const FRHISamplerDesc&) override { return nullptr; }

    uint32 GroupAttempts = 0;
    uint32 FailGroupAttempt = 2;
};

int RunFrameLightingTests()
{
    FBasePassTestDevice Device;
    FBasePassFrameLighting Lighting;
    FBasePassLightBinding Slot0;
    FBasePassLightBinding Slot1;
    int Failures = Expect(!Lighting.Initialize(Device) && Lighting.GetLayout() == nullptr && !Lighting.WriteSlot(0, {}, Slot0), "failed frame lighting initialization can be retried");
    if (!Lighting.Initialize(Device))
    {
        return Failures + Expect(false, "initialize light buffers for every slot");
    }

    FRenderLightProxy Light;
    Light.PrimitiveId = {1, 1};
    Light.DirectionToLight = Math::Vector3f(0.0f, 0.0f, 1.0f);
    Light.Radiance = Math::Vector3f(0.5f, 0.25f, 0.125f);
    if (!Lighting.WriteSlot(0, {&Light, 1}, Slot0))
    {
        return Failures + Expect(false, "write the first slot");
    }
    Light.DirectionToLight[2] = -1.0f;
    if (!Lighting.WriteSlot(1, {&Light, 1}, Slot1))
    {
        return Failures + Expect(false, "write the second slot");
    }
    const auto ReadSlot = [](FRHIBuffer* Buffer) {
        FBasePassLightConstants Constants;
        std::memcpy(&Constants, static_cast<FBasePassTestBuffer*>(Buffer)->Bytes, sizeof(Constants));
        return Constants;
    };
    Failures += Expect(Slot0.Buffer != Slot1.Buffer && Slot0.BindGroup != Slot1.BindGroup && ReadSlot(Slot0.Buffer).DirectionAndEnabled == Math::Vector4f(0.0f, 0.0f, 1.0f, 1.0f) && ReadSlot(Slot1.Buffer).DirectionAndEnabled == Math::Vector4f(0.0f, 0.0f, -1.0f, 1.0f) && ReadSlot(Slot0.Buffer).Radiance == Math::Vector4f(0.5f, 0.25f, 0.125f, 0.0f), "alternating directional lights reach distinct frame slots");
    Failures += Expect(Lighting.WriteSlot(0, {}, Slot0) && ReadSlot(Slot0.Buffer).DirectionAndEnabled == Math::Vector4f() && ReadSlot(Slot0.Buffer).Radiance == Math::Vector4f() && ReadSlot(Slot1.Buffer).DirectionAndEnabled[3] == 1.0f, "disabling one light clears all its slot bytes and preserves the other slot");
    static_cast<FBasePassTestBuffer*>(Slot1.Buffer)->bMapFails = true;
    FBasePassLightBinding Candidate = Slot0;
    Failures += Expect(!Lighting.WriteSlot(1, {}, Candidate) && Candidate.Buffer == Slot0.Buffer && ReadSlot(Slot1.Buffer).DirectionAndEnabled[3] == 1.0f && !Lighting.WriteSlot(2, {}, Candidate), "mapping or slot errors preserve the previous binding and light bytes");
    return Failures;
}

enum class EBasePassCommandEvent : uint8
{
    BindPipeline,
    BeginRenderPass,
    SetViewport,
    SetScissor,
    BindMaterial,
    BindLight,
    SetVertex,
    SetIndex,
    PushConstants,
    DrawIndexed,
    EndRenderPass,
};

struct FRecordedIndexBinding
{
    FRHIBuffer* Buffer = nullptr;
    EPixelFormat Format = EPixelFormat::Unknown;
};

struct FRecordedPushConstants
{
    EShaderStage Stage = EShaderStage::None;
    uint32 Size = 0;
    uint8 Bytes[sizeof(FBasePassObjectConstants)]{};
};

class FBasePassTestCommandList final : public FRHICommandList
{
public:
    void Begin() override {}
    void End() override {}
    void ResourceBarriers(const FRHIBarrierBatch&) override {}

    void BeginRenderPass(const FRHIRenderPassDesc& Desc) override
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
    void SetGraphicsPipeline(FRHIPipeline_Graphics* Pipeline) override
    {
        Events.PushBack(EBasePassCommandEvent::BindPipeline);
        Pipelines.PushBack(Pipeline);
    }
    void SetViewport(const FRHIViewport& InViewport) override
    {
        Events.PushBack(EBasePassCommandEvent::SetViewport);
        Viewport = InViewport;
    }
    void SetScissorRect(const FRHIScissorRect& InScissor) override
    {
        Events.PushBack(EBasePassCommandEvent::SetScissor);
        Scissor = InScissor;
    }
    void SetVertexBuffer(uint32, FRHIBuffer* Buffer, uint64) override
    {
        Events.PushBack(EBasePassCommandEvent::SetVertex);
        VertexBuffers.PushBack(Buffer);
    }
    void SetIndexBuffer(FRHIBuffer* Buffer, uint64, EPixelFormat Format) override
    {
        Events.PushBack(EBasePassCommandEvent::SetIndex);
        IndexBuffers.PushBack(FRecordedIndexBinding{Buffer, Format});
    }
    void SetBindGroup(uint32 SetIndex, FRHIBindGroup* BindGroup) override
    {
        if (SetIndex == FBasePassFrameLighting::SetIndex)
        {
            Events.PushBack(EBasePassCommandEvent::BindLight);
            Lights.PushBack(BindGroup);
            return;
        }
        Events.PushBack(EBasePassCommandEvent::BindMaterial);
        MaterialSets.PushBack(SetIndex);
        Materials.PushBack(BindGroup);
    }
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
    Array<FRHIPipeline_Graphics*> Pipelines;
    Array<FRHIBindGroup*> Materials;
    Array<uint32> MaterialSets;
    Array<FRHIBindGroup*> Lights;
    Array<FRHIBuffer*> VertexBuffers;
    Array<FRecordedIndexBinding> IndexBuffers;
    Array<FRecordedPushConstants> Pushes;
    Array<uint32> DrawIndexCounts;
    FRHIRenderPassDesc RenderPass;
    FRHIViewport Viewport;
    FRHIScissorRect Scissor;
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

        LightLayout.Desc.SetIndex = FBasePassFrameLighting::SetIndex;
        LightLayout.Desc.Bindings.PushBack({FBasePassFrameLighting::BindingIndex, 1, ERHIBindGroupItemType::UniformBuffer, EShaderStage::Pixel});
        LightBuffer.Desc.Size = sizeof(FBasePassLightConstants);
        LightBuffer.Desc.UsageFlag = static_cast<uint32>(EBufferUsageFlags::UniformBuffer);
        LightGroup.Desc.Layout = &LightLayout;
        auto& LightItem = LightGroup.Desc.Items.EmplaceBack();
        LightItem.Buffer = &LightBuffer;
        LightItem.Range = sizeof(FBasePassLightConstants);
        Resources.Light = {&LightBuffer, &LightGroup};

        ConfigurePipeline(Pipeline);
        Resources.Pipeline = &Pipeline;
        Layout.Desc.SetIndex = 0;
        Pipeline.Desc.BindGroupLayouts.PushBack(&Layout);
        Pipeline.Desc.BindGroupLayouts.PushBack(&LightLayout);
        for (std::size_t Index = 0; Index < 3; ++Index)
        {
            Samples[Index].Desc.Format = EPixelFormat::R8G8B8A8_SRGB;
            Samples[Index].Desc.bIsShaderResource = true;
            SampleViews[Index].Desc.Texture = &Samples[Index];
            SampleViews[Index].Desc.Format = EPixelFormat::R8G8B8A8_SRGB;
            Groups[Index].Desc.Layout = &Layout;
            FRHIBindGroupItem Image;
            Image.TextureView = &SampleViews[Index];
            Groups[Index].Desc.Items.PushBack(Image);
            FRHIBindGroupItem SamplerItem;
            SamplerItem.Binding = 1;
            SamplerItem.Sampler = &Sampler;
            Groups[Index].Desc.Items.PushBack(SamplerItem);
            Materials[Index].PassMask = ERenderMeshPassMask::SceneColor;
            Materials[Index].BaseColorTexture = &Samples[Index];
            Materials[Index].MaterialBindGroup = &Groups[Index];
        }
        Groups[3].Desc = Groups[0].Desc;
        Materials[3] = Materials[0];
        Materials[3].MaterialBindGroup = &Groups[3];

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
        Materials[0].BaseColor = Math::Vector4f(1.0f, 0.25f, 0.5f, 19.0f);
        Scene.Meshes[1].LocalToWorld(1, 3) = -3.0f;
        Materials[1].BaseColor = Math::Vector4f(4.0f, 2.0f, 1.0f, -7.0f);
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
        Pipeline.Desc.VertexBindings.Back().Stride = sizeof(FBasePassVertex);
        Pipeline.Desc.VertexBindings.Back().bPerInstance = false;
        Pipeline.Desc.PushConstants.StageFlags = BasePassStages;
        Pipeline.Desc.PushConstants.Size = sizeof(FBasePassObjectConstants);
    }

    void ConfigureMesh(FRenderMeshProxy& Primitive, const uint64 PrimitiveValue, const uint32 ResourceIndex, const EPixelFormat IndexFormat, const uint32 IndexCount)
    {
        FMeshAsset& Mesh = Geometry[ResourceIndex];
        Mesh.VertexBuffer = &VertexBuffers[ResourceIndex];
        Mesh.VertexCount = 3;
        Mesh.IndexBuffer = &IndexBuffers[ResourceIndex];
        Mesh.IndexCount = IndexCount;
        Mesh.IndexFormat = IndexFormat;
        Primitive.PrimitiveId = {7, PrimitiveValue};
        Primitive.Mesh = &Mesh;
        Primitive.Material = &Materials[ResourceIndex];
        Primitive.MaterialAssetId = MaterialIds[ResourceIndex];
    }

    FBasePassTestTexture Color;
    FBasePassTestTexture Depth;
    FBasePassTestTextureView ColorView;
    FBasePassTestTextureView DepthView;
    FBasePassResources Resources;
    FBasePassTestBindGroupLayout Layout;
    FBasePassTestBindGroupLayout LightLayout;
    FBasePassTestBuffer LightBuffer;
    FBasePassTestBindGroup LightGroup;
    FBasePassTestSampler Sampler;
    FBasePassTestTexture Samples[3];
    FBasePassTestTextureView SampleViews[3];
    FBasePassTestBindGroup Groups[4];
    FBasePassTestPipeline Pipeline;
    FMeshAsset Geometry[2];
    FMaterialAsset Materials[4];
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

void RecordBasePass(FBasePass& Pass, const FRenderScene& Scene, FBasePassTestCommandList& CommandList)
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
    Fixture.Geometry[1].bHasNormals = true;
    Fixture.Scene.Meshes[1].NormalToWorld(0, 0) = 0.5f;
    FBasePass Pass(Fixture.Resources);
    FBasePassTestCommandList Commands;
    RecordBasePass(Pass, Fixture.Scene, Commands);

    const bool bAttachments = Commands.BeginRenderPassCount == 1 && Commands.EndRenderPassCount == 1 && Commands.RenderPass.RenderArea.Width == 640 && Commands.RenderPass.RenderArea.Height == 480 && Commands.RenderPass.ColorAttachmentCount == 1 && Commands.RenderPass.ColorAttachments[0].RenderTarget == &Fixture.ColorView && Commands.RenderPass.ColorAttachments[0].LoadOp == EAttachmentLoadOp::Clear && Commands.RenderPass.ColorAttachments[0].StoreOp == EAttachmentStoreOp::Store && Commands.RenderPass.bHasDepthStencil && Commands.RenderPass.DepthStencilAttachment.DepthStencilTarget == &Fixture.DepthView && Commands.RenderPass.DepthStencilAttachment.LoadOp == EAttachmentLoadOp::Clear && Commands.RenderPass.DepthStencilAttachment.StoreOp == EAttachmentStoreOp::Store && Commands.RenderPass.DepthStencilAttachment.ClearDepth == 1.0f && Commands.Viewport.Width == 640.0f && Commands.Viewport.Height == 480.0f && Commands.Scissor.Width == 640 && Commands.Scissor.Height == 480;
    Failures += Expect(bAttachments, "BasePass clears and stores matching SceneColor and depth attachments");

    Failures += Expect(Commands.VertexBuffers.Size() == 2 && Commands.VertexBuffers[0] == &Fixture.VertexBuffers[0] && Commands.VertexBuffers[1] == &Fixture.VertexBuffers[1] && Commands.IndexBuffers.Size() == 2 && Commands.IndexBuffers[0].Buffer == &Fixture.IndexBuffers[0] && Commands.IndexBuffers[0].Format == EPixelFormat::R16_UINT && Commands.IndexBuffers[1].Buffer == &Fixture.IndexBuffers[1] && Commands.IndexBuffers[1].Format == EPixelFormat::R32_UINT && Commands.DrawIndexCounts.Size() == 2 && Commands.DrawIndexCounts[0] == 3 && Commands.DrawIndexCounts[1] == 6, "BasePass preserves immutable snapshot order for indexed R16 and R32 draws");

    bool bPushConstantsValid = Commands.Pushes.Size() == 2;
    if (bPushConstantsValid)
    {
        FBasePassObjectConstants First;
        FBasePassObjectConstants Second;
        std::memcpy(&First, Commands.Pushes[0].Bytes, sizeof(First));
        std::memcpy(&Second, Commands.Pushes[1].Bytes, sizeof(Second));
        bPushConstantsValid = Commands.Pushes[0].Stage == BasePassStages && Commands.Pushes[0].Size == sizeof(First) && Commands.Pushes[1].Stage == BasePassStages && Commands.Pushes[1].Size == sizeof(Second) && MatrixEquals(First.LocalToClip, Fixture.Scene.Meshes[0].LocalToWorld) && MatrixEquals(Second.LocalToClip, Fixture.Scene.Meshes[1].LocalToWorld) && First.BaseColor == Math::Vector4f(1.0f, 0.25f, 0.5f, 0.0f) && Second.BaseColor == Math::Vector4f(4.0f, 2.0f, 1.0f, 1.0f) && First.NormalToWorldRows[0] == Math::Vector4f() && Second.NormalToWorldRows[0] == Math::Vector4f(0.5f, 0.0f, 0.0f, 0.0f);
    }
    Failures += Expect(bPushConstantsValid, "BasePass records the 128-byte transform, material, and authored normal rows");

    const EBasePassCommandEvent ExpectedEvents[] = {
        EBasePassCommandEvent::BindPipeline,
        EBasePassCommandEvent::BeginRenderPass,
        EBasePassCommandEvent::SetViewport,
        EBasePassCommandEvent::SetScissor,
        EBasePassCommandEvent::BindMaterial,
        EBasePassCommandEvent::BindLight,
        EBasePassCommandEvent::SetVertex,
        EBasePassCommandEvent::SetIndex,
        EBasePassCommandEvent::PushConstants,
        EBasePassCommandEvent::DrawIndexed,
        EBasePassCommandEvent::BindMaterial,
        EBasePassCommandEvent::BindLight,
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
    Failures += Expect(bEventOrder && Commands.Pipelines.Size() == 1 && Commands.Pipelines[0] == &Fixture.Pipeline,
                       "BasePass uses its own pipeline for all draws and preserves command order");

    Fixture.Scene.Meshes.PushBack(Fixture.Scene.Meshes[0]);
    Fixture.Scene.Meshes.Back().PrimitiveId = {7, 4};
    Fixture.Scene.Meshes.Back().Material = &Fixture.Materials[2];
    Fixture.Scene.Meshes.PushBack(Fixture.Scene.Meshes[0]);
    Fixture.Scene.Meshes.Back().PrimitiveId = {7, 5};
    Fixture.Scene.Meshes.Back().Material = &Fixture.Materials[3];
    FBasePassTestCommandList MixedMaterialCommands;
    RecordBasePass(Pass, Fixture.Scene, MixedMaterialCommands);
    bool bMixBound = MixedMaterialCommands.Materials.Size() == 4 && MixedMaterialCommands.Lights.Size() == 4 && MixedMaterialCommands.DrawIndexCounts.Size() == 4;
    for (std::size_t Index = 0; bMixBound && Index < 4; ++Index)
    {
        bMixBound = MixedMaterialCommands.Materials[Index] == &Fixture.Groups[Index] && MixedMaterialCommands.MaterialSets[Index] == 0 && MixedMaterialCommands.Lights[Index] == &Fixture.LightGroup;
    }
    Failures += Expect(bMixBound, "BasePass rebinds each material across texture A, white, texture B and shared texture A");

    Fixture.Scene.Meshes.Clear();
    FBasePassTestCommandList EmptyCommands;
    RecordBasePass(Pass, Fixture.Scene, EmptyCommands);
    Failures += Expect(EmptyCommands.BeginRenderPassCount == 1 && EmptyCommands.EndRenderPassCount == 1 &&
                       EmptyCommands.DrawIndexCounts.IsEmpty() && EmptyCommands.Pipelines.Size() == 1 &&
                       EmptyCommands.Pipelines[0] == &Fixture.Pipeline,
                       "zero-visible BasePass binds its own attachment contract and still clears without draws");
    return Failures;
}

int RunBasePassPlanValidationTests()
{
    int Failures = 0;

    {
        FBasePassFixture Fixture;
        Failures += Expect(ProducesBasePassPlan(Fixture), "valid BasePass configuration produces exactly one render pass");
        Fixture.Scene.Meshes.Clear();
        Failures += Expect(ProducesBasePassPlan(Fixture), "zero-visible BasePass still produces a clear pass");
    }
    {
        FBasePassFixture Fixture;
        Fixture.Resources.SceneDepthView = nullptr;
        Failures += Expect(!ProducesBasePassPlan(Fixture), "invalid BasePass attachment produces an empty renderer plan");
    }
    {
        FBasePassFixture Fixture;
        Fixture.Scene.Cameras.Clear();
        Failures += Expect(!ProducesBasePassPlan(Fixture), "missing primary camera produces an empty renderer plan");
    }
    {
        FBasePassFixture Fixture;
        Fixture.Geometry[0].IndexFormat = EPixelFormat::R8_UINT;
        Failures += Expect(!ProducesBasePassPlan(Fixture), "invalid indexed proxy produces an empty renderer plan");
    }
    {
        FBasePassFixture Fixture;
        Fixture.Pipeline.Desc.BlendState.bEnable = true;
        Failures += Expect(!ProducesBasePassPlan(Fixture), "blending is rejected by the opaque BasePass contract");
    }
    {
        FBasePassFixture Fixture;
        Fixture.Pipeline.Desc.DepthStencilState.DepthFunc = ECompareFunction::LessEqual;
        Failures += Expect(!ProducesBasePassPlan(Fixture), "BasePass rejects a pipeline outside the exact Less depth contract");
    }
    {
        FBasePassFixture Fixture;
        Fixture.Scene.Meshes[0].MaterialAssetId = FAssetId{};
        Failures += Expect(!ProducesBasePassPlan(Fixture), "BasePass requires persistent material identity on every proxy");
    }
    {
        FBasePassFixture Fixture;
        Fixture.VertexBuffers[0].Desc.UsageFlag = 0;
        Failures += Expect(!ProducesBasePassPlan(Fixture), "BasePass rejects a vertex buffer without VertexBuffer usage");
    }
    {
        FBasePassFixture Fixture;
        Fixture.IndexBuffers[0].Desc.UsageFlag = 0;
        Failures += Expect(!ProducesBasePassPlan(Fixture), "BasePass rejects an index buffer without IndexBuffer usage");
    }
    {
        FBasePassFixture Fixture;
        Fixture.VertexBuffers[0].Desc.Size = 71;
        Failures += Expect(!ProducesBasePassPlan(Fixture), "BasePass rejects vertex count times stride beyond buffer capacity");
    }
    {
        FBasePassFixture Fixture;
        Fixture.IndexBuffers[0].Desc.Size = 5;
        Failures += Expect(!ProducesBasePassPlan(Fixture), "BasePass rejects index bytes beyond buffer capacity");
    }
    {
        FBasePassFixture Fixture;
        Fixture.Pipeline.Desc.VertexBindings[0].Stride = 0;
        Failures += Expect(!ProducesBasePassPlan(Fixture), "BasePass requires a positive binding-zero vertex stride");
    }
    {
        FBasePassFixture Fixture;
        Fixture.Pipeline.Desc.VertexBindings.TryEmplaceBack();
        Fixture.Pipeline.Desc.VertexBindings.Back().Binding = 1;
        Fixture.Pipeline.Desc.VertexBindings.Back().Stride = 16;
        Failures += Expect(!ProducesBasePassPlan(Fixture), "single-slot BasePass rejects pipelines requiring unbound vertex slots");
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
    Failures += Expect(Plan.Passes.Size() == 2 && StringView(Plan.Passes[0]->GetPassName()) == StringView("BasePass") && StringView(Plan.Passes[1]->GetPassName()) == StringView("CompositeToBackBufferPass"), "combined renderer pipeline emits formal BasePass then SceneColor composite");

    Fixture.Scene.Meshes.Clear();
    Pipeline.BuildPasses(Frame, Fixture.Scene, View, Plan);
    Failures += Expect(Plan.Passes.Size() == 2, "zero-visible combined pipeline still clears BasePass and composites SceneColor");

    Desc.CompositeBindGroup = nullptr;
    FBasePassCompositePipeline MissingBindGroup(Desc);
    MissingBindGroup.BuildPasses(Frame, Fixture.Scene, View, Plan);
    Failures += Expect(Plan.Passes.IsEmpty(), "combined pipeline rejects a missing composite resource");

    Desc.CompositeBindGroup = &CompositeBindGroup;
    CompositePipeline.Desc.RenderTargetFormats[0] = EPixelFormat::R8G8B8A8_SRGB;
    FBasePassCompositePipeline MismatchedBackBuffer(Desc);
    MismatchedBackBuffer.BuildPasses(Frame, Fixture.Scene, View, Plan);
    Failures += Expect(Plan.Passes.IsEmpty(), "combined pipeline rejects composite/backbuffer format mismatch");

    CompositePipeline.Desc.RenderTargetFormats[0] = EPixelFormat::B8G8R8A8_SRGB;
    BackBuffer.Desc.bIsRenderTarget = false;
    FBasePassCompositePipeline NonRenderTargetBackBuffer(Desc);
    NonRenderTargetBackBuffer.BuildPasses(Frame, Fixture.Scene, View, Plan);
    Failures += Expect(Plan.Passes.IsEmpty(), "combined pipeline rejects a backbuffer without render-target usage");

    BackBuffer.Desc.bIsRenderTarget = true;
    BackBuffer.Desc.Format = EPixelFormat::Unknown;
    BackBufferView.Desc.Format = EPixelFormat::Unknown;
    FBasePassCompositePipeline InvalidBackBufferFormat(Desc);
    InvalidBackBufferFormat.BuildPasses(Frame, Fixture.Scene, View, Plan);
    Failures += Expect(Plan.Passes.IsEmpty(), "combined pipeline rejects an invalid effective backbuffer format");

    BackBuffer.Desc.Format = EPixelFormat::B8G8R8A8_SRGB;
    BackBufferView.Desc.Format = EPixelFormat::R8G8B8A8_SRGB;
    FBasePassCompositePipeline MismatchedBackBufferView(Desc);
    MismatchedBackBufferView.BuildPasses(Frame, Fixture.Scene, View, Plan);
    Failures += Expect(Plan.Passes.IsEmpty(), "combined pipeline validates the effective backbuffer view format");

    BackBufferView.Desc.Format = EPixelFormat::B8G8R8A8_SRGB;
    CompositeBindGroup.Desc.Layout = nullptr;
    FBasePassCompositePipeline NullBindGroupLayout(Desc);
    NullBindGroupLayout.BuildPasses(Frame, Fixture.Scene, View, Plan);
    Failures += Expect(Plan.Passes.IsEmpty(), "combined pipeline rejects a bind group without its declared layout");

    FBasePassTestBindGroupLayout OtherLayout;
    OtherLayout.Desc.SetIndex = 0;
    CompositeBindGroup.Desc.Layout = &OtherLayout;
    FBasePassCompositePipeline MismatchedBindGroupLayout(Desc);
    MismatchedBindGroupLayout.BuildPasses(Frame, Fixture.Scene, View, Plan);
    Failures += Expect(Plan.Passes.IsEmpty(), "combined pipeline rejects a bind group from a different layout");

    CompositeBindGroup.Desc.Layout = &CompositeBindGroupLayout;
    CompositeBindGroupLayout.Desc.SetIndex = 1;
    FBasePassCompositePipeline WrongSetIndex(Desc);
    WrongSetIndex.BuildPasses(Frame, Fixture.Scene, View, Plan);
    Failures += Expect(Plan.Passes.IsEmpty(), "combined pipeline requires the composite layout at set zero");
    return Failures;
}

bool HasAccess(const FRFGPassNode& Pass, const FRFGResourceHandle Resource, const ERFGAccessType Access, const ERHIResourceState State, const EShaderStage ExpectedStage = EShaderStage::None) noexcept
{
    for (const FRFGPassResourceAccess& Candidate : Pass.ResourceAccesses)
    {
        if (Candidate.Resource == Resource && Candidate.Access.Access == Access && Candidate.Access.State == State && (ExpectedStage == EShaderStage::None || Candidate.Access.ShaderStage == ExpectedStage))
        {
            return true;
        }
    }
    return false;
}

bool HasTransition(const FRFGCompiledPass& Pass, const FRFGResourceHandle Resource, const ERHIResourceState Before, const ERHIResourceState After) noexcept
{
    for (const FRFGBarrierTransition& Barrier : Pass.PreBarriers)
    {
        if (Barrier.Resource == Resource && Barrier.BeforeState == Before && Barrier.AfterState == After)
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
    Fixture.Scene.Meshes[1].Mesh = Fixture.Scene.Meshes[0].Mesh;
    Fixture.Scene.Meshes[1].Material = &Fixture.Materials[3];
    FBasePass Pass(Fixture.Resources);
    FRFGBuilder Builder;
    FRenderGraphBuilderBridge Bridge(Builder);
    const FRFGPassHandle BaseHandle = Builder.AddPass("BasePass", "BasePass", {}, ERFGPassFlags::None, ERFGQueueType::Graphics);
    FRenderPassSetupContext Setup;
    Setup.GraphBridge = &Bridge;
    Setup.PassHandle = BaseHandle;
    Setup.RenderScene = &Fixture.Scene;
    Pass.Setup(Setup);

    const FRFGResourceHandle SceneColor = Bridge.ImportTexture("SceneColor", &Fixture.Color, ERHIResourceState::Undefined);
    const FRFGResourceHandle SceneDepth = Bridge.ImportTexture("Depth", &Fixture.Depth, ERHIResourceState::Undefined);
    const FRFGResourceHandle Vertex0 = Bridge.ImportBuffer("BasePassVertexBuffer", &Fixture.VertexBuffers[0], ERHIResourceState::VertexBuffer);
    const FRFGResourceHandle Index0 = Bridge.ImportBuffer("BasePassIndexBuffer", &Fixture.IndexBuffers[0], ERHIResourceState::IndexBuffer);
    const FRFGResourceHandle Sample = Bridge.ImportTexture("BasePassColorTexture", &Fixture.Samples[0], ERHIResourceState::ShaderResource);
    const FRFGResourceHandle Light = Bridge.ImportBuffer("BasePassFrameLight", &Fixture.LightBuffer, ERHIResourceState::ConstantBuffer);

    const FRFGPassHandle CompositeHandle = Builder.AddPass("Composite", "Composite", {}, ERFGPassFlags::None, ERFGQueueType::Graphics);
    FBasePassTestTexture BackBuffer;
    BackBuffer.Desc.Width = 640;
    BackBuffer.Desc.Height = 480;
    BackBuffer.Desc.Format = EPixelFormat::B8G8R8A8_SRGB;
    BackBuffer.Desc.bIsRenderTarget = true;
    const FRFGResourceHandle BackBufferHandle = Bridge.ImportTexture("BackBuffer", &BackBuffer, ERHIResourceState::Undefined);
    FRFGAccessDesc ColorRead;
    ColorRead.Access = ERFGAccessType::Read;
    ColorRead.State = ERHIResourceState::ShaderResource;
    ColorRead.ShaderStage = EShaderStage::Pixel;
    ColorRead.PipelineStage = ERFGPipelineStage::Graphics;
    Bridge.Read(CompositeHandle, SceneColor, ColorRead);
    FRFGAccessDesc BackBufferWrite = ColorRead;
    BackBufferWrite.Access = ERFGAccessType::Write;
    BackBufferWrite.State = ERHIResourceState::RenderTarget;
    Bridge.Write(CompositeHandle, BackBufferHandle, BackBufferWrite);
    Bridge.MarkOutput(BackBufferHandle);

    const FRFGRecordedGraph& Graph = Builder.GetRecordedGraph();
    const FRFGPassNode& BaseNode = Graph.GetPassNode(BaseHandle);
    Failures += Expect(Graph.GetResourceNode(SceneColor).InitialState == ERHIResourceState::Undefined && Graph.GetResourceNode(SceneDepth).InitialState == ERHIResourceState::Undefined && Graph.GetResourceNode(Vertex0).InitialState == ERHIResourceState::VertexBuffer && Graph.GetResourceNode(Index0).InitialState == ERHIResourceState::IndexBuffer && Graph.GetResourceNode(Sample).InitialState == ERHIResourceState::ShaderResource && HasAccess(BaseNode, SceneColor, ERFGAccessType::Write, ERHIResourceState::RenderTarget) && HasAccess(BaseNode, SceneDepth, ERFGAccessType::Write, ERHIResourceState::DepthStencilWrite) && HasAccess(BaseNode, Vertex0, ERFGAccessType::Read, ERHIResourceState::VertexBuffer) && HasAccess(BaseNode, Index0, ERFGAccessType::Read, ERHIResourceState::IndexBuffer) && HasAccess(BaseNode, Sample, ERFGAccessType::Read, ERHIResourceState::ShaderResource, EShaderStage::Pixel) &&
                           HasAccess(BaseNode, Light, ERFGAccessType::Read, ERHIResourceState::ConstantBuffer, EShaderStage::Pixel) && Graph.IsOutputResource(SceneDepth) && BaseNode.ResourceAccesses.Size() == 6,
        "BasePass declares shared mesh resources and the frame light once alongside color and depth");

    FRFGCompiler Compiler;
    const FRFGCompileResult Compiled = Compiler.Compile(Graph, Builder.BuildSignature());
    bool bDependencyAndBarriers = Compiled.Plan && Compiled.Plan->GetPasses().Size() == 2;
    const FRFGCompiledPass* CompiledBase = nullptr;
    const FRFGCompiledPass* CompiledComposite = nullptr;
    if (bDependencyAndBarriers)
    {
        for (const FRFGCompiledPass& CompiledPass : Compiled.Plan->GetPasses())
        {
            if (CompiledPass.Handle == BaseHandle)
            {
                CompiledBase = &CompiledPass;
            }
            if (CompiledPass.Handle == CompositeHandle)
            {
                CompiledComposite = &CompiledPass;
            }
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
        bDependencyAndBarriers = bIncomingBase && HasTransition(*CompiledBase, SceneColor, ERHIResourceState::Undefined, ERHIResourceState::RenderTarget) && HasTransition(*CompiledBase, SceneDepth, ERHIResourceState::Undefined, ERHIResourceState::DepthStencilWrite) && HasTransition(*CompiledComposite, SceneColor, ERHIResourceState::RenderTarget, ERHIResourceState::ShaderResource);
    }
    Failures += Expect(bDependencyAndBarriers, "SceneColor alone carries BasePass to Composite dependency while depth remains a stored output");
    return Failures;
}

int RunRHIIndexedAndPushConstantContractTests()
{
    int Failures = 0;
    FRHIBufferDesc DefaultBuffer;
    FRHIPipelineDesc_Graphics DefaultPipeline;
    bool bDefaultRenderTargetsUnknown = true;
    for (const EPixelFormat Format : DefaultPipeline.RenderTargetFormats)
    {
        bDefaultRenderTargetsUnknown &= Format == EPixelFormat::Unknown;
    }
    Failures += Expect(DefaultBuffer.Usage == 0 && DefaultBuffer.UsageFlag == 0 && DefaultPipeline.RenderTargetCount == 0 && bDefaultRenderTargetsUnknown, "default RHI descriptors expose deterministic zero usage and attachment formats");

    FRHIPushConstantRangeDesc Disabled;
    FRHIPushConstantRangeDesc Valid;
    Valid.StageFlags = BasePassStages;
    Valid.Size = 80;
    FRHIPushConstantRangeDesc Misaligned = Valid;
    Misaligned.Size = 78;
    FRHIPushConstantRangeDesc Oversized = Valid;
    Oversized.Size = 132;
    FRHIPushConstantRangeDesc MissingStage = Valid;
    MissingStage.StageFlags = EShaderStage::None;
    Failures += Expect(Disabled.IsDisabled() && !Disabled.IsValid() && Valid.IsValid() && Valid.AcceptsWrite(BasePassStages, 80) && !Valid.AcceptsWrite(EShaderStage::Vertex, 80) && !Valid.AcceptsWrite(BasePassStages, 64) && !Misaligned.IsValid() && !Oversized.IsValid() && !MissingStage.IsValid(), "RHI push constant contract accepts only staged aligned ranges up to 128 bytes");
    Failures += Expect(IsValidIndexFormat(EPixelFormat::R16_UINT) && IsValidIndexFormat(EPixelFormat::R32_UINT) && !IsValidIndexFormat(EPixelFormat::R8_UINT) && !IsValidIndexFormat(EPixelFormat::Unknown), "RHI index contract accepts only R16_UINT and R32_UINT");

    FRHIPipelineDesc_Graphics AttachmentPipeline;
    AttachmentPipeline.RenderTargetCount = 1;
    AttachmentPipeline.RenderTargetFormats[0] = EPixelFormat::B8G8R8A8_SRGB;
    AttachmentPipeline.DepthStencilFormat = EPixelFormat::D32_FLOAT;
    FRHIRenderingAttachmentSignature Signature;
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
    Failures += Expect(bCompatible && bRejectsColor && bRejectsDepth && bRejectsCount && Signature.RenderTargetCount == 0 && Signature.RenderTargetFormats[0] == EPixelFormat::Unknown && Signature.DepthStencilFormat == EPixelFormat::Unknown, "active rendering signature gates every pipeline bind and resets between command scopes");
    return Failures;
}

} // namespace

int RunBasePassTests()
{
    static_assert(sizeof(FBasePassObjectConstants) == 128);
    int Failures = 0;
    Failures += RunRHIIndexedAndPushConstantContractTests();
    Failures += RunBasePassPlanValidationTests();
    Failures += RunBasePassCompositePipelineTests();
    Failures += RunBasePassRecordTests();
    Failures += RunBasePassGraphContractTests();
    Failures += RunFrameLightingTests();
    return Failures;
}

} // namespace LE
