#pragma once

#include "Asset/AssetRegistry.h"
#include "ContentIO/ContentIO.h"
#include "Scene/SceneDocument.h"
#include "World/World.h"

namespace LE
{

class FScene;

enum class ESceneOperationResult : uint8
{
    Success,
    InvalidState,
    AllocationFailed,
    InvalidDocument,
    MissingEntityIdentity,
    AssetResolutionFailed,
    InvalidAssetHandle,
    WorldMutationFailed,
    FileReadFailed,
    FileWriteFailed,
};

struct FSceneOperationDiagnostic
{
    ESceneOperationResult Result = ESceneOperationResult::Success;
    FSceneDocumentDiagnostic Document;
    FSceneEntityId Entity;
    FAssetId Asset;
    EContentIOResult FileResult = EContentIOResult::Success;
};

// Callbacks may register assets in this registry. Successful handles must
// resolve to their requested IDs and remain live for the scene's lifetime.
// The caller owns the candidate registry and its rollback on load failure.
struct FSceneAssetResolver
{
    void* Context = nullptr;
    const FAssetRegistry* Registry = nullptr;
    bool (*ResolveMesh)(void*, FAssetId, FMeshAssetHandle&) noexcept = nullptr;
    bool (*ResolveMaterial)(void*, FAssetId, FMaterialAssetHandle&) noexcept = nullptr;
};

class SCENE_API FScene final
{
public:
    static bool Create(UniquePtr<FScene>& OutScene,
        IAllocator& Allocator = GetDefaultAllocator()) noexcept;

    FWorld& GetWorld() noexcept { return World; }
    const FWorld& GetWorld() const noexcept { return World; }

    bool TryCreateEntity(FSceneEntityId Id, FEntity& OutEntity) noexcept;
    bool TryFindEntity(FSceneEntityId Id, FEntity& OutEntity) const noexcept;
    bool TryGetEntityId(FEntity Entity, FSceneEntityId& OutId) const noexcept;

private:
    friend SCENE_API ESceneOperationResult RestoreScene(const FSceneDocument&, const FSceneAssetResolver&,
        UniquePtr<FScene>&, FSceneOperationDiagnostic*, IAllocator&) noexcept;
    friend SCENE_API ESceneOperationResult CaptureScene(const FScene&, const FAssetRegistry&,
        FSceneDocument&, FSceneOperationDiagnostic*) noexcept;

    explicit FScene(IAllocator& Allocator) noexcept;
    ~FScene() = default;
    FScene(const FScene&) = delete;
    FScene& operator=(const FScene&) = delete;

    bool TryCreateEntityWithUniqueId(FSceneEntityId Id, FEntity& OutEntity) noexcept;

    // World is destroyed before its attached identity storage.
    TComponentStorage<FSceneEntityId> Identities;
    FWorld World;
};

// A successful restore/load replaces OutScene. To keep a currently rendered
// scene alive, pass an empty candidate owner and publish it at a frame boundary.
SCENE_API ESceneOperationResult RestoreScene(const FSceneDocument& Document,
    const FSceneAssetResolver& Assets, UniquePtr<FScene>& OutScene,
    FSceneOperationDiagnostic* Diagnostic = nullptr,
    IAllocator& Allocator = GetDefaultAllocator()) noexcept;
SCENE_API ESceneOperationResult LoadScene(StringView Path,
    const FSceneAssetResolver& Assets, UniquePtr<FScene>& OutScene,
    FSceneOperationDiagnostic* Diagnostic = nullptr,
    IAllocator& Allocator = GetDefaultAllocator()) noexcept;
SCENE_API ESceneOperationResult CaptureScene(const FScene& Scene, const FAssetRegistry& Registry,
    FSceneDocument& OutDocument, FSceneOperationDiagnostic* Diagnostic = nullptr) noexcept;
SCENE_API ESceneOperationResult SaveScene(StringView Path, const FScene& Scene,
    const FAssetRegistry& Registry, FSceneOperationDiagnostic* Diagnostic = nullptr) noexcept;
SCENE_API const char* DescribeSceneOperationResult(ESceneOperationResult Result) noexcept;

} // namespace LE
