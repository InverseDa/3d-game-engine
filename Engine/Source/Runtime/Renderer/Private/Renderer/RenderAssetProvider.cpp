#include "Renderer/RenderAssetProvider.h"

#include "AssetFormat/AssetFormat.h"
#include "Memory/UniquePtr.h"
#include "RHI/RHIBindGroup.h"
#include "RHI/RHIBuffer.h"
#include "RHI/RHICommandAllocator.h"
#include "RHI/RHICommandList.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHISampler.h"
#include "RHI/RHITexture.h"

#include <cmath>
#include <cstring>
#include <new>
#include <utility>

namespace LE
{
namespace
{

template <typename Resource>
UniquePtr<Resource> OwnRHI(Resource* Value) noexcept
{
    return UniquePtr<Resource>::Adopt(Value, nullptr, [](void*, Resource* Object) noexcept
    {
        RHI::DestroyResource(Object);
    });
}

UniquePtr<FRHIBuffer> CreateUploadBuffer(
    FRHIDevice& Device,
    const char* Name,
    const uint64 Size,
    const EBufferUsageFlags Usage) noexcept
{
    FRHIBufferDesc Desc;
    if (!Desc.Name.TryAssign(Name))
    {
        return {};
    }
    Desc.Size = Size;
    Desc.Usage = static_cast<uint32>(EResourceUsage::Upload);
    Desc.UsageFlag = static_cast<uint32>(Usage);
    return OwnRHI(Device.CreateBuffer(Desc));
}

bool ValidateMesh(const FMeshAssetData& Data, const FRenderBounds& Bounds) noexcept
{
    if (Data.Positions.IsEmpty() || Data.Positions.Size() > MaxAssetMeshVertices ||
        Data.Indices.IsEmpty() || Data.Indices.Size() > MaxAssetMeshIndices ||
        Data.Indices.Size() % 3 != 0 || !Bounds.IsValid() ||
        (!Data.UVs.IsEmpty() && Data.UVs.Size() != Data.Positions.Size()))
    {
        return false;
    }
    for (const Math::Vector2f& UV : Data.UVs)
    {
        if (!std::isfinite(UV.Values[0]) || !std::isfinite(UV.Values[1]))
        {
            return false;
        }
    }
    for (const Math::Vector3f& Position : Data.Positions)
    {
        for (std::size_t Axis = 0; Axis < Math::Vector3f::Dimension; ++Axis)
        {
            const float Value = Position.Values[Axis];
            if (!std::isfinite(Value) || Value < Bounds.Min.Values[Axis] ||
                Value > Bounds.Max.Values[Axis])
            {
                return false;
            }
        }
    }
    for (const uint32 Index : Data.Indices)
    {
        if (Index >= Data.Positions.Size())
        {
            return false;
        }
    }
    return true;
}

struct FOwnedRenderMesh final : FMeshAsset
{
    UniquePtr<FRHIBuffer> OwnedVertices;
    UniquePtr<FRHIBuffer> OwnedIndices;
};

struct FTextureResources
{
    UniquePtr<FRHITexture> Image;
    UniquePtr<FRHITextureView> View;
    std::size_t References = 0;
};

// Member destruction frees the list before its allocator and the image
// after the list. Commands recorded but never submitted need no wait.
struct FTextureSubmission
{
    UniquePtr<FRHITexture> Image;
    UniquePtr<FRHIBuffer> Staging;
    UniquePtr<FRHICommandAllocator> Allocator;
    UniquePtr<FRHICommandList> List;
    UniquePtr<FRHIFence> Fence;
};

struct FOwnedRenderMaterial final : FMaterialAsset
{
    FAssetId TextureId;
    UniquePtr<FRHIBindGroup> Binding;
};

constexpr uint32 MaterialSet = 0;
constexpr uint32 ImageSlot = 0;
constexpr uint32 SamplerSlot = 1;

bool HasMaterialLayout(const FRHIBindGroupLayout& Layout) noexcept
{
    const auto& Desc = Layout.GetDesc();
    if (Desc.SetIndex != MaterialSet || Desc.Bindings.Size() != 2)
    {
        return false;
    }
    const auto& Image = Desc.Bindings[0];
    const auto& Sampler = Desc.Bindings[1];
    return Image.Binding == ImageSlot && Image.Type == ERHIBindGroupItemType::SampledImage &&
        Image.Count == 1 && Image.StageFlags == EShaderStage::Pixel &&
        Sampler.Binding == SamplerSlot && Sampler.Type == ERHIBindGroupItemType::Sampler &&
        Sampler.Count == 1 && Sampler.StageFlags == EShaderStage::Pixel;
}

} // namespace

struct FRenderAssetProvider::FSession
{
    FSession(FRHIDevice& InDevice, FRHIPipeline_Graphics& InPipeline,
        FRHIBindGroupLayout& InLayout) noexcept
        : Device(InDevice), Pipeline(InPipeline), Layout(InLayout)
    {
    }

    ETextureUploadResult Upload(uint32 Width, uint32 Height, Span<const uint8> Pixels,
        FTextureResources& Out) noexcept
    {
        if (bDeviceLost)
        {
            return ETextureUploadResult::DeviceLost;
        }
        if (Pending.Fence)
        {
            // An earlier wait failed after submission. Once the fence can be
            // confirmed, discard the failed candidate and allow a retry.
            const auto Completion = Pending.Fence->WaitChecked(0);
            if (Completion == ERHIFenceWaitResult::DeviceLost)
            {
                bDeviceLost = true;
                return ETextureUploadResult::DeviceLost;
            }
            if (Completion != ERHIFenceWaitResult::Success)
            {
                return ETextureUploadResult::CompletionFailed;
            }
            RetirePending();
        }
        if (Width == 0 || Height == 0 ||
            Width > MaxAssetTextureDimension || Height > MaxAssetTextureDimension ||
            Pixels.Size() != static_cast<std::size_t>(Width) * Height * 4)
        {
            return ETextureUploadResult::InvalidInput;
        }
        FTextureSubmission Candidate;
        Candidate.Staging = CreateUploadBuffer(
            Device, "AssetTextureStaging", Pixels.Size(), EBufferUsageFlags::TransferSrc);
        if (!Candidate.Staging)
        {
            return ETextureUploadResult::StagingCreationFailed;
        }
        void* const Mapping = Candidate.Staging->Map(0, Pixels.Size());
        if (Mapping == nullptr)
        {
            return ETextureUploadResult::StagingMappingFailed;
        }
        std::memcpy(Mapping, Pixels.Data(), Pixels.Size());
        Candidate.Staging->Unmap();

        FRHITextureDesc Desc;
        if (!Desc.Name.TryAssign("AssetBaseColorTexture"))
        {
            return ETextureUploadResult::TextureCreationFailed;
        }
        Desc.Width = Width;
        Desc.Height = Height;
        Desc.Format = EPixelFormat::R8G8B8A8_SRGB;
        Desc.bIsTransferDestination = true;
        Desc.bIsShaderResource = true;
        Candidate.Image = OwnRHI(Device.CreateTexture(Desc));
        if (!Candidate.Image)
        {
            return ETextureUploadResult::TextureCreationFailed;
        }
        FRHIQueue* const Queue = Device.GetGraphicsQueue();
        if (Queue == nullptr)
        {
            return ETextureUploadResult::CommandCreationFailed;
        }
        Candidate.Allocator = OwnRHI(Device.CreateCommandAllocator(EQueueType::Graphics));
        if (!Candidate.Allocator)
        {
            return ETextureUploadResult::CommandCreationFailed;
        }
        Candidate.List = OwnRHI(Device.CreateCommandList(Candidate.Allocator.Get()));
        Candidate.Fence = OwnRHI(Device.CreateFence(false));
        if (!Candidate.List || !Candidate.Fence)
        {
            return ETextureUploadResult::CommandCreationFailed;
        }
        FRHITexture* const Image = Candidate.Image.Get();
        if (!Candidate.List->BeginChecked() ||
            !Candidate.List->TextureUploadBarrier(
                Image, ERHIResourceState::Undefined, ERHIResourceState::CopyDestination) ||
            !Candidate.List->CopyBufferToTexture2D(Candidate.Staging.Get(), Image) ||
            !Candidate.List->TextureUploadBarrier(
                Image, ERHIResourceState::CopyDestination, ERHIResourceState::ShaderResource) ||
            !Candidate.List->EndChecked())
        {
            return ETextureUploadResult::RecordingFailed;
        }

        FRHISubmitInfo Submit;
        Submit.CmdList = Candidate.List.Get();
        Submit.FenceToSignal = Candidate.Fence.Get();
        const auto SubmissionResult = Queue->Submit(Submit);
        if (SubmissionResult == ERHIQueueSubmitResult::DeviceLost)
        {
            bDeviceLost = true;
            return ETextureUploadResult::DeviceLost;
        }
        if (SubmissionResult != ERHIQueueSubmitResult::Success)
        {
            return ETextureUploadResult::SubmissionFailed;
        }
        const auto Completion = Candidate.Fence->WaitChecked();
        if (Completion == ERHIFenceWaitResult::DeviceLost)
        {
            bDeviceLost = true;
            return ETextureUploadResult::DeviceLost;
        }
        if (Completion != ERHIFenceWaitResult::Success)
        {
            Pending = std::move(Candidate);
            return ETextureUploadResult::CompletionFailed;
        }

        FRHITextureViewDesc ViewDesc;
        ViewDesc.Texture = Image;
        ViewDesc.Format = Desc.Format;
        auto View = OwnRHI(Device.CreateTextureView(ViewDesc));
        if (!View)
        {
            return ETextureUploadResult::ViewCreationFailed;
        }
        Out.Image = std::move(Candidate.Image);
        Out.View = std::move(View);
        return ETextureUploadResult::Success;
    }

    void FinishPending() noexcept
    {
        if (Pending.Fence && !bDeviceLost)
        {
            const auto Wait = Pending.Fence->WaitChecked();
            if (Wait != ERHIFenceWaitResult::Success && Wait != ERHIFenceWaitResult::DeviceLost)
            {
                const auto Drain = Device.GetGraphicsQueue()->WaitIdleChecked();
                if (Drain != ERHIFenceWaitResult::Success && Drain != ERHIFenceWaitResult::DeviceLost)
                {
                    HandleContractViolation("could not safely retire a submitted texture upload", __FILE__, __LINE__);
                }
            }
        }
        RetirePending();
    }

    void RetirePending() noexcept
    {
        FTextureSubmission Completed = std::move(Pending);
    }

    void Release(const FAssetId Id) noexcept
    {
        FTextureResources* const Resources = Textures.Find(Id);
        if (Resources == nullptr || Resources->References == 0)
        {
            HandleContractViolation("texture cache reference underflow", __FILE__, __LINE__);
        }
        if (--Resources->References == 0)
        {
            Textures.Erase(Id);
        }
    }

    FRHIDevice& Device;
    FRHIPipeline_Graphics& Pipeline;
    FRHIBindGroupLayout& Layout;
    UniquePtr<FRHISampler> Sampler;
    FTextureResources White;
    HashMap<FAssetId, FTextureResources> Textures;
    FTextureSubmission Pending;
    std::size_t Materials = 0;
    bool bDeviceLost = false;
};

const char* DescribeTextureUploadResult(ETextureUploadResult Result) noexcept
{
    switch (Result)
    {
    case ETextureUploadResult::Success: return "success";
    case ETextureUploadResult::InvalidInput: return "invalid texture or material input";
    case ETextureUploadResult::StagingCreationFailed: return "create staging buffer";
    case ETextureUploadResult::StagingMappingFailed: return "map staging buffer";
    case ETextureUploadResult::TextureCreationFailed: return "create texture image";
    case ETextureUploadResult::CommandCreationFailed: return "create upload commands or fence";
    case ETextureUploadResult::RecordingFailed: return "record upload";
    case ETextureUploadResult::SubmissionFailed: return "submit upload";
    case ETextureUploadResult::CompletionFailed: return "confirm upload fence";
    case ETextureUploadResult::DeviceLost: return "graphics device lost";
    case ETextureUploadResult::ViewCreationFailed: return "create uploaded texture view";
    case ETextureUploadResult::CacheAllocationFailed: return "allocate material or texture cache";
    case ETextureUploadResult::BindingCreationFailed: return "create sampler or material binding";
    }
    return "unknown texture upload result";
}

FRHIBindGroupLayout* CreateBasePassMaterialLayout(FRHIDevice& Device) noexcept
{
    FRHIBindGroupLayoutDesc Desc;
    Desc.SetIndex = MaterialSet;
    if (!Desc.Name.TryAssign("BasePassMaterialLayout") ||
        !Desc.Bindings.TryPushBack({ImageSlot, 1, ERHIBindGroupItemType::SampledImage, EShaderStage::Pixel}) ||
        !Desc.Bindings.TryPushBack({SamplerSlot, 1, ERHIBindGroupItemType::Sampler, EShaderStage::Pixel}))
    {
        return nullptr;
    }
    return Device.CreateBindGroupLayout(Desc);
}

FRenderAssetProvider::~FRenderAssetProvider() noexcept
{
    Shutdown();
}

bool FRenderAssetProvider::Initialize(FRHIDevice* Device, FRHIPipeline_Graphics* Pipeline,
    FRHIBindGroupLayout* Layout) noexcept
{
    if (Session != nullptr || Device == nullptr || Pipeline == nullptr || Layout == nullptr ||
        !HasMaterialLayout(*Layout) || Pipeline->GetDesc().BindGroupLayouts.Size() != 1 ||
        Pipeline->GetDesc().BindGroupLayouts[0] != Layout)
    {
        LastResult = ETextureUploadResult::InvalidInput;
        return false;
    }
    FRHISamplerDesc SamplerDesc;
    SamplerDesc.MinFilter = ESamplerFilter::Nearest;
    SamplerDesc.MagFilter = ESamplerFilter::Nearest;
    SamplerDesc.MipmapMode = ESamplerMipmapMode::Nearest;
    SamplerDesc.AddressU = ESamplerAddressMode::ClampToEdge;
    SamplerDesc.AddressV = ESamplerAddressMode::ClampToEdge;
    SamplerDesc.AddressW = ESamplerAddressMode::ClampToEdge;
    SamplerDesc.MaxLOD = 0.0f;
    if (!SamplerDesc.Name.TryAssign("BasePassMaterialSampler"))
    {
        LastResult = ETextureUploadResult::BindingCreationFailed;
        return false;
    }
    auto Sampler = OwnRHI(Device->CreateSampler(SamplerDesc));
    if (!Sampler)
    {
        LastResult = ETextureUploadResult::BindingCreationFailed;
        return false;
    }
    Session = new (std::nothrow) FSession(*Device, *Pipeline, *Layout);
    if (Session == nullptr)
    {
        LastResult = ETextureUploadResult::CacheAllocationFailed;
        return false;
    }
    Session->Sampler = std::move(Sampler);
    LastResult = ETextureUploadResult::Success;
    TextureUploads = 0;
    return true;
}

void FRenderAssetProvider::Shutdown() noexcept
{
    if (Session != nullptr)
    {
        if (Session->Materials != 0)
        {
            HandleContractViolation("destroy material assets before their render provider", __FILE__, __LINE__);
        }
        Session->FinishPending();
        delete Session;
        Session = nullptr;
    }
}

FMeshAsset* FRenderAssetProvider::CreateMesh(void* Context, const FMeshAssetData& Data) noexcept
{
    const auto* const Provider = static_cast<const FRenderAssetProvider*>(Context);
    const FRenderBounds Bounds(Data.BoundsMin, Data.BoundsMax);
    if (Provider == nullptr || Provider->Session == nullptr || !ValidateMesh(Data, Bounds))
    {
        return nullptr;
    }

    FRHIDevice& Device = Provider->Session->Device;
    const uint64 VertexBytes = Data.Positions.Size() * sizeof(FBasePassVertex);
    UniquePtr<FRHIBuffer> Vertices = CreateUploadBuffer(
        Device, "AssetMeshVertices", VertexBytes, EBufferUsageFlags::VertexBuffer);
    if (!Vertices)
    {
        return nullptr;
    }
    void* const VertexMapping = Vertices->Map(0, VertexBytes);
    if (VertexMapping == nullptr)
    {
        return nullptr;
    }
    auto* VertexDestination = static_cast<uint8*>(VertexMapping);
    const bool bHasUV = !Data.UVs.IsEmpty();
    for (std::size_t Index = 0; Index < Data.Positions.Size(); ++Index)
    {
        const FBasePassVertex Vertex{Data.Positions[Index], Math::Vector3f(1.0f),
            bHasUV ? Data.UVs[Index] : Math::Vector2f{}};
        std::memcpy(VertexDestination, &Vertex, sizeof(Vertex));
        VertexDestination += sizeof(Vertex);
    }
    Vertices->Unmap();

    const uint64 IndexBytes = Data.Indices.Size() * sizeof(uint32);
    UniquePtr<FRHIBuffer> Indices = CreateUploadBuffer(
        Device, "AssetMeshIndices", IndexBytes, EBufferUsageFlags::IndexBuffer);
    if (!Indices)
    {
        return nullptr;
    }
    void* const IndexMapping = Indices->Map(0, IndexBytes);
    if (IndexMapping == nullptr)
    {
        return nullptr;
    }
    std::memcpy(IndexMapping, Data.Indices.Data(), static_cast<std::size_t>(IndexBytes));
    Indices->Unmap();

    auto* const Mesh = new (std::nothrow) FOwnedRenderMesh;
    if (Mesh == nullptr)
    {
        return nullptr;
    }
    Mesh->VertexBuffer = Vertices.Get();
    Mesh->VertexCount = static_cast<uint32>(Data.Positions.Size());
    Mesh->IndexBuffer = Indices.Get();
    Mesh->IndexCount = static_cast<uint32>(Data.Indices.Size());
    Mesh->IndexFormat = EPixelFormat::R32_UINT;
    Mesh->LocalBounds = Bounds;
    Mesh->bHasUV = bHasUV;
    Mesh->OwnedVertices = std::move(Vertices);
    Mesh->OwnedIndices = std::move(Indices);
    return Mesh;
}

void FRenderAssetProvider::DestroyMesh(void*, void* Object) noexcept
{
    delete static_cast<FOwnedRenderMesh*>(static_cast<FMeshAsset*>(Object));
}

FMaterialAsset* FRenderAssetProvider::CreateMaterial(
    void* Context, const FMaterialAssetData& Data, const FCpuTextureAsset* Texture) noexcept
{
    auto* const Provider = static_cast<FRenderAssetProvider*>(Context);
    if (Provider == nullptr)
    {
        return nullptr;
    }
    Provider->LastResult = ETextureUploadResult::InvalidInput;
    const bool bTextured = Data.BaseColorTexture.IsValid();
    if (Provider->Session == nullptr || bTextured != (Texture != nullptr))
    {
        return nullptr;
    }
    for (const float Component : Data.BaseColor.Values)
    {
        if (!std::isfinite(Component) || Component < 0.0f)
        {
            return nullptr;
        }
    }
    FSession& Session = *Provider->Session;
    if (Session.bDeviceLost)
    {
        Provider->LastResult = ETextureUploadResult::DeviceLost;
        return nullptr;
    }
    FTextureResources* Resources = &Session.White;
    if (bTextured)
    {
        Resources = Session.Textures.Find(Data.BaseColorTexture);
        if (Resources == nullptr)
        {
            FTextureResources Candidate;
            Provider->LastResult = Session.Upload(Texture->Width, Texture->Height, Texture->Pixels.AsSpan(), Candidate);
            if (Provider->LastResult != ETextureUploadResult::Success)
            {
                return nullptr;
            }
            ++Provider->TextureUploads;
            if (Session.Textures.TryInsert(Data.BaseColorTexture, std::move(Candidate)) != EHashInsertResult::Inserted)
            {
                Provider->LastResult = ETextureUploadResult::CacheAllocationFailed;
                return nullptr;
            }
            Resources = Session.Textures.Find(Data.BaseColorTexture);
        }
        ++Resources->References;
    }
    else if (!Resources->Image)
    {
        constexpr uint8 White[] = {255, 255, 255, 255};
        Provider->LastResult = Session.Upload(1, 1, {White, sizeof(White)}, *Resources);
        if (Provider->LastResult != ETextureUploadResult::Success)
        {
            return nullptr;
        }
    }

    FRHIBindGroupDesc BindDesc;
    BindDesc.Layout = &Session.Layout;
    FRHIBindGroupItem ImageItem;
    ImageItem.Binding = ImageSlot;
    ImageItem.TextureView = Resources->View.Get();
    FRHIBindGroupItem SamplerItem;
    SamplerItem.Binding = SamplerSlot;
    SamplerItem.Sampler = Session.Sampler.Get();
    UniquePtr<FRHIBindGroup> Binding;
    if (BindDesc.Name.TryAssign("BasePassMaterial") && BindDesc.Items.TryPushBack(ImageItem) &&
        BindDesc.Items.TryPushBack(SamplerItem))
    {
        Binding = OwnRHI(Session.Device.CreateBindGroup(BindDesc));
    }
    if (!Binding)
    {
        if (bTextured)
        {
            Session.Release(Data.BaseColorTexture);
        }
        Provider->LastResult = ETextureUploadResult::BindingCreationFailed;
        return nullptr;
    }
    auto* const Material = new (std::nothrow) FOwnedRenderMaterial;
    if (Material == nullptr)
    {
        Binding.Reset();
        if (bTextured)
        {
            Session.Release(Data.BaseColorTexture);
        }
        Provider->LastResult = ETextureUploadResult::CacheAllocationFailed;
        return nullptr;
    }

    Material->Binding = std::move(Binding);
    Material->GraphicsPipeline = &Session.Pipeline;
    Material->PassMask = ERenderMeshPassMask::SceneColor;
    Material->BaseColor = Math::Vector4f(Data.BaseColor.Values[0], Data.BaseColor.Values[1], Data.BaseColor.Values[2], 1.0f);
    Material->BaseColorTexture = Resources->Image.Get();
    Material->MaterialBindGroup = Material->Binding.Get();
    Material->bRequiresUV = bTextured;
    Material->TextureId = Data.BaseColorTexture;
    ++Session.Materials;
    Provider->LastResult = ETextureUploadResult::Success;
    return Material;
}

void FRenderAssetProvider::DestroyMaterial(void* Context, void* Object) noexcept
{
    auto* Material = static_cast<FOwnedRenderMaterial*>(static_cast<FMaterialAsset*>(Object));
    if (Material == nullptr)
    {
        return;
    }
    auto& Session = *static_cast<FRenderAssetProvider*>(Context)->Session;
    const FAssetId TextureId = Material->TextureId;
    delete Material;
    if (TextureId.IsValid())
    {
        Session.Release(TextureId);
    }
    --Session.Materials;
}

} // namespace LE
