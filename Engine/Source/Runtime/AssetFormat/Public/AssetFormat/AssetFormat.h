#pragma once

#include "Asset/RenderAssetTypes.h"
#include "Asset/TextureAssetTypes.h"
#include "Containers/Array.h"
#include "Containers/Span.h"
#include "Math/Vector.h"

namespace LE
{

inline constexpr uint32 AssetFileVersion = 1;
inline constexpr std::size_t MaxAssetFileBytes = 128 * 1024 * 1024;
inline constexpr std::size_t MaxAssetPathBytes = 1024;
inline constexpr std::size_t MaxAssetFileHeaderBytes = 48 + MaxAssetPathBytes;
inline constexpr uint32 MaxAssetMeshVertices = 1'000'000;
inline constexpr uint32 MaxAssetMeshIndices = 3'000'000;
inline constexpr uint32 MaxAssetTextureDimension = 4096;

struct FAssetFileHeaderView
{
    FAssetId Id;
    FAssetTypeId TypeId;
    StringView Path;
    std::size_t PayloadOffset = 0;
    std::size_t PayloadSize = 0;
};

struct FMeshAssetData
{
    explicit FMeshAssetData(IAllocator& Allocator = GetDefaultAllocator()) noexcept
        : Positions(Allocator), Indices(Allocator) {}

    Array<Math::Vector3f> Positions;
    Array<uint32> Indices;
    Math::Vector3f BoundsMin;
    Math::Vector3f BoundsMax;
};

struct FMaterialAssetData
{
    Math::Vector3f BaseColor{1.0f};
};

struct FCpuTextureAsset
{
    explicit FCpuTextureAsset(IAllocator& Allocator = GetDefaultAllocator()) noexcept
        : Pixels(Allocator) {}

    uint32 Width = 0;
    uint32 Height = 0;
    Array<uint8> Pixels;
};

enum class EAssetFormatResult : uint8
{
    Success = 0,
    InvalidHeader,
    UnsupportedVersion,
    InvalidIdentity,
    InvalidPath,
    TypeMismatch,
    LimitExceeded,
    InvalidPayload,
    AllocationFailed,
};

// Header-only callers provide up to MaxAssetFileHeaderBytes from the start of
// the file and its full byte length. Path borrows that prefix on success.
// All codec operations preserve their outputs on failure.
ASSETFORMAT_API EAssetFormatResult ReadAssetFileHeader(
    Span<const uint8> Prefix, std::size_t FileSize, FAssetFileHeaderView& Out,
    IAllocator& Allocator = GetDefaultAllocator()) noexcept;

inline EAssetFormatResult ReadAssetFileHeader(
    const Span<const uint8> Bytes, FAssetFileHeaderView& Out,
    IAllocator& Allocator = GetDefaultAllocator()) noexcept
{
    return ReadAssetFileHeader(Bytes, Bytes.Size(), Out, Allocator);
}

ASSETFORMAT_API EAssetFormatResult EncodeAssetFile(
    FAssetId Id, StringView Path, const FMeshAssetData& Mesh, Array<uint8>& Out) noexcept;
ASSETFORMAT_API EAssetFormatResult EncodeAssetFile(
    FAssetId Id, StringView Path, const FMaterialAssetData& Material, Array<uint8>& Out) noexcept;
ASSETFORMAT_API EAssetFormatResult EncodeAssetFile(
    FAssetId Id, StringView Path, const FCpuTextureAsset& Texture, Array<uint8>& Out) noexcept;

ASSETFORMAT_API EAssetFormatResult DecodeAssetFile(
    Span<const uint8> Bytes, FMeshAssetData& Out) noexcept;
ASSETFORMAT_API EAssetFormatResult DecodeAssetFile(
    Span<const uint8> Bytes, FMaterialAssetData& Out) noexcept;
ASSETFORMAT_API EAssetFormatResult DecodeAssetFile(
    Span<const uint8> Bytes, FCpuTextureAsset& Out) noexcept;

} // namespace LE
