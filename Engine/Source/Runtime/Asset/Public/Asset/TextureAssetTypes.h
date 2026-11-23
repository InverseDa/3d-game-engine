#pragma once

#include "Asset/AssetTypes.h"

namespace LE
{

// CPU texture payload is defined by AssetFormat. This type's identity is
// available to registry consumers independently of its storage.
struct FCpuTextureAsset;

ASSET_API FAssetTypeId GetCpuTextureAssetTypeId() noexcept;

template <> struct TAssetTypeTraits<FCpuTextureAsset>
{
    static FAssetTypeId GetTypeId() noexcept { return GetCpuTextureAssetTypeId(); }
};

using FCpuTextureAssetHandle = TAssetHandle<FCpuTextureAsset>;

} // namespace LE
