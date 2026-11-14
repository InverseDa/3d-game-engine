#include "Asset/AssetRegistry.h"
#include "AssetFormat/AssetFormat.h"
#include "RHI/RHIBuffer.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHIPipeline.h"
#include "Renderer/RenderAssetProvider.h"
#include "World/World.h"
#include "WorldRenderer/WorldRenderer.h"

#include <cstdio>
#include <cstring>
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
};

class FUploadTestBuffer final : public FRHIBuffer
{
public:
    FUploadTestBuffer(const FRHIBufferDesc& InDesc, FUploadTrace& InTrace, const bool FailMap)
        : Desc(InDesc), Trace(InTrace), bFailMap(FailMap) {}

    ~FUploadTestBuffer() override { ++Trace.Destroyed; }

    void* Map(const uint64 Offset, const uint64 Size) override
    {
        ++Trace.Mapped;
        return !bFailMap && Offset == 0 && Size == Desc.Size && Size <= sizeof(Bytes)
            ? Bytes : nullptr;
    }

    void Unmap() override { ++Trace.Unmapped; }
    const FRHIBufferDesc& GetDesc() const override { return Desc; }

    uint8 Bytes[VertexBytes]{};

private:
    FRHIBufferDesc Desc;
    FUploadTrace& Trace;
    bool bFailMap;
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

    FRHIQueue* GetGraphicsQueue() const override { return nullptr; }
    FRHITexture* CreateTexture(const FRHITextureDesc&) override { return nullptr; }
    FRHITextureView* CreateTextureView(const FRHITextureViewDesc&) override { return nullptr; }
    FRHIShader* CreateShaderFromFile(EShaderStage, const void*, uint64, const String&) override { return nullptr; }
    FRHIPipeline_Graphics* CreateGraphicsPipeline(const FRHIPipelineDesc_Graphics&) override { return nullptr; }
    FRHICommandAllocator* CreateCommandAllocator(EQueueType) override { return nullptr; }
    FRHICommandList* CreateCommandList(FRHICommandAllocator*) override { return nullptr; }
    FRHISemaphore* CreateBinarySemaphore() override { return nullptr; }
    FRHIFence* CreateFence(bool) override { return nullptr; }
    FRHISwapchain* CreateSwapchain(const FRHISwapchainDesc&) override { return nullptr; }
    FRHIBindGroup* CreateBindGroup(const FRHIBindGroupDesc&) override { return nullptr; }
    FRHIBindGroupLayout* CreateBindGroupLayout(const FRHIBindGroupLayoutDesc&) override { return nullptr; }
    FRHISampler* CreateSampler(const FRHISamplerDesc&) override { return nullptr; }
    void* GetBindlessHeapGPUDescriptor() const override { return nullptr; }
    uint32 AllocateBindlessIndex(FRHIResource*) override { return 0; }

    FUploadTrace Trace;
    unsigned Attempts = 0;
    unsigned FailCreate = 0;
    unsigned FailMap = 0;
};

class FProviderTestPipeline final : public FRHIPipeline_Graphics
{
public:
    const FRHIPipelineDesc_Graphics& GetDesc() const override { return Desc; }

private:
    FRHIPipelineDesc_Graphics Desc;
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

bool TestFailedUploads()
{
    FMeshAssetData Data = MakeMesh();
    FUploadTestDevice Device;
    FRenderAssetProvider Provider{&Device, nullptr};
    Device.FailCreate = 2;
    FMeshAsset* Mesh = FRenderAssetProvider::CreateMesh(&Provider, Data);
    const bool SecondFailed = Mesh == nullptr && Device.Trace.Created == 1 &&
        Device.Trace.Mapped == 1 && Device.Trace.Unmapped == 1 && Device.Trace.Destroyed == 1;
    FRenderAssetProvider::DestroyMesh(&Provider, Mesh);
    bool Passed = ExpectProvider(SecondFailed, "failed index buffer creation destroys the uploaded vertex buffer");

    Device.FailMap = Device.Attempts + 2;
    Mesh = FRenderAssetProvider::CreateMesh(&Provider, Data);
    const bool IndexMapFailed = Mesh == nullptr && Device.Trace.Created == 3 &&
        Device.Trace.Mapped == 3 && Device.Trace.Unmapped == 2 && Device.Trace.Destroyed == 3;
    FRenderAssetProvider::DestroyMesh(&Provider, Mesh);
    Passed &= ExpectProvider(IndexMapFailed, "failed index mapping destroys both buffers");

    const unsigned AttemptsBeforeInvalidData = Device.Attempts;
    Data.Indices[1] = 3;
    Mesh = FRenderAssetProvider::CreateMesh(&Provider, Data);
    Passed &= ExpectProvider(Mesh == nullptr && Device.Attempts == AttemptsBeforeInvalidData,
        "invalid mesh data is rejected before RHI allocation");
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
    if (!World.TryCreateEntity(Camera) ||
        World.TryAddTransform(Camera, CameraPose) != EComponentMutationResult::Added ||
        World.TryAddComponent(*Cameras, Camera, FWorldCameraComponent{}) != EComponentMutationResult::Added)
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
        if (!World.TryCreateEntity(Entity) ||
            World.TryAddTransform(Entity, Pose) != EComponentMutationResult::Added ||
            World.TryAddComponent(*Meshes, Entity, std::move(Component)) != EComponentMutationResult::Added)
        {
            return false;
        }
    }
    return true;
}

bool TestSharedMaterialAcrossExtents()
{
    FUploadTestDevice Device;
    FProviderTestPipeline Pipeline;
    FRenderAssetProvider Provider{&Device, &Pipeline};
    FMeshAssetData MeshData = MakeMesh();
    FMaterialAssetData MaterialData;
    MaterialData.BaseColor = Math::Vector3f(0.25f, 2.0f, 0.5f);
    FAssetId MeshId;
    FAssetId MaterialId;
    if (!FAssetId::TryParse("e8100000-0000-4000-8000-000000000001", MeshId) ||
        !FAssetId::TryParse("e8100000-0000-4000-8000-000000000002", MaterialId))
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
    if (!ExpectProvider(Registry.Register(MeshRegistration, MeshHandle) == EAssetRegisterResult::Success,
        "registry accepts the owned render mesh"))
    {
        FRenderAssetProvider::DestroyMesh(&Provider, Mesh);
        return false;
    }
    FMaterialAsset* const Material = FRenderAssetProvider::CreateMaterial(&Provider, MaterialData);
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
    if (!ExpectProvider(Registry.Register(MaterialRegistration, MaterialHandle) == EAssetRegisterResult::Success,
        "registry accepts the owned render material"))
    {
        FRenderAssetProvider::DestroyMaterial(&Provider, Material);
        return false;
    }

    const auto* const Vertices = static_cast<const FUploadTestBuffer*>(Mesh->VertexBuffer);
    const auto* const Indices = static_cast<const FUploadTestBuffer*>(Mesh->IndexBuffer);
    FBasePassVertex FirstVertex;
    std::memcpy(&FirstVertex, Vertices->Bytes, sizeof(FirstVertex));
    bool Passed = ExpectProvider(
        Mesh->IndexFormat == EPixelFormat::R32_UINT && Mesh->IndexCount == 3 && Mesh->VertexCount == 3 &&
        Vertices->GetDesc().Size == VertexBytes && Indices->GetDesc().Size == IndexBytes &&
        Vertices->GetDesc().UsageFlag == static_cast<uint32>(EBufferUsageFlags::VertexBuffer) &&
        Indices->GetDesc().UsageFlag == static_cast<uint32>(EBufferUsageFlags::IndexBuffer) &&
        FirstVertex.Position == MeshData.Positions[0] && FirstVertex.Color == Math::Vector3f(1.0f) &&
        std::memcmp(Indices->Bytes, MeshData.Indices.Data(), IndexBytes) == 0 &&
        Device.Trace.Mapped == 2 && Device.Trace.Unmapped == 2,
        "payload uploads white vertices and R32 indices with appropriate usages");

    {
        FWorld World;
        if (!ExpectProvider(AddWorldObjects(World, MeshHandle, MaterialHandle),
            "two entities share the same render mesh and material"))
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
            bool BothVisible = Status == EWorldRenderSnapshotBuildResult::Success &&
                Snapshot.GetRenderScene().Meshes.Size() == 2;
            if (BothVisible)
            {
                for (const FRenderMeshProxy& Proxy : Snapshot.GetRenderScene().Meshes)
                {
                    BothVisible &= Proxy.GraphicsPipeline == &Pipeline && Proxy.MaterialAssetId == MaterialId
                        && Proxy.VertexBuffer == Mesh->VertexBuffer && Proxy.IndexBuffer == Mesh->IndexBuffer
                        && Proxy.BaseColor == Math::Vector4f(0.25f, 2.0f, 0.5f, 1.0f);
                }
            }
            Passed &= ExpectProvider(BothVisible, "both entities resolve the same payload and pipeline after extent changes");
        }
    }

    Passed &= ExpectProvider(Registry.Unregister(MaterialHandle) == EAssetUnregisterResult::Success &&
        Registry.Unregister(MeshHandle) == EAssetUnregisterResult::Success && Device.Trace.Destroyed == 2,
        "registry releases provider-owned buffers and material before the borrowed pipeline");
    return Passed;
}

} // namespace

bool RunRenderAssetProviderTests()
{
    const bool Failures = TestFailedUploads();
    return TestSharedMaterialAcrossExtents() && Failures;
}

} // namespace LE
