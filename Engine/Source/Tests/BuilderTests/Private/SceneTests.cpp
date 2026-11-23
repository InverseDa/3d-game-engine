#include "Renderer/RenderAssets.h"
#include "Scene/Scene.h"
#include "World/WorldCommandBuffer.h"

#include <cstdio>
#include <limits>
#include <utility>

namespace LE
{
namespace
{

using EResult = ESceneOperationResult;

bool ExpectScene(const bool bCondition, const char* Label)
{
    if (!bCondition)
    {
        std::fprintf(stderr, "FAILED: Scene: %s\n", Label);
    }
    return bCondition;
}

template <typename IdType> IdType ParseSceneTestId(const StringView Text)
{
    IdType Id;
    if (!IdType::TryParse(Text, Id))
    {
        HandleContractViolation("bad test fixture ID", __FILE__, __LINE__);
    }
    return Id;
}

class FSceneFailingAllocator final : public IAllocator
{
public:
    void* Allocate(const std::size_t Size, const std::size_t Alignment) noexcept override
    {
        if (Attempts++ >= FailAfter)
        {
            return nullptr;
        }
        void* const Result = GetDefaultAllocator().Allocate(Size, Alignment);
        if (Result != nullptr)
        {
            ++Outstanding;
        }
        return Result;
    }

    void Deallocate(void* const Address, const std::size_t Size, const std::size_t Alignment) noexcept override
    {
        if (Address != nullptr)
        {
            --Outstanding;
            GetDefaultAllocator().Deallocate(Address, Size, Alignment);
        }
    }

    std::size_t Attempts = 0;
    std::size_t FailAfter = (std::numeric_limits<std::size_t>::max)();
    std::size_t Outstanding = 0;
};

struct FSceneTestIds
{
    FSceneEntityId Empty = ParseSceneTestId<FSceneEntityId>("10000000-0000-4000-8000-000000000001");
    FSceneEntityId Left = ParseSceneTestId<FSceneEntityId>("10000000-0000-4000-8000-000000000002");
    FSceneEntityId Right = ParseSceneTestId<FSceneEntityId>("10000000-0000-4000-8000-000000000003");
    FSceneEntityId Camera = ParseSceneTestId<FSceneEntityId>("10000000-0000-4000-8000-000000000004");
    FSceneEntityId Parent = ParseSceneTestId<FSceneEntityId>("10000000-0000-4000-8000-000000000005");
    FAssetId Mesh = ParseSceneTestId<FAssetId>("20000000-0000-4000-8000-000000000001");
    FAssetId Material = ParseSceneTestId<FAssetId>("20000000-0000-4000-8000-000000000002");
};

FSceneDocument MakeSceneDocument(const FSceneTestIds& Ids)
{
    FSceneDocument Document;
    FSceneEntityRecord& Parent = Document.Entities.EmplaceBack();
    Parent.Id = Ids.Parent;
    Parent.bHasTransform = true;
    Parent.Transform.Translation[1] = 3.0f;
    FSceneEntityRecord& Camera = Document.Entities.EmplaceBack();
    Camera.Id = Ids.Camera;
    Camera.bHasCamera = true;
    Camera.bHasTransform = true;
    Camera.Transform.Translation[2] = 5.0f;
    Camera.Camera.DebugName = "Test camera";
    Camera.Camera.Priority = 7;
    Camera.Camera.FarPlane = 300.0f;
    for (const FSceneEntityId Id : {Ids.Right, Ids.Left})
    {
        FSceneEntityRecord& Child = Document.Entities.EmplaceBack();
        Child.Id = Id;
        Child.Parent = Ids.Parent;
        Child.bHasMesh = true;
        Child.bHasTransform = true;
        Child.Transform.Translation[0] = Id == Ids.Left ? -2.0f : 2.0f;
        Child.Mesh.Mesh = Ids.Mesh;
        Child.Mesh.Material = Ids.Material;
        Child.Mesh.SortKey = 123;
        Child.Mesh.DebugName = "Shared test mesh";
    }
    Document.Entities.EmplaceBack().Id = Ids.Empty;
    return Document;
}

struct FSceneFakeAssets
{
    FMeshAsset Mesh;
    FMaterialAsset Material;
    FAssetRegistry Registry;
    FSceneFailingAllocator* StopWorldAllocationAfterMaterial = nullptr;
    std::size_t MeshCalls = 0;
    std::size_t MaterialCalls = 0;
    bool bFailMaterial = false;

    FSceneAssetResolver MakeResolver() noexcept { return {this, &Registry, &ResolveMesh, &ResolveMaterial}; }

    static bool ResolveMesh(void* const Context, const FAssetId Id, FMeshAssetHandle& Out) noexcept
    {
        auto& Assets = *static_cast<FSceneFakeAssets*>(Context);
        ++Assets.MeshCalls;
        TAssetRegistration<FMeshAsset> Registration;
        Registration.Id = Id;
        Registration.SoftPath = "Game/mesh.leasset";
        Registration.ProviderName = "SceneTests";
        Registration.Object = &Assets.Mesh;
        return Assets.Registry.Register(Registration, Out) == EAssetRegisterResult::Success;
    }

    static bool ResolveMaterial(void* const Context, const FAssetId Id, FMaterialAssetHandle& Out) noexcept
    {
        auto& Assets = *static_cast<FSceneFakeAssets*>(Context);
        ++Assets.MaterialCalls;
        if (Assets.bFailMaterial)
        {
            return false;
        }
        TAssetRegistration<FMaterialAsset> Registration;
        Registration.Id = Id;
        Registration.SoftPath = "Game/material.leasset";
        Registration.ProviderName = "SceneTests";
        Registration.Object = &Assets.Material;
        if (Assets.Registry.Register(Registration, Out) != EAssetRegisterResult::Success)
        {
            return false;
        }
        if (Assets.StopWorldAllocationAfterMaterial != nullptr)
        {
            auto& Allocator = *Assets.StopWorldAllocationAfterMaterial;
            Allocator.FailAfter = Allocator.Attempts;
        }
        return true;
    }
};

bool TestRestoredScene(const FScene& Scene, const FSceneTestIds& Ids)
{
    const FWorld& World = Scene.GetWorld();
    FEntity Parent;
    FEntity Left;
    FEntity Right;
    FEntity Camera;
    FEntity Empty;
    if (!ExpectScene(World.GetEntityCount() == 5 && Scene.TryFindEntity(Ids.Parent, Parent) && Scene.TryFindEntity(Ids.Left, Left) && Scene.TryFindEntity(Ids.Right, Right) && Scene.TryFindEntity(Ids.Camera, Camera) && Scene.TryFindEntity(Ids.Empty, Empty), "restored all persistent IDs"))
    {
        return false;
    }
    FEntity ActualParent;
    FSceneEntityId ActualId;
    FTransform Local;
    Math::Matrix4f LeftWorld;
    const auto* const LeftMesh = World.GetComponent(*World.GetMeshComponents(), Left);
    const auto* const RightMesh = World.GetComponent(*World.GetMeshComponents(), Right);
    const auto* const RestoredCamera = World.GetComponent(*World.GetCameraComponents(), Camera);
    bool bPassed = ExpectScene(World.TryGetTransformParent(Left, ActualParent) && ActualParent == Parent && World.TryGetTransformParent(Right, ActualParent) && ActualParent == Parent && World.TryGetLocalTransform(Left, Local) && Local.Translation[0] == -2.0f && World.TryGetWorldMatrix(Left, LeftWorld) && LeftWorld(1, 3) == 3.0f && Scene.TryGetEntityId(Parent, ActualId) && ActualId == Ids.Parent, "restored parent links and derived matrices");
    bPassed &= ExpectScene(LeftMesh != nullptr && RightMesh != nullptr && LeftMesh->Mesh == RightMesh->Mesh && LeftMesh->Material == RightMesh->Material && LeftMesh->SortKey == 123 && LeftMesh->DebugName.View() == StringView("Shared test mesh"), "shared typed handles and mesh fields");
    bPassed &= ExpectScene(RestoredCamera != nullptr && RestoredCamera->bActive && RestoredCamera->FarPlane == 300.0f && RestoredCamera->Priority == 7 && RestoredCamera->DebugName.View() == StringView("Test camera"), "camera fields");
    bPassed &= ExpectScene(!World.HasTransform(Empty) && World.GetComponent(*World.GetMeshComponents(), Empty) == nullptr, "empty entity remains empty");
    return bPassed;
}

bool TestFailedRestore(const FSceneDocument& Source, const FSceneTestIds& Ids, const FSceneFakeAssets& PublishedAssets, UniquePtr<FScene>& Published)
{
    const FScene* const Initial = Published.Get();
    FSceneOperationDiagnostic Diagnostic;
    bool bPassed = true;
    {
        FSceneFakeAssets Candidate;
        Candidate.bFailMaterial = true;
        bPassed &= ExpectScene(RestoreScene(Source, Candidate.MakeResolver(), Published, &Diagnostic) == EResult::AssetResolutionFailed && Published.Get() == Initial && Diagnostic.Entity == Ids.Left && Diagnostic.Asset == Ids.Material && Candidate.Registry.GetAssetCount() == 1 && PublishedAssets.Registry.GetAssetCount() == 2, "partial candidate failure keeps active session");
    }
    {
        FSceneFailingAllocator Allocator;
        FSceneFakeAssets Candidate;
        Candidate.StopWorldAllocationAfterMaterial = &Allocator;
        bPassed &= ExpectScene(RestoreScene(Source, Candidate.MakeResolver(), Published, &Diagnostic, Allocator) == EResult::AllocationFailed && Published.Get() == Initial && Allocator.Outstanding == 0 && Candidate.Registry.GetAssetCount() == 2, "allocation failure rolls back candidate World");
    }
    {
        FSceneFakeAssets Candidate;
        const FSceneAssetResolver Resolver = Candidate.MakeResolver();
        FMeshAssetHandle WrongType;
        bPassed &= ExpectScene(Resolver.ResolveMesh(Resolver.Context, Ids.Material, WrongType), "prepare wrong asset type");
        FSceneDocument Wrong = MakeSceneDocument(Ids);
        for (FSceneEntityRecord& Entity : Wrong.Entities)
        {
            if (Entity.bHasMesh)
            {
                Entity.Mesh.Mesh = Ids.Material;
            }
        }
        bPassed &= ExpectScene(RestoreScene(Wrong, Resolver, Published, &Diagnostic) == EResult::AssetResolutionFailed && Published.Get() == Initial && Diagnostic.Asset == Ids.Material && Candidate.MaterialCalls == 0, "reject wrong asset type");
    }
    {
        FSceneFakeAssets Candidate;
        FSceneDocument Invalid = MakeSceneDocument(Ids);
        Invalid.Entities[0].Parent = Ids.Left;
        bPassed &= ExpectScene(RestoreScene(Invalid, Candidate.MakeResolver(), Published, &Diagnostic) == EResult::InvalidDocument && Published.Get() == Initial && Candidate.MeshCalls == 0 && Diagnostic.Document.Result == ESceneDocumentResult::ParentCycle, "invalid hierarchy is rejected before loading resources");
    }
    return bPassed;
}

struct FTemporarySceneFile
{
    String Path;
    ~FTemporarySceneFile()
    {
        if (!Path.IsEmpty())
        {
            static_cast<void>(std::remove(Path.Data()));
        }
    }
};

bool TestSaveAndLoad(const FSceneTestIds& Ids, const FSceneFakeAssets& PublishedAssets, FScene& Published)
{
    FUuid TemporaryId;
    if (!ExpectScene(FUuid::TryGenerate(TemporaryId), "temporary file identity"))
    {
        return false;
    }
    FTemporarySceneFile Temporary;
    Temporary.Path = TemporaryId.ToString();
    Temporary.Path.Append(".scene.json");
    const StringView Path = Temporary.Path.View();
    FWorld& World = Published.GetWorld();
    FEntity Left;
    FTransform Edited;
    if (!ExpectScene(Published.TryFindEntity(Ids.Left, Left) && World.TryGetLocalTransform(Left, Edited), "find entity for edit"))
    {
        return false;
    }
    Edited.Translation[2] = -4.0f;
    bool bPassed = ExpectScene(World.SetLocalTransform(Left, Edited) == ETransformUpdateResult::Updated && SaveScene(Path, Published, PublishedAssets.Registry) == EResult::Success, "save authored transform");
    Array<uint8> OldFile;
    bPassed &= ExpectScene(ReadContentFile(Path, MaxSceneDocumentBytes, OldFile) == EFileSystemResult::Success, "read first save");
    const FWorldTickContext Tick{};
    if (ExpectScene(World.BeginTick(Tick) == EWorldTickResult::Success, "begin uncommitted frame"))
    {
        bPassed &= ExpectScene(SaveScene(Path, Published, PublishedAssets.Registry) == EResult::InvalidState, "reject save during update");
        bPassed &= ExpectScene(World.EndTick() == EWorldTickResult::Success, "end uncommitted frame");
    }
    else
    {
        bPassed = false;
    }
    FEntity Unmanaged;
    if (ExpectScene(World.TryCreateEntity(Unmanaged), "create entity without persistent identity"))
    {
        bPassed &= ExpectScene(SaveScene(Path, Published, PublishedAssets.Registry) == EResult::MissingEntityIdentity, "reject unmanaged entity");
        bPassed &= ExpectScene(World.DestroyEntity(Unmanaged), "remove unmanaged entity");
    }
    else
    {
        bPassed = false;
    }
    FSceneFakeAssets ForeignAssets;
    bPassed &= ExpectScene(SaveScene(Path, Published, ForeignAssets.Registry) == EResult::InvalidAssetHandle, "reject save with foreign registry");
    Array<uint8> CurrentFile;
    const bool bReadCurrent = ReadContentFile(Path, MaxSceneDocumentBytes, CurrentFile) == EFileSystemResult::Success;
    bool bEqual = OldFile.Size() == CurrentFile.Size();
    for (std::size_t Index = 0; bEqual && Index < OldFile.Size(); ++Index)
    {
        bEqual = OldFile[Index] == CurrentFile[Index];
    }
    bPassed &= ExpectScene(bReadCurrent && bEqual, "rejected saves preserve existing bytes");

    FSceneFakeAssets ReloadedAssets;
    UniquePtr<FScene> Reloaded;
    FSceneOperationDiagnostic Diagnostic;
    if (!ExpectScene(LoadScene(Path, ReloadedAssets.MakeResolver(), Reloaded, &Diagnostic) == EResult::Success, "reload saved document into fresh World and registry"))
    {
        return false;
    }
    bPassed &= TestRestoredScene(*Reloaded, Ids);
    FEntity ReloadedLeft;
    FTransform Local;
    bPassed &= ExpectScene(Reloaded->TryFindEntity(Ids.Left, ReloadedLeft) && ReloadedLeft != Left && Reloaded->GetWorld().TryGetLocalTransform(ReloadedLeft, Local) && Local.Translation[2] == -4.0f, "save and reload retains transform without retaining runtime handle");
    return bPassed;
}

bool TestDirectionalLightSaveAndLoad(const FSceneTestIds& Ids)
{
    FSceneDocument Document;
    auto& AuthoredActive = Document.Entities.EmplaceBack();
    AuthoredActive.Id = Ids.Left;
    AuthoredActive.bHasDirectionalLight = true;
    AuthoredActive.DirectionalLight = {Math::Vector3f(1.0f, 0.0f, -4.0f), Math::Vector3f(1.0f, 0.5f, 0.25f), 8.0f, true};
    auto& AuthoredDisabled = Document.Entities.EmplaceBack();
    AuthoredDisabled.Id = Ids.Right;
    AuthoredDisabled.bHasDirectionalLight = true;
    AuthoredDisabled.DirectionalLight = {Math::Vector3f(0.0f, -2.0f, 0.0f), Math::Vector3f(0.0f, 1.0f, 0.5f), 16.0f, false};

    FAssetRegistry Registry;
    const FSceneAssetResolver Resolver{nullptr, &Registry};
    UniquePtr<FScene> Published;
    FUuid TemporaryId;
    if (!ExpectScene(FUuid::TryGenerate(TemporaryId) && RestoreScene(Document, Resolver, Published) == EResult::Success, "restore two light entities"))
    {
        return false;
    }
    FTemporarySceneFile Temporary;
    Temporary.Path = TemporaryId.ToString();
    Temporary.Path.Append(".scene.json");
    const StringView Path = Temporary.Path.View();
    Array<uint8> WithLights;
    UniquePtr<FScene> Reloaded;
    if (!ExpectScene(SaveScene(Path, *Published, Registry) == EResult::Success && ReadContentFile(Path, MaxSceneDocumentBytes, WithLights) == EFileSystemResult::Success && LoadScene(Path, Resolver, Reloaded) == EResult::Success, "save and reload v2 light scene"))
    {
        return false;
    }
    FWorld& World = Reloaded->GetWorld();
    auto* const Storage = World.GetDirectionalLightComponents();
    FEntity ActiveEntity;
    FEntity DisabledEntity;
    if (!ExpectScene(Storage != nullptr && Storage->GetEntities().Size() == 2 && Reloaded->TryFindEntity(Ids.Left, ActiveEntity) && Reloaded->TryFindEntity(Ids.Right, DisabledEntity), "restore light storage and persistent IDs"))
    {
        return false;
    }
    const auto* const ActiveLight = World.GetComponent(*Storage, ActiveEntity);
    const auto* const DisabledLight = World.GetComponent(*Storage, DisabledEntity);
    bool bPassed = ExpectScene(ActiveLight != nullptr && ActiveLight->bActive && ActiveLight->Direction == Math::Vector3f(1, 0, -4) && ActiveLight->Color == Math::Vector3f(1, 0.5f, 0.25f) && ActiveLight->Intensity == 8.0f && DisabledLight != nullptr && !DisabledLight->bActive && DisabledLight->Direction == Math::Vector3f(0, -2, 0) && DisabledLight->Color == Math::Vector3f(0, 1, 0.5f) && DisabledLight->Intensity == 16.0f && !World.HasTransform(ActiveEntity) && !World.HasTransform(DisabledEntity), "preserve authored values and require no transform for lights");
    Array<uint8> SavedAgain;
    const StringView FirstSave{reinterpret_cast<const char*>(WithLights.Data()), WithLights.Size()};
    const StringView Version2Prefix("{\n  \"version\": 2,\n");
    bPassed &= ExpectScene(FirstSave.Substr(0, Version2Prefix.Size()) == Version2Prefix && SaveScene(Path, *Reloaded, Registry) == EResult::Success && ReadContentFile(Path, MaxSceneDocumentBytes, SavedAgain) == EFileSystemResult::Success && FirstSave == StringView(reinterpret_cast<const char*>(SavedAgain.Data()), SavedAgain.Size()), "save preserves deterministic v2 bytes");

    FSceneOperationDiagnostic Diagnostic;
    const auto* const PublishedScene = Reloaded.Get();
    Document.Entities[1].DirectionalLight.bActive = true;
    bPassed &= ExpectScene(RestoreScene(Document, Resolver, Reloaded, &Diagnostic) == EResult::InvalidDocument && Diagnostic.Document.Result == ESceneDocumentResult::MultipleActiveDirectionalLights && Diagnostic.Entity == Ids.Right && Reloaded.Get() == PublishedScene, "reject a second active light before replacing the scene");
    bPassed &= ExpectScene(World.DestroyEntity(ActiveEntity) && Storage->GetEntities().Size() == 1 && World.GetComponent(*Storage, ActiveEntity) == nullptr, "light storage follows entity destruction");
    FSceneDocument Captured;
    String Encoded;
    bPassed &= ExpectScene(CaptureScene(*Reloaded, Registry, Captured) == EResult::Success && EncodeSceneDocument(Captured, Encoded) == ESceneDocumentResult::Success && Encoded.View().Substr(0, Version2Prefix.Size()) == Version2Prefix, "a disabled light on its own still requires Scene v2");
    FWorldDirectionalLightComponent Edited;
    Edited.Direction = Math::Vector3f(0, 3, 0);
    bPassed &= ExpectScene(World.BeginTick(FWorldTickContext{}) == EWorldTickResult::Success && World.GetCommandBuffer()->TrySetComponent(*Storage, DisabledEntity, std::move(Edited)) == EWorldCommandRecordResult::Recorded && World.EndTick() == EWorldTickResult::Success && CaptureScene(*Reloaded, Registry, Captured) == EResult::Success && Captured.Entities.Size() == 1 && Captured.Entities[0].DirectionalLight.bActive && Captured.Entities[0].DirectionalLight.Direction == Math::Vector3f(0, 3, 0), "deferred light edits reach the committed scene");
    bPassed &= ExpectScene(World.Reset() && Storage->GetEntities().IsEmpty() && CaptureScene(*Reloaded, Registry, Captured) == EResult::Success && EncodeSceneDocument(Captured, Encoded) == ESceneDocumentResult::Success && Encoded.View() == StringView("{\n  \"version\": 1,\n  \"entities\": []\n}\n"), "World reset removes light storage values and saves the original v1 form");
    return bPassed;
}

bool TestMeshTransformPreflight(const FSceneTestIds& Ids)
{
    FSceneDocument Document = MakeSceneDocument(Ids);
    auto& Light = Document.Entities.EmplaceBack();
    Light.Id = ParseSceneTestId<FSceneEntityId>("10000000-0000-4000-8000-000000000006");
    Light.bHasDirectionalLight = true;
    auto& ParentScale = Document.Entities[0].Transform.Scale;
    ParentScale = Math::Vector3f(-2.0f, 3.0f, 0.5f);

    bool bMeshHasNormals = true;
    const FSceneMeshNormalResolver MeshNormals{&bMeshHasNormals, [](const void* Context, FAssetId) noexcept { return *static_cast<const bool*>(Context); }};
    FSceneOperationDiagnostic Diagnostic;
    bool bPassed = ExpectScene(ValidateSceneMeshTransforms(Document, MeshNormals, &Diagnostic) == EResult::Success, "preflight accepts nonsingular mirrored world transforms");
    ParentScale[0] = 0.0f;
    bPassed &= ExpectScene(ValidateSceneMeshTransforms(Document, MeshNormals, &Diagnostic) == EResult::InvalidNormalTransform && Diagnostic.Entity == Ids.Left && Diagnostic.Asset == Ids.Mesh && Diagnostic.Document.Field == StringView("transform"), "preflight reports the lit mesh child and asset for a singular ancestor transform");
    Light.DirectionalLight.bActive = false;
    bPassed &= ExpectScene(ValidateSceneMeshTransforms(Document, MeshNormals, &Diagnostic) == EResult::Success && !Diagnostic.Entity.IsValid(), "preflight preserves the singular transform fallback without an active light");
    Light.DirectionalLight.bActive = true;
    bMeshHasNormals = false;
    bPassed &= ExpectScene(ValidateSceneMeshTransforms(Document, MeshNormals, &Diagnostic) == EResult::Success, "preflight preserves the singular transform fallback without authored normals");
    return bPassed;
}

} // namespace

bool RunSceneTests()
{
    const FSceneTestIds Ids;
    const FSceneDocument Document = MakeSceneDocument(Ids);
    FSceneFakeAssets Assets;
    UniquePtr<FScene> Published;
    FSceneOperationDiagnostic Diagnostic;
    if (!ExpectScene(RestoreScene(Document, Assets.MakeResolver(), Published, &Diagnostic) == EResult::Success, "restore valid document"))
    {
        return false;
    }
    bool bPassed = ExpectScene(Assets.MeshCalls == 1 && Assets.MaterialCalls == 1, "shared assets are resolved only once");
    bPassed &= TestRestoredScene(*Published, Ids);
    bPassed &= TestFailedRestore(Document, Ids, Assets, Published);
    bPassed &= TestSaveAndLoad(Ids, Assets, *Published);
    bPassed &= TestDirectionalLightSaveAndLoad(Ids);
    bPassed &= TestMeshTransformPreflight(Ids);
    FEntity Removed;
    if (ExpectScene(Published->TryFindEntity(Ids.Empty, Removed) && Published->GetWorld().DestroyEntity(Removed), "destroy a scene entity via World"))
    {
        FEntity NewEntity = Removed;
        bPassed &= ExpectScene(!Published->TryFindEntity(Ids.Empty, NewEntity) && NewEntity == Removed && Published->TryCreateEntity(Ids.Empty, NewEntity) && NewEntity != Removed, "identity storage follows World entity lifetime");
    }
    else
    {
        bPassed = false;
    }
    return bPassed;
}

} // namespace LE
