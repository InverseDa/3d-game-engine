#include "Renderer/RenderAssetProvider.h"

#include "AssetFormat/AssetFormat.h"
#include "Memory/UniquePtr.h"
#include "RHI/RHIBuffer.h"
#include "RHI/RHIDevice.h"

#include <cmath>
#include <cstring>
#include <new>
#include <utility>

namespace LE
{
namespace
{

void DestroyBuffer(void*, FRHIBuffer* Buffer) noexcept
{
    RHI::DestroyResource(Buffer);
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
    return UniquePtr<FRHIBuffer>::Adopt(Device.CreateBuffer(Desc), nullptr, &DestroyBuffer);
}

bool ValidateMesh(const FMeshAssetData& Data, const FRenderBounds& Bounds) noexcept
{
    if (Data.Positions.IsEmpty() || Data.Positions.Size() > MaxAssetMeshVertices ||
        Data.Indices.IsEmpty() || Data.Indices.Size() > MaxAssetMeshIndices ||
        Data.Indices.Size() % 3 != 0 || !Bounds.IsValid())
    {
        return false;
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

} // namespace

FMeshAsset* FRenderAssetProvider::CreateMesh(void* Context, const FMeshAssetData& Data) noexcept
{
    const auto* const Provider = static_cast<const FRenderAssetProvider*>(Context);
    const FRenderBounds Bounds(Data.BoundsMin, Data.BoundsMax);
    if (Provider == nullptr || Provider->Device == nullptr || !ValidateMesh(Data, Bounds))
    {
        return nullptr;
    }

    FRHIDevice& Device = *Provider->Device;
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
    for (const Math::Vector3f& Position : Data.Positions)
    {
        const FBasePassVertex Vertex{Position, Math::Vector3f(1.0f)};
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
    Mesh->OwnedVertices = std::move(Vertices);
    Mesh->OwnedIndices = std::move(Indices);
    return Mesh;
}

void FRenderAssetProvider::DestroyMesh(void*, void* Object) noexcept
{
    delete static_cast<FOwnedRenderMesh*>(static_cast<FMeshAsset*>(Object));
}

FMaterialAsset* FRenderAssetProvider::CreateMaterial(
    void* Context, const FMaterialAssetData& Data) noexcept
{
    const auto* const Provider = static_cast<const FRenderAssetProvider*>(Context);
    if (Provider == nullptr || Provider->BasePassPipeline == nullptr)
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
    return new (std::nothrow) FMaterialAsset{
        Provider->BasePassPipeline,
        ERenderMeshPassMask::SceneColor,
        Math::Vector4f(Data.BaseColor.Values[0], Data.BaseColor.Values[1], Data.BaseColor.Values[2], 1.0f),
    };
}

void FRenderAssetProvider::DestroyMaterial(void*, void* Object) noexcept
{
    delete static_cast<FMaterialAsset*>(Object);
}

} // namespace LE
