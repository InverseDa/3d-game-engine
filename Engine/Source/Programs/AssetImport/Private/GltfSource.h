#pragma once

#include "AssetFormat/AssetFormat.h"
#include "Containers/String.h"
#include "Memory/UniquePtr.h"
#include "Math/Quaternion.h"

namespace LE::Detail
{

inline constexpr uint32 GltfNoIndex = 0xFFFFFFFFu;

struct FGltfDiagnostic
{
    const char* Code = "ok";
    const char* Message = "";
    String File;
    String Selector;
};

struct FGltfNode
{
    uint32 Index = GltfNoIndex;
    uint32 Parent = GltfNoIndex;
    uint32 Mesh = GltfNoIndex;
    String Name;
    Math::Vector3f Translation{};
    Math::Vector3f Scale{1.0f};
    Math::Quaternionf Rotation = Math::Quaternionf::Identity();
};

struct FGltfPrimitive
{
    uint32 Mesh = GltfNoIndex;
    uint32 Index = GltfNoIndex;
    uint32 Material = GltfNoIndex;
    String Name;
};

struct FGltfMaterial
{
    uint32 Index = GltfNoIndex;
    uint32 Image = GltfNoIndex;
    String Name;
};

struct FGltfImage
{
    uint32 Index = GltfNoIndex;
    String Name;
};

class FGltfSource
{
public:
    FGltfSource() noexcept;
    ~FGltfSource();

    bool Open(StringView AbsoluteRoot, StringView RelativeGltf, uint32 Scene, FGltfDiagnostic& Diagnostic) noexcept;
    Span<const uint8> DocumentBytes() const noexcept;
    Span<const FGltfNode> Nodes() const noexcept;
    Span<const FGltfPrimitive> Primitives() const noexcept;
    Span<const FGltfMaterial> Materials() const noexcept;
    Span<const FGltfImage> Images() const noexcept;
    Span<const FGltfDiagnostic> Warnings() const noexcept;

    bool DecodeMesh(uint32 Mesh, uint32 Primitive, FMeshAssetData& Out, FGltfDiagnostic& Diagnostic) const noexcept;
    bool DecodeMaterial(uint32 Material, FAssetId Texture, FMaterialAssetData& Out, FGltfDiagnostic& Diagnostic) const noexcept;
    bool DecodeTexture(uint32 Image, FCpuTextureAsset& Out, FGltfDiagnostic& Diagnostic) const noexcept;

private:
    struct FState;
    UniquePtr<FState> State;
};

} // namespace LE::Detail
