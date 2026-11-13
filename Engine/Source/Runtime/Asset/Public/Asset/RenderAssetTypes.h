#pragma once

#include "Asset/AssetTypes.h"

#include <type_traits>

namespace LE
{

// Logical render-asset types are declared in Asset so World can retain typed
// handles without depending on Renderer or RHI. Renderer owns their complete
// GPU-facing payload definitions.
struct FMeshAsset;
struct FMaterialAsset;

ASSET_API FAssetTypeId GetMeshAssetTypeId() noexcept;
ASSET_API FAssetTypeId GetMaterialAssetTypeId() noexcept;

template <>
struct TAssetTypeTraits<FMeshAsset>
{
    static FAssetTypeId GetTypeId() noexcept { return GetMeshAssetTypeId(); }
};

template <>
struct TAssetTypeTraits<FMaterialAsset>
{
    static FAssetTypeId GetTypeId() noexcept { return GetMaterialAssetTypeId(); }
};

using FMeshAssetHandle = TAssetHandle<FMeshAsset>;
using FMaterialAssetHandle = TAssetHandle<FMaterialAsset>;

static_assert(sizeof(FMeshAssetHandle) == 16 && sizeof(FMaterialAssetHandle) == 16,
    "render asset handles must remain compact 16-byte runtime identities");
static_assert(std::is_trivially_copyable<FMeshAssetHandle>::value &&
        std::is_trivially_copyable<FMaterialAssetHandle>::value,
    "render asset handles must remain trivially copyable values");

} // namespace LE
