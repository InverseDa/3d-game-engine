#pragma once

#include "Renderer/RenderAssets.h"

namespace LE
{

class FRHIDevice;
struct FMeshAssetData;
struct FMaterialAssetData;

struct FBasePassVertex
{
    Math::Vector3f Position;
    Math::Vector3f Color;
};

static_assert(std::is_standard_layout<FBasePassVertex>::value
    && std::is_trivially_copyable<FBasePassVertex>::value);

// The application supplies a device and the shared opaque BasePass pipeline.
// It must keep both alive until all assets and frames using them are retired.
struct RENDERER_API FRenderAssetProvider
{
    FRHIDevice* Device = nullptr;
    FRHIPipeline_Graphics* BasePassPipeline = nullptr;

    // These callbacks accept decoded CPU data and match the AssetLoading
    // provider signature. The mesh owns its buffers. The material borrows
    // the pipeline. Each payload must be returned through its destroy callback.
    static FMeshAsset* CreateMesh(void* Context, const FMeshAssetData& Data) noexcept;
    static void DestroyMesh(void* Context, void* Object) noexcept;
    static FMaterialAsset* CreateMaterial(void* Context, const FMaterialAssetData& Data) noexcept;
    static void DestroyMaterial(void* Context, void* Object) noexcept;
};

} // namespace LE
