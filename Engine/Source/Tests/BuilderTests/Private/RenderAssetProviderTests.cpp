#include "Asset/AssetRegistry.h"
#include "AssetFormat/AssetFormat.h"
#include "RHI/RHIBindGroup.h"
#include "RHI/RHIBuffer.h"
#include "RHI/RHICommandAllocator.h"
#include "RHI/RHICommandList.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHIPipeline.h"
#include "RHI/RHISampler.h"
#include "RHI/RHITexture.h"
#include "Renderer/RenderAssetProvider.h"
#include "World/World.h"
#include "WorldRenderer/WorldRenderer.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

namespace LE
{
namespace
{

constexpr std::size_t VertexBytes = 3 * sizeof(FBasePassVertex);
constexpr std::size_t IndexBytes = 3 * sizeof(uint32);

bool ExpectProvider(const bool Condition, const char* Name)
{
    if (!Condition)
    {
        std::fprintf(stderr, "FAILED: RenderAssetProvider: %s\n", Name);
    }
    return Condition;
}

struct FUploadTrace
{
    unsigned Created = 0;
    unsigned Mapped = 0;
    unsigned Unmapped = 0;
    unsigned Destroyed = 0;
    unsigned ImagesDestroyed = 0;
    unsigned ViewsCreated = 0;
    unsigned ViewsDestroyed = 0;
    unsigned BindingsDestroyed = 0;
    unsigned ListsDestroyed = 0;
    unsigned AllocatorsDestroyed = 0;
    unsigned FencesDestroyed = 0;
    uint32 Steps = 0;
    unsigned FailedRecordStep = 0;
    bool bFailSubmit = false;
    bool bFailWait = false;
    bool bLostOnSubmit = false;
    bool bFailDrain = false;
    bool bCopyMatched = false;
    uint8 Uploaded[16]{};
};

class FUploadTestBuffer final : public FRHIBuffer
{
public:
    FUploadTestBuffer(const FRHIBufferDesc& InDesc, FUploadTrace& InTrace, const bool FailMap) : Desc(InDesc), Trace(InTrace), bFailMap(FailMap) {}

    ~FUploadTestBuffer() override { ++Trace.Destroyed; }

    void* Map(const uint64 Offset, const uint64 Size) override
    {
        ++Trace.Mapped;
        return !bFailMap && Offset == 0 && Size == Desc.Size && Size <= sizeof(Bytes) ? Bytes : nullptr;
    }

    void Unmap() override { ++Trace.Unmapped; }
    const FRHIBufferDesc& GetDesc() const override { return Desc; }

    uint8 Bytes[VertexBytes]{};

private:
    FRHIBufferDesc Desc;
    FUploadTrace& Trace;
    bool bFailMap;
};

class FUploadTestTexture final : public FRHITexture
{
public:
    FUploadTestTexture(const FRHITextureDesc& InDesc, FUploadTrace& InTrace) : Desc(InDesc), Trace(InTrace) {}
    ~FUploadTestTexture() override { ++Trace.ImagesDestroyed; }
    const FRHITextureDesc& GetDesc() const override { return Desc; }

private:
    FRHITextureDesc Desc;
    FUploadTrace& Trace;
};

class FUploadTestView final : public FRHITextureView
{
public:
    FUploadTestView(const FRHITextureViewDesc& InDesc, FUploadTrace& InTrace) : Desc(InDesc), Trace(InTrace) {}
    ~FUploadTestView() override { ++Trace.ViewsDestroyed; }
    FRHITexture* GetTexture() const override { return Desc.Texture; }
    const FRHITextureViewDesc& GetDesc() const override { return Desc; }

private:
    FRHITextureViewDesc Desc;
    FUploadTrace& Trace;
};

class FUploadTestAllocator final : public FRHICommandAllocator
{
public:
    explicit FUploadTestAllocator(FUploadTrace& InTrace) : Trace(InTrace) {}
    ~FUploadTestAllocator() override { ++Trace.AllocatorsDestroyed; }
    bool Reset() override { return true; }

private:
    FUploadTrace& Trace;
};

class FUploadTestFence final : public FRHIFence
{
public:
    explicit FUploadTestFence(FUploadTrace& InTrace) : Trace(InTrace) {}
    ~FUploadTestFence() override { ++Trace.FencesDestroyed; }
    void Reset() override {}
    void Wait(uint64) override {}
    bool IsSignaled() override { return !Trace.bFailWait; }
    ERHIFenceWaitResult WaitChecked(uint64) override
    {
        if (Trace.Steps == 123456)
        {
            Trace.Steps = 1234567;
        }
        return Trace.bFailWait ? ERHIFenceWaitResult::Timeout : ERHIFenceWaitResult::Success;
    }

private:
    FUploadTrace& Trace;
};

class FUploadTestCommands final : public FRHICommandList
{
public:
    explicit FUploadTestCommands(FUploadTrace& InTrace) : Trace(InTrace) {}
    ~FUploadTestCommands() override { ++Trace.ListsDestroyed; }
    void Begin() override {}
    void End() override {}
    bool BeginChecked() override { return Step(1); }
    bool EndChecked() override { return Step(5); }
    bool TextureUploadBarrier(FRHITexture* Texture, ERHIResourceState Before, ERHIResourceState After) override { return Texture != nullptr && ((Before == ERHIResourceState::Undefined && After == ERHIResourceState::CopyDestination && Step(2)) || (Before == ERHIResourceState::CopyDestination && After == ERHIResourceState::ShaderResource && Step(4))); }
    bool CopyBufferToTexture2D(FRHIBuffer* Source, FRHITexture* Destination, uint64 Offset) override
    {
        if (Source == nullptr || Destination == nullptr || Offset != 0)
        {
            return false;
        }
        const auto& Desc = Destination->GetDesc();
        const auto Bytes = static_cast<std::size_t>(Desc.Width) * Desc.Height * 4;
        Trace.bCopyMatched = Source->GetDesc().UsageFlag == static_cast<uint32>(EBufferUsageFlags::TransferSrc) && Desc.bIsTransferDestination && Desc.bIsShaderResource && Desc.Format == EPixelFormat::R8G8B8A8_SRGB && Bytes == Source->GetDesc().Size && Bytes <= sizeof(Trace.Uploaded);
        if (Trace.bCopyMatched)
        {
            std::memcpy(Trace.Uploaded, static_cast<FUploadTestBuffer*>(Source)->Bytes, Bytes);
        }
        return Trace.bCopyMatched && Step(3);
    }
    void ResourceBarriers(const FRHIBarrierBatch&) override {}
    void BeginRenderPass(const FRHIRenderPassDesc&) override {}
    void EndRenderPass() override {}
    void SetGraphicsPipeline(FRHIPipeline_Graphics*) override {}
    void SetViewport(const FRHIViewport&) override {}
    void SetScissorRect(const FRHIScissorRect&) override {}
    void SetVertexBuffer(uint32, FRHIBuffer*, uint64) override {}
    void SetIndexBuffer(FRHIBuffer*, uint64, EPixelFormat) override {}
    void SetBindGroup(uint32, FRHIBindGroup*) override {}
    void SetPushConstants(EShaderStage, const void*, uint32) override {}
    void Draw(uint32, uint32, uint32) override {}
    void DrawIndexed(uint32, uint32, uint32, int32, uint32) override {}

private:
    bool Step(unsigned Index)
    {
        Trace.Steps = Index == 1 ? 1 : Trace.Steps * 10 + Index;
        return Trace.FailedRecordStep != Index;
    }
    FUploadTrace& Trace;
};

class FUploadTestQueue final : public FRHIQueue
{
public:
    explicit FUploadTestQueue(FUploadTrace& InTrace) : Trace(InTrace) {}
    ERHIQueueSubmitResult Submit(const FRHISubmitInfo& SubmitInfo) override
    {
        if (Trace.Steps != 12345 || SubmitInfo.CmdList == nullptr || SubmitInfo.FenceToSignal == nullptr)
        {
            return ERHIQueueSubmitResult::InvalidArguments;
        }
        Trace.Steps = 123456;
        if (Trace.bLostOnSubmit)
        {
            return ERHIQueueSubmitResult::DeviceLost;
        }
        return Trace.bFailSubmit ? ERHIQueueSubmitResult::Error : ERHIQueueSubmitResult::Success;
    }
    void WaitIdle() override {}
    ERHIFenceWaitResult WaitIdleChecked() override
    {
        ++DrainCalls;
        return Trace.bFailDrain ? ERHIFenceWaitResult::Error : ERHIFenceWaitResult::Success;
    }
    EQueueType GetType() const override { return EQueueType::Graphics; }
    unsigned DrainCalls = 0;

private:
    FUploadTrace& Trace;
};

class FUploadTestLayout final : public FRHIBindGroupLayout
{
public:
    explicit FUploadTestLayout(const FRHIBindGroupLayoutDesc& InDesc) : Desc(InDesc) {}
    const FRHIBindGroupLayoutDesc& GetDesc() const override { return Desc; }

private:
    FRHIBindGroupLayoutDesc Desc;
};

class FUploadTestBindGroup final : public FRHIBindGroup
{
public:
    FUploadTestBindGroup(const FRHIBindGroupDesc& InDesc, FUploadTrace& InTrace) : Desc(InDesc), Trace(InTrace) {}
    ~FUploadTestBindGroup() override { ++Trace.BindingsDestroyed; }
    const FRHIBindGroupDesc& GetDesc() const override { return Desc; }

private:
    FRHIBindGroupDesc Desc;
    FUploadTrace& Trace;
};

class FUploadTestSampler final : public FRHISampler
{
public:
    explicit FUploadTestSampler(const FRHISamplerDesc& InDesc) : Desc(InDesc) {}
    const FRHISamplerDesc& GetDesc() const override { return Desc; }

private:
    FRHISamplerDesc Desc;
};

class FUploadTestDevice final : public FRHIDevice
{
public:
    FRHIBuffer* CreateBuffer(const FRHIBufferDesc& Desc) override
    {
        ++Attempts;
        if (Attempts == FailCreate)
        {
            return nullptr;
        }
        ++Trace.Created;
        return new FUploadTestBuffer(Desc, Trace, Attempts == FailMap);
    }

    FRHIQueue* GetGraphicsQueue() const override { return &Queue; }
    FRHITexture* CreateTexture(const FRHITextureDesc& Desc) override { return bFailImage ? nullptr : new FUploadTestTexture(Desc, Trace); }
    FRHITextureView* CreateTextureView(const FRHITextureViewDesc& Desc) override
    {
        if (bFailView || Trace.Steps != 1234567 || Trace.bFailWait)
        {
            return nullptr;
        }
        ++Trace.ViewsCreated;
        return new FUploadTestView(Desc, Trace);
    }
    FRHIShader* CreateShaderFromFile(EShaderStage, const void*, uint64, const String&) override { return nullptr; }
    FRHIPipeline_Graphics* CreateGraphicsPipeline(const FRHIPipelineDesc_Graphics&) override { return nullptr; }
    FRHICommandAllocator* CreateCommandAllocator(EQueueType) override { return bFailCommands ? nullptr : new FUploadTestAllocator(Trace); }
    FRHICommandList* CreateCommandList(FRHICommandAllocator* Allocator) override { return Allocator == nullptr ? nullptr : new FUploadTestCommands(Trace); }
    FRHISemaphore* CreateBinarySemaphore() override { return nullptr; }
    FRHIFence* CreateFence(bool) override { return new FUploadTestFence(Trace); }
    FRHISwapchain* CreateSwapchain(const FRHISwapchainDesc&) override { return nullptr; }
    FRHIBindGroup* CreateBindGroup(const FRHIBindGroupDesc& Desc) override
    {
        if (bFailBinding || Desc.Items.Size() != 2 || Desc.Items[0].TextureView == nullptr || Desc.Items[1].Sampler == nullptr)
        {
            return nullptr;
        }
        return new FUploadTestBindGroup(Desc, Trace);
    }
    FRHIBindGroupLayout* CreateBindGroupLayout(const FRHIBindGroupLayoutDesc& Desc) override { return new FUploadTestLayout(Desc); }
    FRHISampler* CreateSampler(const FRHISamplerDesc& Desc) override { return Desc.MinFilter == ESamplerFilter::Nearest && Desc.MagFilter == ESamplerFilter::Nearest && Desc.AddressU == ESamplerAddressMode::ClampToEdge && Desc.AddressV == ESamplerAddressMode::ClampToEdge && Desc.MaxLOD == 0.0f ? new FUploadTestSampler(Desc) : nullptr; }
    void* GetBindlessHeapGPUDescriptor() const override { return nullptr; }
    uint32 AllocateBindlessIndex(FRHIResource*) override { return 0; }

    FUploadTrace Trace;
    mutable FUploadTestQueue Queue{Trace};
    unsigned Attempts = 0;
    unsigned FailCreate = 0;
    unsigned FailMap = 0;
    bool bFailImage = false;
    bool bFailView = false;
    bool bFailCommands = false;
    bool bFailBinding = false;
};

class FProviderTestPipeline final : public FRHIPipeline_Graphics
{
public:
    const FRHIPipelineDesc_Graphics& GetDesc() const override { return Desc; }
    FRHIPipelineDesc_Graphics Desc;
};

struct FProviderFixture
{
    FProviderFixture()
    {
        Layout = CreateBasePassMaterialLayout(Device);
        Pipeline.Desc.BindGroupLayouts.PushBack(Layout);
        FRHIBindGroupLayoutDesc LightingLayoutDesc;
        LightingLayoutDesc.SetIndex = 1;
        LightingLayout = Device.CreateBindGroupLayout(LightingLayoutDesc);
        Pipeline.Desc.BindGroupLayouts.PushBack(LightingLayout);
        bInitialized = Provider.Initialize(&Device, &Pipeline, Layout);
    }

    ~FProviderFixture()
    {
        Provider.Shutdown();
        RHI::DestroyResource(LightingLayout);
        RHI::DestroyResource(Layout);
    }

    FUploadTestDevice Device;
    FProviderTestPipeline Pipeline;
    FRHIBindGroupLayout* Layout = nullptr;
    FRHIBindGroupLayout* LightingLayout = nullptr;
    FRenderAssetProvider Provider;
    bool bInitialized = false;
};

FMeshAssetData MakeMesh()
{
    FMeshAssetData Data;
    Data.Positions = {
        Math::Vector3f(-0.5f, -0.5f, 0.0f),
        Math::Vector3f(0.5f, -0.5f, 0.0f),
        Math::Vector3f(0.0f, 0.5f, 0.0f),
    };
    Data.Indices = {0, 2, 1};
    Data.BoundsMin = Math::Vector3f(-0.5f, -0.5f, 0.0f);
    Data.BoundsMax = Math::Vector3f(0.5f, 0.5f, 0.0f);
    return Data;
}

bool TestNormalVertexUploads()
{
    FProviderFixture Fixture;
    if (!ExpectProvider(Fixture.bInitialized, "material provider accepts set 0 with a separate lighting set"))
    {
        return false;
    }
    FMeshAssetData Data = MakeMesh();
    const Math::Vector3f ExpectedNormals[] = {
        Math::Vector3f(0.0f, 0.0f, 1.0f),
        Math::Vector3f(0.0f, 0.6f, -0.8f),
        Math::Vector3f(-1.0f, 0.0f, 0.0f),
    };
    Data.UVs = {Math::Vector2f(0.0f, 1.0f), Math::Vector2f(1.0f, 1.0f), Math::Vector2f(0.5f, 0.0f)};
    FAssetId Id;
    if (!FAssetId::TryParse("aef10000-0000-4000-8000-000000000001", Id))
    {
        return false;
    }

    bool Passed = true;
    for (const bool bAuthoredNormals : {false, true})
    {
        if (bAuthoredNormals)
        {
            Data.Normals = {ExpectedNormals[0], ExpectedNormals[1], ExpectedNormals[2]};
        }
        Array<uint8> Cooked;
        FAssetFileHeaderView Header;
        FMeshAssetData Decoded;
        const bool bDecoded = EncodeAssetFile(Id, "Tests/Provider.mesh", Data, Cooked) == EAssetFormatResult::Success && ReadAssetFileHeader(Cooked.AsSpan(), Header) == EAssetFormatResult::Success && Header.Version == (bAuthoredNormals ? MeshNormalsAssetFileVersion : TexturedAssetFileVersion) && DecodeAssetFile(Cooked.AsSpan(), Decoded) == EAssetFormatResult::Success;
        FMeshAsset* Mesh = bDecoded ? FRenderAssetProvider::CreateMesh(&Fixture.Provider, Decoded) : nullptr;
        if (!ExpectProvider(Mesh != nullptr, "provider constructs baked mesh with and without authored normals"))
        {
            return false;
        }
        const auto* const Vertices = static_cast<const FUploadTestBuffer*>(Mesh->VertexBuffer);
        FBasePassVertex Uploaded[3];
        std::memcpy(Uploaded, Vertices->Bytes, sizeof(Uploaded));
        bool bMatched = Mesh->bHasUV && Mesh->bHasNormals == bAuthoredNormals && Vertices->GetDesc().Size == sizeof(Uploaded);
        for (std::size_t Index = 0; Index < 3; ++Index)
        {
            bMatched = bMatched && Uploaded[Index].Position == Data.Positions[Index] && Uploaded[Index].UV == Data.UVs[Index] && Uploaded[Index].Normal == (bAuthoredNormals ? ExpectedNormals[Index] : Math::Vector3f{});
        }
        Passed &= ExpectProvider(bMatched, "GPU vertex bytes carry authored v3 normals or zero legacy normals");
        FRenderAssetProvider::DestroyMesh(&Fixture.Provider, Mesh);
    }

    const unsigned AttemptsBeforeInvalidData = Fixture.Device.Attempts;
    Data.Normals = {ExpectedNormals[0], ExpectedNormals[1]};
    FMeshAsset* Mesh = FRenderAssetProvider::CreateMesh(&Fixture.Provider, Data);
    Passed &= ExpectProvider(Mesh == nullptr && Fixture.Device.Attempts == AttemptsBeforeInvalidData, "malformed vertex normal count never allocates GPU buffers");
    FRenderAssetProvider::DestroyMesh(&Fixture.Provider, Mesh);

    Data.Normals = {ExpectedNormals[0], Math::Vector3f{}, ExpectedNormals[2]};
    Mesh = FRenderAssetProvider::CreateMesh(&Fixture.Provider, Data);
    Passed &= ExpectProvider(Mesh == nullptr && Fixture.Device.Attempts == AttemptsBeforeInvalidData, "zero normal never allocates GPU buffers");
    FRenderAssetProvider::DestroyMesh(&Fixture.Provider, Mesh);

    Data.Normals[1] = Math::Vector3f(0.0f, std::numeric_limits<float>::infinity(), 0.0f);
    Mesh = FRenderAssetProvider::CreateMesh(&Fixture.Provider, Data);
    Passed &= ExpectProvider(Mesh == nullptr && Fixture.Device.Attempts == AttemptsBeforeInvalidData, "non-finite normal never allocates GPU buffers");
    FRenderAssetProvider::DestroyMesh(&Fixture.Provider, Mesh);
    return Passed;
}

bool TestFailedUploads()
{
    FMeshAssetData Data = MakeMesh();
    Data.Normals = {Math::Vector3f(0.0f, 0.0f, 1.0f), Math::Vector3f(0.0f, 0.0f, 1.0f), Math::Vector3f(0.0f, 0.0f, 1.0f)};
    FProviderFixture Fixture;
    if (!Fixture.bInitialized)
    {
        return false;
    }
    auto& Device = Fixture.Device;
    auto& Provider = Fixture.Provider;
    Device.FailCreate = 2;
    FMeshAsset* Mesh = FRenderAssetProvider::CreateMesh(&Provider, Data);
    const bool SecondFailed = Mesh == nullptr && Device.Trace.Created == 1 && Device.Trace.Mapped == 1 && Device.Trace.Unmapped == 1 && Device.Trace.Destroyed == 1;
    FRenderAssetProvider::DestroyMesh(&Provider, Mesh);
    bool Passed = ExpectProvider(SecondFailed, "failed index buffer creation destroys the uploaded vertex buffer");

    Device.FailMap = Device.Attempts + 2;
    Mesh = FRenderAssetProvider::CreateMesh(&Provider, Data);
    const bool IndexMapFailed = Mesh == nullptr && Device.Trace.Created == 3 && Device.Trace.Mapped == 3 && Device.Trace.Unmapped == 2 && Device.Trace.Destroyed == 3;
    FRenderAssetProvider::DestroyMesh(&Provider, Mesh);
    Passed &= ExpectProvider(IndexMapFailed, "failed index mapping destroys both buffers");

    const unsigned AttemptsBeforeInvalidData = Device.Attempts;
    Data.Indices[1] = 3;
    Mesh = FRenderAssetProvider::CreateMesh(&Provider, Data);
    Passed &= ExpectProvider(Mesh == nullptr && Device.Attempts == AttemptsBeforeInvalidData, "invalid mesh data is rejected before RHI allocation");
    FRenderAssetProvider::DestroyMesh(&Provider, Mesh);
    return Passed;
}

bool AddWorldObjects(FWorld& World, FMeshAssetHandle MeshHandle, FMaterialAssetHandle MaterialHandle)
{
    auto* const Meshes = World.GetMeshComponents();
    auto* const Cameras = World.GetCameraComponents();
    if (Meshes == nullptr || Cameras == nullptr)
    {
        return false;
    }
    FEntity Camera;
    FTransform CameraPose;
    CameraPose.Translation = Math::Vector3f(0.0f, 0.0f, 3.0f);
    if (!World.TryCreateEntity(Camera) || World.TryAddTransform(Camera, CameraPose) != EComponentMutationResult::Added || World.TryAddComponent(*Cameras, Camera, FWorldCameraComponent{}) != EComponentMutationResult::Added)
    {
        return false;
    }
    for (const float X : {-0.6f, 0.6f})
    {
        FEntity Entity;
        FTransform Pose;
        Pose.Translation.Values[0] = X;
        FWorldMeshComponent Component;
        Component.Mesh = MeshHandle;
        Component.Material = MaterialHandle;
        if (!World.TryCreateEntity(Entity) || World.TryAddTransform(Entity, Pose) != EComponentMutationResult::Added || World.TryAddComponent(*Meshes, Entity, std::move(Component)) != EComponentMutationResult::Added)
        {
            return false;
        }
    }
    return true;
}

bool TestSharedMaterialAcrossExtents()
{
    FProviderFixture Fixture;
    if (!Fixture.bInitialized)
    {
        return false;
    }
    auto& Device = Fixture.Device;
    auto& Pipeline = Fixture.Pipeline;
    auto& Provider = Fixture.Provider;
    FMeshAssetData MeshData = MakeMesh();
    FMaterialAssetData MaterialData;
    MaterialData.BaseColor = Math::Vector3f(0.25f, 2.0f, 0.5f);
    FAssetId MeshId;
    FAssetId MaterialId;
    if (!FAssetId::TryParse("e8100000-0000-4000-8000-000000000001", MeshId) || !FAssetId::TryParse("e8100000-0000-4000-8000-000000000002", MaterialId))
    {
        return false;
    }

    FAssetRegistry Registry;
    FMeshAsset* const Mesh = FRenderAssetProvider::CreateMesh(&Provider, MeshData);
    if (!ExpectProvider(Mesh != nullptr, "decoded mesh creates a GPU payload"))
    {
        return false;
    }
    FMeshAssetHandle MeshHandle;
    TAssetRegistration<FMeshAsset> MeshRegistration;
    MeshRegistration.Id = MeshId;
    MeshRegistration.SoftPath = "Game/Test/Mesh.leasset";
    MeshRegistration.DisplayName = "Mesh";
    MeshRegistration.ProviderName = "Test.RenderAssetProvider";
    MeshRegistration.Object = Mesh;
    MeshRegistration.Ownership = EAssetOwnership::OwnedByProviderThunk;
    MeshRegistration.Destroy = &FRenderAssetProvider::DestroyMesh;
    MeshRegistration.ProviderContext = &Provider;
    if (!ExpectProvider(Registry.Register(MeshRegistration, MeshHandle) == EAssetRegisterResult::Success, "registry accepts the owned render mesh"))
    {
        FRenderAssetProvider::DestroyMesh(&Provider, Mesh);
        return false;
    }
    FMaterialAsset* const Material = FRenderAssetProvider::CreateMaterial(&Provider, MaterialData, nullptr);
    if (!ExpectProvider(Material != nullptr, "decoded material borrows the shared pipeline"))
    {
        return false;
    }
    FMaterialAssetHandle MaterialHandle;
    TAssetRegistration<FMaterialAsset> MaterialRegistration;
    MaterialRegistration.Id = MaterialId;
    MaterialRegistration.SoftPath = "Game/Test/Material.leasset";
    MaterialRegistration.DisplayName = "Material";
    MaterialRegistration.ProviderName = "Test.RenderAssetProvider";
    MaterialRegistration.Object = Material;
    MaterialRegistration.Ownership = EAssetOwnership::OwnedByProviderThunk;
    MaterialRegistration.Destroy = &FRenderAssetProvider::DestroyMaterial;
    MaterialRegistration.ProviderContext = &Provider;
    if (!ExpectProvider(Registry.Register(MaterialRegistration, MaterialHandle) == EAssetRegisterResult::Success, "registry accepts the owned render material"))
    {
        FRenderAssetProvider::DestroyMaterial(&Provider, Material);
        return false;
    }

    const auto* const Vertices = static_cast<const FUploadTestBuffer*>(Mesh->VertexBuffer);
    const auto* const Indices = static_cast<const FUploadTestBuffer*>(Mesh->IndexBuffer);
    FBasePassVertex FirstVertex;
    std::memcpy(&FirstVertex, Vertices->Bytes, sizeof(FirstVertex));
    bool Passed = ExpectProvider(Mesh->IndexFormat == EPixelFormat::R32_UINT && Mesh->IndexCount == 3 && Mesh->VertexCount == 3 && Vertices->GetDesc().Size == VertexBytes && Indices->GetDesc().Size == IndexBytes && Vertices->GetDesc().UsageFlag == static_cast<uint32>(EBufferUsageFlags::VertexBuffer) && Indices->GetDesc().UsageFlag == static_cast<uint32>(EBufferUsageFlags::IndexBuffer) && FirstVertex.Position == MeshData.Positions[0] && FirstVertex.Color == Math::Vector3f(1.0f) && std::memcmp(Indices->Bytes, MeshData.Indices.Data(), IndexBytes) == 0 && Device.Trace.Mapped == 3 && Device.Trace.Unmapped == 3, "payload uploads white vertices and R32 indices with appropriate usages");

    {
        FWorld World;
        if (!ExpectProvider(AddWorldObjects(World, MeshHandle, MaterialHandle), "two entities share the same render mesh and material"))
        {
            return false;
        }
        FWorldRenderer Bridge;
        uint64 FrameIndex = 0;
        for (const uint32 Width : {1280u, 640u})
        {
            FWorldRenderSnapshotBuildContext Context;
            Context.FrameIndex = FrameIndex++;
            Context.ViewportWidth = Width;
            Context.ViewportHeight = 720;
            FWorldRenderSnapshot Snapshot;
            const auto Status = Bridge.BuildSnapshot(World, Registry, Context, Snapshot);
            bool BothVisible = Status == EWorldRenderSnapshotBuildResult::Success && Snapshot.GetRenderScene().Meshes.Size() == 2;
            if (BothVisible)
            {
                for (const FRenderMeshProxy& Proxy : Snapshot.GetRenderScene().Meshes)
                {
                    BothVisible &= Proxy.GraphicsPipeline == &Pipeline && Proxy.MaterialAssetId == MaterialId && Proxy.VertexBuffer == Mesh->VertexBuffer && Proxy.IndexBuffer == Mesh->IndexBuffer && Proxy.BaseColor == Math::Vector4f(0.25f, 2.0f, 0.5f, 1.0f) && Proxy.BaseColorTexture == Material->BaseColorTexture && Proxy.MaterialBindGroup == Material->MaterialBindGroup;
                }
            }
            Passed &= ExpectProvider(BothVisible, "both entities resolve the same payload and pipeline after extent changes");
        }
    }

    Passed &= ExpectProvider(Registry.Unregister(MaterialHandle) == EAssetUnregisterResult::Success && Registry.Unregister(MeshHandle) == EAssetUnregisterResult::Success && Device.Trace.Destroyed == 3, "registry releases provider-owned buffers and material before the borrowed pipeline");
    return Passed;
}

bool TestFailedTextureUpload()
{
    FProviderFixture Fixture;
    if (!Fixture.bInitialized)
    {
        return false;
    }
    auto& Provider = Fixture.Provider;
    auto& Device = Fixture.Device;
    auto& Trace = Device.Trace;
    FCpuTextureAsset Texture;
    Texture.Width = 1;
    Texture.Height = 1;
    Texture.Pixels = {12, 34, 56, 78};
    FMaterialAssetData Data;
    if (!FAssetId::TryParse("e8100000-0000-4000-8000-000000000003", Data.BaseColorTexture))
    {
        return false;
    }
    auto Rejected = [&](ETextureUploadResult Result) {
        auto* Material = FRenderAssetProvider::CreateMaterial(&Provider, Data, &Texture);
        bool bPass = Material == nullptr && Provider.GetLastTextureUploadResult() == Result;
        FRenderAssetProvider::DestroyMaterial(&Provider, Material);
        return bPass;
    };
    bool Passed = true;
    Device.FailCreate = Device.Attempts + 1;
    Passed &= ExpectProvider(Rejected(ETextureUploadResult::StagingCreationFailed), "staging creation failure rejects upload");
    Device.FailMap = Device.Attempts + 1;
    Passed &= ExpectProvider(Rejected(ETextureUploadResult::StagingMappingFailed), "staging map failure rejects upload");
    Device.bFailImage = true;
    Passed &= ExpectProvider(Rejected(ETextureUploadResult::TextureCreationFailed), "image creation failure rejects upload");
    Device.bFailImage = false;
    Device.bFailCommands = true;
    Passed &= ExpectProvider(Rejected(ETextureUploadResult::CommandCreationFailed), "command creation failure rejects upload");
    Device.bFailCommands = false;
    for (unsigned Step = 1; Step <= 5; ++Step)
    {
        Trace.FailedRecordStep = Step;
        Passed &= ExpectProvider(Rejected(ETextureUploadResult::RecordingFailed) && Trace.ViewsCreated == 0, "every checked recording failure prevents submission or publication");
    }
    Trace.FailedRecordStep = 0;
    Trace.bFailSubmit = true;
    Passed &= ExpectProvider(Rejected(ETextureUploadResult::SubmissionFailed) && Trace.ViewsCreated == 0, "submit failure prevents texture publication");
    Trace.bFailSubmit = false;
    Device.bFailView = true;
    Passed &= ExpectProvider(Rejected(ETextureUploadResult::ViewCreationFailed), "view creation failure releases completed upload");
    Device.bFailView = false;

    Trace.bFailWait = true;
    const unsigned DestroyedBuffers = Trace.Destroyed;
    const unsigned DestroyedImages = Trace.ImagesDestroyed;
    const unsigned DestroyedLists = Trace.ListsDestroyed;
    const unsigned DestroyedAllocators = Trace.AllocatorsDestroyed;
    const unsigned DestroyedFences = Trace.FencesDestroyed;
    Passed &= ExpectProvider(Rejected(ETextureUploadResult::CompletionFailed) && Rejected(ETextureUploadResult::CompletionFailed) && Trace.Destroyed == DestroyedBuffers && Trace.ImagesDestroyed == DestroyedImages && Trace.ListsDestroyed == DestroyedLists && Trace.AllocatorsDestroyed == DestroyedAllocators && Trace.FencesDestroyed == DestroyedFences && Trace.ViewsCreated == 0 && Device.Queue.DrainCalls == 0, "unconfirmed completion retains all submitted resources and blocks another upload");
    Trace.bFailWait = false;
    auto* Material = FRenderAssetProvider::CreateMaterial(&Provider, Data, &Texture);
    Passed &= ExpectProvider(Material != nullptr && Trace.Steps == 1234567 && Trace.bCopyMatched && std::memcmp(Texture.Pixels.Data(), Trace.Uploaded, Texture.Pixels.Size()) == 0 && Trace.Destroyed == DestroyedBuffers + 2 && Trace.ImagesDestroyed == DestroyedImages + 1 && Provider.GetTextureUploadCount() == 1 && Device.Queue.DrainCalls == 0, "completed retired upload permits a checked retry with exact pixel bytes and no global wait");
    FRenderAssetProvider::DestroyMaterial(&Provider, Material);

    Trace.bFailWait = true;
    const unsigned BeforeTerminalCleanup = Trace.ImagesDestroyed;
    Passed &= ExpectProvider(Rejected(ETextureUploadResult::CompletionFailed) && Trace.ImagesDestroyed == BeforeTerminalCleanup, "retain the pending image for terminal retirement");
    Provider.Shutdown();
    Passed &= ExpectProvider(Device.Queue.DrainCalls == 1 && Trace.ImagesDestroyed == BeforeTerminalCleanup + 1, "checked terminal queue drain releases the pending image");
    return Passed;
}

bool TestSharedAuthoredTexture()
{
    FProviderFixture Fixture;
    if (!Fixture.bInitialized)
    {
        return false;
    }
    auto& Provider = Fixture.Provider;
    auto& Device = Fixture.Device;
    auto& Trace = Device.Trace;
    FCpuTextureAsset Texture;
    Texture.Width = 2;
    Texture.Height = 2;
    Texture.Pixels = {255, 20, 40, 255, 0, 120, 0, 255, 0, 0, 255, 255, 128, 128, 0, 255};
    FMaterialAssetData Data;
    if (!FAssetId::TryParse("e8100000-0000-4000-8000-000000000003", Data.BaseColorTexture))
    {
        return false;
    }
    FMaterialAsset* A = FRenderAssetProvider::CreateMaterial(&Provider, Data, &Texture);
    Data.BaseColor = Math::Vector3f(0.5f, 0.8f, 0.9f);
    FMaterialAsset* B = FRenderAssetProvider::CreateMaterial(&Provider, Data, &Texture);
    bool Passed = ExpectProvider(A != nullptr && B != nullptr && A->MaterialBindGroup != B->MaterialBindGroup && A->BaseColorTexture == B->BaseColorTexture && A->bRequiresUV && B->bRequiresUV && Provider.GetTextureUploadCount() == 1 && Trace.ViewsCreated == 1, "multiple material bindings share one uploaded RGBA8 image");
    Device.bFailBinding = true;
    FMaterialAsset* Failed = FRenderAssetProvider::CreateMaterial(&Provider, Data, &Texture);
    Passed &= ExpectProvider(Failed == nullptr && Provider.GetLastTextureUploadResult() == ETextureUploadResult::BindingCreationFailed && Trace.ImagesDestroyed == 0, "binding failure releases only its candidate reference");
    FRenderAssetProvider::DestroyMaterial(&Provider, Failed);
    FRenderAssetProvider::DestroyMaterial(&Provider, A);
    Passed &= ExpectProvider(Trace.BindingsDestroyed == 1 && Trace.ImagesDestroyed == 0, "returning a failed registration's payload leaves a shared image used by another material");
    FRenderAssetProvider::DestroyMaterial(&Provider, B);
    Passed &= ExpectProvider(Trace.BindingsDestroyed == 2 && Trace.ViewsDestroyed == 1 && Trace.ImagesDestroyed == 1, "last reference releases shared view and image");
    return Passed;
}

} // namespace

bool RunRenderAssetProviderTests()
{
    bool Passed = TestFailedUploads();
    Passed &= TestNormalVertexUploads();
    Passed &= TestSharedMaterialAcrossExtents();
    Passed &= TestFailedTextureUpload();
    Passed &= TestSharedAuthoredTexture();
    return Passed;
}

} // namespace LE
