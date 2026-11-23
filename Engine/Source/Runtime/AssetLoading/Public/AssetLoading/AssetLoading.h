#pragma once

#include "Asset/AssetRegistry.h"
#include "AssetFormat/AssetFormat.h"
#include "FileSystem/FileSystem.h"
#include "Memory/UniquePtr.h"

namespace LE
{

enum class EAssetLoadResult : uint8
{
    Success,
    InvalidInput,
    NotFound,
    TypeMismatch,
    InvalidMount,
    DuplicateId,
    DuplicatePath,
    IdentityMismatch,
    FileError,
    FormatError,
    ProviderUnavailable,
    ProviderFailed,
    RegistrationFailed,
    AllocationFailed,
    ReentrantLoad,
};

// The diagnostic copies the logical path when available. Error details remain
// usable even if allocating that copy fails.
struct FAssetLoadDiagnostic
{
    explicit FAssetLoadDiagnostic(IAllocator& Allocator = GetDefaultAllocator()) noexcept : Path(Allocator) {}

    EAssetLoadResult Result = EAssetLoadResult::Success;
    EFileSystemResult FileResult = EFileSystemResult::Success;
    EAssetFormatResult FormatResult = EAssetFormatResult::Success;
    EAssetRegisterResult RegistrationResult = EAssetRegisterResult::Success;
    FAssetId Id;
    FAssetId DependencyId;
    String Path;
};

struct FAssetMount
{
    StringView Name;
    StringView AbsoluteRoot;
};

struct FAssetCatalogEntry
{
    FAssetId Id;
    FAssetTypeId Type;
    StringView Path;
};

// Entries and views are borrowed until the next successful Rebuild or destruction.
class ASSETLOADING_API FAssetCatalog final : public FNonCopyable
{
public:
    explicit FAssetCatalog(IAllocator& Allocator = GetDefaultAllocator()) noexcept;
    ~FAssetCatalog() noexcept;

    EAssetLoadResult Rebuild(Span<const FAssetMount> Mounts, FAssetLoadDiagnostic* Diagnostic = nullptr) noexcept;
    EAssetLoadResult Find(FAssetId Id, FAssetCatalogEntry& Out) const noexcept;
    EAssetLoadResult Find(StringView Path, FAssetCatalogEntry& Out) const noexcept;
    EAssetLoadResult Decode(FAssetId Id, FMeshAssetData& Out, FAssetLoadDiagnostic* Diagnostic = nullptr) const noexcept;
    EAssetLoadResult Decode(FAssetId Id, FMaterialAssetData& Out, FAssetLoadDiagnostic* Diagnostic = nullptr) const noexcept;
    EAssetLoadResult Decode(FAssetId Id, FCpuTextureAsset& Out, FAssetLoadDiagnostic* Diagnostic = nullptr) const noexcept;
    std::size_t GetEntryCount() const noexcept;

private:
    friend class FAssetLoadSession;
    struct FIndex;
    EAssetLoadResult Read(FAssetId Id, Array<uint8>& Out, FAssetLoadDiagnostic* Diagnostic) const noexcept;
    template <typename Asset, typename Data> EAssetLoadResult DecodeTyped(FAssetId Id, Data& Out, FAssetLoadDiagnostic* Diagnostic) const noexcept;

    IAllocator& Allocator;
    UniquePtr<FIndex> Index;
};

template <typename Asset, typename Data> struct TAssetLoadProvider
{
    void* Context = nullptr;
    Asset* (*Create)(void*, const Data&) noexcept = nullptr;
    FAssetDestroyThunk Destroy = nullptr;
};

template <> struct TAssetLoadProvider<FMaterialAsset, FMaterialAssetData>
{
    void* Context = nullptr;
    // Pixels are borrowed for this callback only, and null for a solid material.
    FMaterialAsset* (*Create)(void*, const FMaterialAssetData&, const FCpuTextureAsset*) noexcept = nullptr;
    FAssetDestroyThunk Destroy = nullptr;
};

struct FAssetLoadProviders
{
    TAssetLoadProvider<FMeshAsset, FMeshAssetData> Mesh;
    TAssetLoadProvider<FMaterialAsset, FMaterialAssetData> Material;
};

// Single-threaded. Keep the catalog fixed and provider code/context alive for
// the session. Wait for GPU users before destroying a session with GPU payloads.
class ASSETLOADING_API FAssetLoadSession final : public FNonCopyable
{
public:
    explicit FAssetLoadSession(const FAssetCatalog& Catalog, FAssetLoadProviders Providers = {}, IAllocator& Allocator = GetDefaultAllocator()) noexcept;
    ~FAssetLoadSession() noexcept;

    const FAssetRegistry& GetRegistry() const noexcept { return Registry; }

    EAssetLoadResult Load(FAssetId Id, FMeshAssetHandle& Out, FAssetLoadDiagnostic* Diagnostic = nullptr) noexcept;
    EAssetLoadResult Load(FAssetId Id, FMaterialAssetHandle& Out, FAssetLoadDiagnostic* Diagnostic = nullptr) noexcept;
    EAssetLoadResult Load(FAssetId Id, FCpuTextureAssetHandle& Out, FAssetLoadDiagnostic* Diagnostic = nullptr) noexcept;
    EAssetLoadResult Load(StringView Path, FMeshAssetHandle& Out, FAssetLoadDiagnostic* Diagnostic = nullptr) noexcept;
    EAssetLoadResult Load(StringView Path, FMaterialAssetHandle& Out, FAssetLoadDiagnostic* Diagnostic = nullptr) noexcept;
    EAssetLoadResult Load(StringView Path, FCpuTextureAssetHandle& Out, FAssetLoadDiagnostic* Diagnostic = nullptr) noexcept;

private:
    template <typename Asset> EAssetLoadResult LoadPath(StringView Path, TAssetHandle<Asset>& Out, FAssetLoadDiagnostic* Diagnostic) noexcept;

    template <typename Asset, typename Data> EAssetLoadResult LoadAsset(FAssetId Id, TAssetHandle<Asset>& Out, const TAssetLoadProvider<Asset, Data>& Provider, FAssetLoadDiagnostic* Diagnostic) noexcept;
    template <typename Asset, typename Data> EAssetLoadResult LoadAssetInternal(FAssetId Id, TAssetHandle<Asset>& Out, const TAssetLoadProvider<Asset, Data>& Provider, FAssetLoadDiagnostic* Diagnostic) noexcept;

    const FAssetCatalog& Catalog;
    const FAssetLoadProviders Providers;
    IAllocator& Allocator;
    FAssetRegistry Registry;
    bool bLoading = false;
};

} // namespace LE
