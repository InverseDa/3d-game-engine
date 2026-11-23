#include "Scene/Scene.h"

#include <algorithm>
#include <new>
#include <utility>

namespace LE
{
namespace
{

using EResult = ESceneOperationResult;

EResult Report(FSceneOperationDiagnostic* const Diagnostic, const EResult Result, const FSceneEntityId Entity = {}, const FAssetId Asset = {}, const EFileSystemResult File = EFileSystemResult::Success, const FSceneDocumentDiagnostic& Document = {}) noexcept
{
    if (Diagnostic != nullptr)
    {
        *Diagnostic = FSceneOperationDiagnostic{Result, Document, Entity, Asset, File};
    }
    return Result;
}

EResult ReportDocument(FSceneOperationDiagnostic* const Diagnostic, const FSceneDocumentDiagnostic& Document) noexcept
{
    const EResult Result = Document.Result == ESceneDocumentResult::AllocationFailed ? EResult::AllocationFailed : EResult::InvalidDocument;
    return Report(Diagnostic, Result, Document.Entity, {}, EFileSystemResult::Success, Document);
}

EResult ReportComponent(FSceneOperationDiagnostic* const Diagnostic, const EComponentMutationResult Mutation, const FSceneEntityId Entity) noexcept
{
    return Report(Diagnostic, Mutation == EComponentMutationResult::AllocationFailed ? EResult::AllocationFailed : EResult::WorldMutationFailed, Entity);
}

template <typename Asset> EResult ResolveAsset(const FSceneAssetResolver& Resolver, const FAssetId Id, bool (*Resolve)(void*, FAssetId, TAssetHandle<Asset>&) noexcept, TAssetHandle<Asset>& OutHandle) noexcept
{
    TAssetHandle<Asset> Handle;
    const EAssetFindResult Cached = Resolver.Registry->TryFindById(Id, Handle);
    if (Cached != EAssetFindResult::Success)
    {
        if (Cached != EAssetFindResult::NotFound || Resolve == nullptr || !Resolve(Resolver.Context, Id, Handle))
        {
            return EResult::AssetResolutionFailed;
        }
    }
    FAssetId ResolvedId;
    if (Resolver.Registry->TryGetAssetId(Handle, ResolvedId) != EAssetResolveResult::Success || ResolvedId != Id)
    {
        return EResult::InvalidAssetHandle;
    }
    OutHandle = Handle;
    return EResult::Success;
}

EResult AddMesh(FWorld& World, const FEntity Entity, const FSceneEntityRecord& Record, const FSceneAssetResolver& Resolver, IAllocator& Allocator, FSceneOperationDiagnostic* const Diagnostic) noexcept
{
    FWorldMeshComponent Component;
    Component.DebugName = String(Allocator);
    if (!Component.DebugName.TryAssign(Record.Mesh.DebugName.View()))
    {
        return Report(Diagnostic, EResult::AllocationFailed, Record.Id);
    }
    Component.SortKey = Record.Mesh.SortKey;
    if (const EResult Result = ResolveAsset(Resolver, Record.Mesh.Mesh, Resolver.ResolveMesh, Component.Mesh); Result != EResult::Success)
    {
        return Report(Diagnostic, Result, Record.Id, Record.Mesh.Mesh);
    }
    if (const EResult Result = ResolveAsset(Resolver, Record.Mesh.Material, Resolver.ResolveMaterial, Component.Material); Result != EResult::Success)
    {
        return Report(Diagnostic, Result, Record.Id, Record.Mesh.Material);
    }
    const EComponentMutationResult Mutation = World.TryAddComponent(*World.GetMeshComponents(), Entity, std::move(Component));
    return Mutation == EComponentMutationResult::Added ? EResult::Success : ReportComponent(Diagnostic, Mutation, Record.Id);
}

EResult AddCamera(FWorld& World, const FEntity Entity, const FSceneEntityRecord& Record, IAllocator& Allocator, FSceneOperationDiagnostic* const Diagnostic) noexcept
{
    FWorldCameraComponent Component;
    Component.DebugName = String(Allocator);
    if (!Component.DebugName.TryAssign(Record.Camera.DebugName.View()))
    {
        return Report(Diagnostic, EResult::AllocationFailed, Record.Id);
    }
    Component.VerticalFieldOfViewRadians = Record.Camera.VerticalFieldOfViewRadians;
    Component.NearPlane = Record.Camera.NearPlane;
    Component.FarPlane = Record.Camera.FarPlane;
    Component.Priority = Record.Camera.Priority;
    Component.bActive = Record.Camera.bActive;
    const EComponentMutationResult Mutation = World.TryAddComponent(*World.GetCameraComponents(), Entity, std::move(Component));
    return Mutation == EComponentMutationResult::Added ? EResult::Success : ReportComponent(Diagnostic, Mutation, Record.Id);
}

struct FRestoreEntity
{
    const FSceneEntityRecord* Record = nullptr;
    FEntity Entity;
};

template <typename CreateEntityType> EResult RestoreHierarchy(const FSceneDocument& Document, FWorld& World, Array<FRestoreEntity>& Entities, const CreateEntityType& CreateEntity, FSceneOperationDiagnostic* const Diagnostic) noexcept
{
    if (!Entities.TryReserve(Document.Entities.Size()))
    {
        return Report(Diagnostic, EResult::AllocationFailed);
    }
    for (const FSceneEntityRecord& Record : Document.Entities)
    {
        if (!Entities.TryPushBack(FRestoreEntity{&Record, {}}))
        {
            return Report(Diagnostic, EResult::AllocationFailed);
        }
    }
    if (Entities.Size() > 1)
    {
        std::sort(Entities.Data(), Entities.Data() + Entities.Size(), [](const FRestoreEntity& Left, const FRestoreEntity& Right) { return Left.Record->Id < Right.Record->Id; });
    }
    for (FRestoreEntity& Item : Entities)
    {
        const FSceneEntityRecord& Record = *Item.Record;
        if (!CreateEntity(Record.Id, Item.Entity))
        {
            return Report(Diagnostic, EResult::AllocationFailed, Record.Id);
        }
        if (Record.bHasTransform)
        {
            const auto Mutation = World.TryAddTransform(Item.Entity, Record.Transform);
            if (Mutation != EComponentMutationResult::Added)
            {
                return ReportComponent(Diagnostic, Mutation, Record.Id);
            }
        }
    }
    for (const FRestoreEntity& Item : Entities)
    {
        const FSceneEntityRecord& Record = *Item.Record;
        if (!Record.Parent.IsValid())
        {
            continue;
        }
        const auto* const End = Entities.Data() + Entities.Size();
        const auto* const Parent = std::lower_bound(static_cast<const FRestoreEntity*>(Entities.Data()), End, Record.Parent, [](const FRestoreEntity& Entry, const FSceneEntityId Id) { return Entry.Record->Id < Id; });
        if (Parent == End || Parent->Record->Id != Record.Parent || World.SetTransformParent(Item.Entity, Parent->Entity) != ETransformParentResult::Reparented)
        {
            return Report(Diagnostic, EResult::WorldMutationFailed, Record.Id);
        }
    }
    return World.UpdateTransforms() ? EResult::Success : Report(Diagnostic, EResult::WorldMutationFailed);
}

} // namespace

FScene::FScene(IAllocator& Allocator) noexcept : Identities(Allocator), World(Allocator)
{
    if (World.IsValid())
    {
        static_cast<void>(World.AttachComponentStorage(Identities));
    }
}

bool FScene::Create(UniquePtr<FScene>& OutScene, IAllocator& Allocator) noexcept
{
    FScene* const Storage = TryAllocateArray<FScene>(Allocator, 1);
    if (Storage == nullptr)
    {
        return false;
    }
    FScene* const Object = new (Storage) FScene(Allocator);
    auto Candidate = UniquePtr<FScene>::Adopt(Object, &Allocator, [](void* const Context, FScene* const Scene) noexcept {
        Scene->~FScene();
        DeallocateArray(*static_cast<IAllocator*>(Context), Scene, 1);
    });
    if (!Object->Identities.IsBound())
    {
        return false;
    }
    OutScene = std::move(Candidate);
    return true;
}

bool FScene::TryCreateEntity(const FSceneEntityId Id, FEntity& OutEntity) noexcept
{
    FEntity Duplicate;
    return Id.IsValid() && !TryFindEntity(Id, Duplicate) && TryCreateEntityWithUniqueId(Id, OutEntity);
}

bool FScene::TryCreateEntityWithUniqueId(const FSceneEntityId Id, FEntity& OutEntity) noexcept
{
    FEntity Candidate;
    if (World.GetTickState() != EWorldTickState::Idle || !World.TryCreateEntity(Candidate))
    {
        return false;
    }
    if (World.TryAddComponent(Identities, Candidate, Id) != EComponentMutationResult::Added)
    {
        static_cast<void>(World.DestroyEntity(Candidate));
        return false;
    }
    OutEntity = Candidate;
    return true;
}

bool FScene::TryFindEntity(const FSceneEntityId Id, FEntity& OutEntity) const noexcept
{
    const auto Values = Identities.GetComponents();
    const auto Entities = Identities.GetEntities();
    for (std::size_t Index = 0; Index < Values.Size(); ++Index)
    {
        if (Values[Index] == Id)
        {
            OutEntity = Entities[Index];
            return true;
        }
    }
    return false;
}

bool FScene::TryGetEntityId(const FEntity Entity, FSceneEntityId& OutId) const noexcept
{
    const FSceneEntityId* const Id = World.GetComponent(Identities, Entity);
    if (Id == nullptr)
    {
        return false;
    }
    OutId = *Id;
    return true;
}

ESceneOperationResult RestoreScene(const FSceneDocument& Document, const FSceneAssetResolver& Assets, UniquePtr<FScene>& OutScene, FSceneOperationDiagnostic* const Diagnostic, IAllocator& Allocator) noexcept
{
    FSceneDocumentDiagnostic Format;
    if (ValidateSceneDocument(Document, &Format) != ESceneDocumentResult::Success)
    {
        return ReportDocument(Diagnostic, Format);
    }
    if (Assets.Registry == nullptr || !Assets.Registry->IsValid() || (OutScene && OutScene->GetWorld().GetTickState() != EWorldTickState::Idle))
    {
        return Report(Diagnostic, EResult::InvalidState);
    }
    UniquePtr<FScene> Candidate;
    if (!FScene::Create(Candidate, Allocator))
    {
        return Report(Diagnostic, EResult::AllocationFailed);
    }
    Array<FRestoreEntity> Entities(Allocator);
    FWorld& World = Candidate->GetWorld();
    const auto CreateEntity = [&Candidate](const FSceneEntityId Id, FEntity& Entity) noexcept { return Candidate->TryCreateEntityWithUniqueId(Id, Entity); };
    if (const EResult Result = RestoreHierarchy(Document, World, Entities, CreateEntity, Diagnostic); Result != EResult::Success)
    {
        return Result;
    }
    for (const FRestoreEntity& Item : Entities)
    {
        const FSceneEntityRecord& Record = *Item.Record;
        if (Record.bHasMesh)
        {
            const EResult Result = AddMesh(World, Item.Entity, Record, Assets, Allocator, Diagnostic);
            if (Result != EResult::Success)
            {
                return Result;
            }
        }
        if (Record.bHasCamera)
        {
            const EResult Result = AddCamera(World, Item.Entity, Record, Allocator, Diagnostic);
            if (Result != EResult::Success)
            {
                return Result;
            }
        }
        if (Record.bHasDirectionalLight)
        {
            const FSceneDirectionalLightRecord& Light = Record.DirectionalLight;
            const auto Mutation = World.TryAddComponent(*World.GetDirectionalLightComponents(), Item.Entity, FWorldDirectionalLightComponent{Light.Direction, Light.Color, Light.Intensity, Light.bActive});
            if (Mutation != EComponentMutationResult::Added)
            {
                return ReportComponent(Diagnostic, Mutation, Record.Id);
            }
        }
    }
    OutScene = std::move(Candidate);
    return Report(Diagnostic, EResult::Success);
}

ESceneOperationResult LoadScene(const StringView Path, const FSceneAssetResolver& Assets, UniquePtr<FScene>& OutScene, FSceneOperationDiagnostic* const Diagnostic, IAllocator& Allocator) noexcept
{
    Array<uint8> Bytes(Allocator);
    const EFileSystemResult ReadResult = ReadContentFile(Path, MaxSceneDocumentBytes, Bytes);
    if (ReadResult != EFileSystemResult::Success)
    {
        return Report(Diagnostic, ReadResult == EFileSystemResult::OutOfMemory ? EResult::AllocationFailed : EResult::FileReadFailed, {}, {}, ReadResult);
    }
    FSceneDocument Document(Allocator);
    FSceneDocumentDiagnostic Format;
    const StringView Text(reinterpret_cast<const char*>(Bytes.Data()), Bytes.Size());
    if (DecodeSceneDocument(Text, Document, &Format) != ESceneDocumentResult::Success)
    {
        return ReportDocument(Diagnostic, Format);
    }
    return RestoreScene(Document, Assets, OutScene, Diagnostic, Allocator);
}

ESceneOperationResult ValidateSceneMeshTransforms(const FSceneDocument& Document, const FSceneMeshNormalResolver& MeshNormals, FSceneOperationDiagnostic* const Diagnostic) noexcept
{
    if (MeshNormals.HasNormals == nullptr)
    {
        return Report(Diagnostic, EResult::InvalidState);
    }
    bool bHasActiveLight = false;
    bool bHasMeshNormals = false;
    for (const FSceneEntityRecord& Record : Document.Entities)
    {
        bHasActiveLight |= Record.bHasDirectionalLight && Record.DirectionalLight.bActive;
        bHasMeshNormals |= Record.bHasMesh && MeshNormals.HasNormals(MeshNormals.Context, Record.Mesh.Mesh);
        if (bHasActiveLight && bHasMeshNormals)
        {
            break;
        }
    }
    if (!bHasActiveLight || !bHasMeshNormals)
    {
        return Report(Diagnostic, EResult::Success);
    }

    IAllocator& Allocator = Document.Entities.GetAllocator();
    FWorld World(Allocator);
    if (!World.IsValid())
    {
        return Report(Diagnostic, EResult::AllocationFailed);
    }
    Array<FRestoreEntity> Entities(Allocator);
    const auto CreateEntity = [&World](FSceneEntityId, FEntity& Entity) noexcept { return World.TryCreateEntity(Entity); };
    if (const EResult Result = RestoreHierarchy(Document, World, Entities, CreateEntity, Diagnostic); Result != EResult::Success)
    {
        return Result;
    }
    for (const FRestoreEntity& Item : Entities)
    {
        const FSceneEntityRecord& Record = *Item.Record;
        if (!Record.bHasMesh || !MeshNormals.HasNormals(MeshNormals.Context, Record.Mesh.Mesh))
        {
            continue;
        }
        Math::Matrix4f LocalToWorld = Math::Matrix4f::Identity();
        if (Record.bHasTransform && !World.TryGetWorldMatrix(Item.Entity, LocalToWorld))
        {
            return Report(Diagnostic, EResult::WorldMutationFailed, Record.Id, Record.Mesh.Mesh);
        }
        Math::Matrix3f NormalToWorld;
        if (!Math::TryBuildNormalMatrix(LocalToWorld, NormalToWorld))
        {
            FSceneDocumentDiagnostic Format;
            Format.Entity = Record.Id;
            Format.Field = "transform";
            return Report(Diagnostic, EResult::InvalidNormalTransform, Record.Id, Record.Mesh.Mesh, EFileSystemResult::Success, Format);
        }
    }
    return Report(Diagnostic, EResult::Success);
}

ESceneOperationResult CaptureScene(const FScene& Scene, const FAssetRegistry& Registry, FSceneDocument& OutDocument, FSceneOperationDiagnostic* const Diagnostic) noexcept
{
    const FWorld& World = Scene.GetWorld();
    if (World.GetTickState() != EWorldTickState::Idle || !Registry.IsValid())
    {
        return Report(Diagnostic, EResult::InvalidState);
    }
    const auto Entities = Scene.Identities.GetEntities();
    const auto Ids = Scene.Identities.GetComponents();
    if (World.GetEntityCount() != Entities.Size())
    {
        return Report(Diagnostic, EResult::MissingEntityIdentity);
    }
    if (Entities.Size() > MaxSceneEntities)
    {
        FSceneDocumentDiagnostic Format;
        Format.Result = ESceneDocumentResult::LimitExceeded;
        Format.Field = "entities";
        return ReportDocument(Diagnostic, Format);
    }
    IAllocator& Allocator = OutDocument.Entities.GetAllocator();
    FSceneDocument Candidate(Allocator);
    if (!Candidate.Entities.TryReserve(Entities.Size()))
    {
        return Report(Diagnostic, EResult::AllocationFailed);
    }
    for (std::size_t Index = 0; Index < Entities.Size(); ++Index)
    {
        const FEntity Entity = Entities[Index];
        FSceneEntityRecord Record(Allocator);
        Record.Id = Ids[Index];
        Record.bHasTransform = World.TryGetLocalTransform(Entity, Record.Transform);
        if (Record.bHasTransform)
        {
            FEntity Parent;
            if (!World.TryGetTransformParent(Entity, Parent))
            {
                return Report(Diagnostic, EResult::WorldMutationFailed, Record.Id);
            }
            if (Parent.IsValid() && !Scene.TryGetEntityId(Parent, Record.Parent))
            {
                return Report(Diagnostic, EResult::MissingEntityIdentity, Record.Id);
            }
        }
        const auto* const Mesh = World.GetComponent(*World.GetMeshComponents(), Entity);
        if (Mesh != nullptr)
        {
            Record.bHasMesh = true;
            if (Registry.TryGetAssetId(Mesh->Mesh, Record.Mesh.Mesh) != EAssetResolveResult::Success || Registry.TryGetAssetId(Mesh->Material, Record.Mesh.Material) != EAssetResolveResult::Success)
            {
                return Report(Diagnostic, EResult::InvalidAssetHandle, Record.Id);
            }
            if (!Record.Mesh.DebugName.TryAssign(Mesh->DebugName.View()))
            {
                return Report(Diagnostic, EResult::AllocationFailed, Record.Id);
            }
            Record.Mesh.SortKey = Mesh->SortKey;
        }
        const auto* const Camera = World.GetComponent(*World.GetCameraComponents(), Entity);
        if (Camera != nullptr)
        {
            Record.bHasCamera = true;
            if (!Record.Camera.DebugName.TryAssign(Camera->DebugName.View()))
            {
                return Report(Diagnostic, EResult::AllocationFailed, Record.Id);
            }
            Record.Camera.VerticalFieldOfViewRadians = Camera->VerticalFieldOfViewRadians;
            Record.Camera.NearPlane = Camera->NearPlane;
            Record.Camera.FarPlane = Camera->FarPlane;
            Record.Camera.Priority = Camera->Priority;
            Record.Camera.bActive = Camera->bActive;
        }
        const auto* const Light = World.GetComponent(*World.GetDirectionalLightComponents(), Entity);
        if (Light != nullptr)
        {
            Record.bHasDirectionalLight = true;
            Record.DirectionalLight = {Light->Direction, Light->Color, Light->Intensity, Light->bActive};
        }
        if (!Candidate.Entities.TryPushBack(std::move(Record)))
        {
            return Report(Diagnostic, EResult::AllocationFailed);
        }
    }
    FSceneDocumentDiagnostic Format;
    if (ValidateSceneDocument(Candidate, &Format) != ESceneDocumentResult::Success)
    {
        return ReportDocument(Diagnostic, Format);
    }
    OutDocument = std::move(Candidate);
    return Report(Diagnostic, EResult::Success);
}

ESceneOperationResult SaveScene(const StringView Path, const FScene& Scene, const FAssetRegistry& Registry, FSceneOperationDiagnostic* const Diagnostic) noexcept
{
    FSceneDocument Document;
    if (const auto Result = CaptureScene(Scene, Registry, Document, Diagnostic); Result != EResult::Success)
    {
        return Result;
    }
    String Text;
    FSceneDocumentDiagnostic Format;
    if (EncodeSceneDocument(Document, Text, &Format) != ESceneDocumentResult::Success)
    {
        return ReportDocument(Diagnostic, Format);
    }
    const Span<const uint8> Bytes(reinterpret_cast<const uint8*>(Text.Data()), Text.Size());
    const EFileSystemResult File = WriteContentFileAtomically(Path, Bytes);
    if (File != EFileSystemResult::Success)
    {
        return Report(Diagnostic, File == EFileSystemResult::OutOfMemory ? EResult::AllocationFailed : EResult::FileWriteFailed, {}, {}, File);
    }
    return Report(Diagnostic, EResult::Success);
}

const char* DescribeSceneOperationResult(const ESceneOperationResult Result) noexcept
{
    switch (Result)
    {
    case ESceneOperationResult::Success:
        return "success";
    case ESceneOperationResult::InvalidState:
        return "scene or registry is unavailable or busy";
    case ESceneOperationResult::AllocationFailed:
        return "could not allocate scene data";
    case ESceneOperationResult::InvalidDocument:
        return "invalid scene document";
    case ESceneOperationResult::MissingEntityIdentity:
        return "live entity has no persistent scene ID";
    case ESceneOperationResult::AssetResolutionFailed:
        return "could not load the requested asset and type";
    case ESceneOperationResult::InvalidAssetHandle:
        return "asset handle does not resolve in the scene registry";
    case ESceneOperationResult::WorldMutationFailed:
        return "could not apply scene data to the World";
    case ESceneOperationResult::FileReadFailed:
        return "could not read the scene file";
    case ESceneOperationResult::FileWriteFailed:
        return "could not publish the scene file";
    case ESceneOperationResult::InvalidNormalTransform:
        return "lit mesh has an invalid normal transform";
    }
    return "unknown scene failure";
}

} // namespace LE
