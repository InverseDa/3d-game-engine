#pragma once

#include "Renderer/RenderAssets.h"

namespace LE
{

class FRHIDevice;
class FRHIBindGroupLayout;
struct FMeshAssetData;
struct FMaterialAssetData;
struct FCpuTextureAsset;

struct FBasePassVertex
{
    Math::Vector3f Position;
    Math::Vector3f Color;
    Math::Vector2f UV;
    Math::Vector3f Normal;
};

static_assert(std::is_standard_layout<FBasePassVertex>::value && std::is_trivially_copyable<FBasePassVertex>::value);

enum class ETextureUploadResult : uint8
{
    Success,
    InvalidInput,
    StagingCreationFailed,
    StagingMappingFailed,
    TextureCreationFailed,
    CommandCreationFailed,
    RecordingFailed,
    SubmissionFailed,
    CompletionFailed,
    DeviceLost,
    ViewCreationFailed,
    CacheAllocationFailed,
    BindingCreationFailed,
};

RENDERER_API const char* DescribeTextureUploadResult(ETextureUploadResult Result) noexcept;
RENDERER_API FRHIBindGroupLayout* CreateBasePassMaterialLayout(FRHIDevice& Device) noexcept;

// Owns a scene's GPU texture cache and sampler. The device and layout
// outlive the provider. Drain submitted frames then release registry objects
// before shutting down the provider.
class RENDERER_API FRenderAssetProvider final : public FNonCopyable
{
public:
    FRenderAssetProvider() noexcept = default;
    ~FRenderAssetProvider() noexcept;

    bool Initialize(FRHIDevice* Device, FRHIBindGroupLayout* Layout) noexcept;
    void Shutdown() noexcept;
    ETextureUploadResult GetLastTextureUploadResult() const noexcept { return LastResult; }
    // Counts completed uploads for authored asset IDs during this session.
    std::size_t GetTextureUploadCount() const noexcept { return TextureUploads; }

    static FMeshAsset* CreateMesh(void* Context, const FMeshAssetData& Data) noexcept;
    static void DestroyMesh(void* Context, void* Object) noexcept;
    static FMaterialAsset* CreateMaterial(void* Context, const FMaterialAssetData& Data, const FCpuTextureAsset* Texture) noexcept;
    static void DestroyMaterial(void* Context, void* Object) noexcept;

private:
    struct FSession;
    FSession* Session = nullptr;
    ETextureUploadResult LastResult = ETextureUploadResult::Success;
    std::size_t TextureUploads = 0;
};

} // namespace LE
