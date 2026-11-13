#include "WorldRenderer/WorldRendererMinimal.h"

#include "Renderer/RenderAssets.h"
#include "World/WorldMinimal.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace LE
{
namespace
{

int Expect(const bool bCondition, const char* const Name)
{
    if (bCondition)
    {
        return 0;
    }
    std::fprintf(stderr, "FAILED: %s\n", Name);
    return 1;
}

bool NearlyEqual(const float Left, const float Right) noexcept
{
    return std::fabs(Left - Right) <= 1.0e-4f;
}

FWorldRenderSnapshotBuildContext MakeBuildContext(
    const uint64 FrameIndex,
    const uint32 Width = 1280,
    const uint32 Height = 720) noexcept
{
    FWorldRenderSnapshotBuildContext Context;
    Context.FrameIndex = FrameIndex;
    Context.ViewportWidth = Width;
    Context.ViewportHeight = Height;
    return Context;
}

bool TryAddTestCamera(
    FWorld& World,
    FEntity& OutEntity,
    const Math::Vector3f& Translation = Math::Vector3f(0.0f, 0.0f, 10.0f),
    const int32 Priority = 0,
    const char* const Name = "TestCamera") noexcept
{
    TComponentStorage<FWorldCameraComponent>* const Cameras =
        World.GetCameraComponents();
    FTransform Transform;
    Transform.Translation = Translation;
    FWorldCameraComponent Camera;
    Camera.DebugName = Name;
    Camera.Priority = Priority;
    return Cameras != nullptr && World.TryCreateEntity(OutEntity) &&
        World.TryAddTransform(OutEntity, Transform) == EComponentMutationResult::Added &&
        World.TryAddComponent(*Cameras, OutEntity, std::move(Camera)) ==
            EComponentMutationResult::Added;
}

bool HasBounds(
    const FRenderBounds& Bounds,
    const Math::Vector3f& Min,
    const Math::Vector3f& Max) noexcept
{
    return Bounds.IsValid() &&
        NearlyEqual(Bounds.Min.Values[0], Min.Values[0]) &&
        NearlyEqual(Bounds.Min.Values[1], Min.Values[1]) &&
        NearlyEqual(Bounds.Min.Values[2], Min.Values[2]) &&
        NearlyEqual(Bounds.Max.Values[0], Max.Values[0]) &&
        NearlyEqual(Bounds.Max.Values[1], Max.Values[1]) &&
        NearlyEqual(Bounds.Max.Values[2], Max.Values[2]);
}

class FSnapshotFailingAllocator final : public IAllocator
{
public:
    void* Allocate(const std::size_t Size, const std::size_t Alignment) noexcept override
    {
        ++AllocationAttempts;
        if (FailOnAttempt != 0 && AllocationAttempts == FailOnAttempt)
        {
            return nullptr;
        }
        void* const Result = GetDefaultAllocator().Allocate(Size, Alignment);
        if (Result != nullptr)
        {
            ++LiveAllocations;
        }
        return Result;
    }

    void Deallocate(
        void* const Address,
        const std::size_t Size,
        const std::size_t Alignment) noexcept override
    {
        if (Address != nullptr)
        {
            --LiveAllocations;
        }
        GetDefaultAllocator().Deallocate(Address, Size, Alignment);
    }

    void ResetAttempts(const std::size_t InFailOnAttempt) noexcept
    {
        AllocationAttempts = 0;
        FailOnAttempt = InFailOnAttempt;
    }

    std::size_t AllocationAttempts = 0;
    std::size_t FailOnAttempt = 0;
    std::size_t LiveAllocations = 0;
};

struct FWorldRendererFixture
{
    bool Initialize() noexcept
    {
        if (!FAssetId::TryParse(
                "a2300000-0000-4000-8000-000000000001", MeshId) ||
            !FAssetId::TryParse(
                "a2300000-0000-4000-8000-000000000002", MaterialId))
        {
            return false;
        }

        MeshAsset.VertexBuffer = reinterpret_cast<FRHIBuffer*>(
            static_cast<std::uintptr_t>(0x1000));
        MeshAsset.VertexCount = 3;
        MeshAsset.IndexBuffer = reinterpret_cast<FRHIBuffer*>(
            static_cast<std::uintptr_t>(0x1100));
        MeshAsset.IndexCount = 3;
        MeshAsset.IndexFormat = EPixelFormat::R32_UINT;
        MeshAsset.LocalBounds = FRenderBounds(
            Math::Vector3f(-1.0f, -2.0f, -0.5f),
            Math::Vector3f(1.0f, 2.0f, 0.5f));
        MaterialAssets[0].GraphicsPipeline = reinterpret_cast<FRHIPipeline_Graphics*>(
            static_cast<std::uintptr_t>(0x2000));
        MaterialAssets[0].PassMask = ERenderMeshPassMask::SceneColor;
        MaterialAssets[0].BaseColor = Math::Vector4f(1.0f, 0.5f, 0.25f, 0.0f);
        MaterialAssets[1].GraphicsPipeline = reinterpret_cast<FRHIPipeline_Graphics*>(
            static_cast<std::uintptr_t>(0x3000));
        MaterialAssets[1].PassMask = ERenderMeshPassMask::SceneColor;
        MaterialAssets[1].BaseColor = Math::Vector4f(2.0f, 1.0f, 0.5f, 7.0f);

        TAssetRegistration<FMeshAsset> MeshRegistration;
        MeshRegistration.Id = MeshId;
        MeshRegistration.SoftPath = "Tests/WorldRenderer/Triangle.mesh";
        MeshRegistration.DisplayName = "TriangleMesh";
        MeshRegistration.ProviderName = "BuilderTests";
        MeshRegistration.Object = &MeshAsset;
        TAssetRegistration<FMaterialAsset> MaterialRegistration;
        MaterialRegistration.Id = MaterialId;
        MaterialRegistration.SoftPath = "Tests/WorldRenderer/Triangle.material";
        MaterialRegistration.DisplayName = "TriangleMaterial";
        MaterialRegistration.ProviderName = "BuilderTests";
        MaterialRegistration.Object = &MaterialAssets[0];
        if (Assets.Register(MeshRegistration, MeshHandle) !=
                EAssetRegisterResult::Success ||
            Assets.Register(MaterialRegistration, MaterialHandle) !=
                EAssetRegisterResult::Success)
        {
            return false;
        }

        FTransform Transform;
        Transform.Translation = Math::Vector3f(4.0f, 5.0f, 6.0f);
        Transform.Rotation = Math::Quaternionf(
            Math::Vector3f(0.0f, 0.0f, 1.0f), 1.57079632679489661923f);
        Transform.Scale = Math::Vector3f(2.0f, 3.0f, 4.0f);
        FWorldMeshComponent Component;
        Component.DebugName = "SnapshotTriangle";
        Component.Mesh = MeshHandle;
        Component.Material = MaterialHandle;
        Component.SortKey = 41;
        MeshStorage = World.GetMeshComponents();
        return TryAddTestCamera(
                World, CameraEntity, Math::Vector3f(4.0f, 5.0f, 20.0f)) &&
            MeshStorage != nullptr && World.TryCreateEntity(Entity) &&
            World.TryAddTransform(Entity, Transform) == EComponentMutationResult::Added &&
            World.TryAddComponent(*MeshStorage, Entity, std::move(Component)) ==
                EComponentMutationResult::Added;
    }

    FAssetRegistry Assets;
    FWorld World;
    FMeshAsset MeshAsset;
    FMaterialAsset MaterialAssets[2];
    FAssetId MeshId;
    FAssetId MaterialId;
    FMeshAssetHandle MeshHandle;
    FMaterialAssetHandle MaterialHandle;
    TComponentStorage<FWorldMeshComponent>* MeshStorage = nullptr;
    FEntity CameraEntity;
    FEntity Entity;
};

int RunSnapshotSuccessAndIdentityTests()
{
    int Failures = 0;
    FWorldRendererFixture Fixture;
    const bool bInitialized = Fixture.Initialize();
    FWorldRenderer Renderer;
    FWorldRenderSnapshot Snapshot;
    FWorldRenderSnapshotBuildReport Report;
    const EWorldRenderSnapshotBuildResult Result = Renderer.BuildSnapshot(
        Fixture.World, Fixture.Assets, MakeBuildContext(17), Snapshot, &Report);
    const FRenderScene& Scene = Snapshot.GetRenderScene();
    Failures += Expect(bInitialized &&
        Result == EWorldRenderSnapshotBuildResult::Success &&
        Report.Result == EWorldRenderSnapshotBuildResult::Success &&
        Snapshot.IsPublished() && Snapshot.GetFrameIndex() == 17 &&
        Scene.Meshes.Size() == 1 && Scene.Cameras.Size() == 1 &&
        Report.SourceMeshCount == 1 && Report.VisibleMeshCount == 1 &&
        Report.CulledMeshCount == 0,
        "WorldRenderer publishes one complete frame-owned snapshot");
    if (!Scene.Meshes.IsEmpty())
    {
        const FRenderMeshProxy& Proxy = Scene.Meshes[0];
        const uint64 ExpectedValue =
            (static_cast<uint64>(Fixture.Entity.GetGeneration()) << 32) |
            static_cast<uint64>(Fixture.Entity.GetIndex());
        Failures += Expect(Proxy.PrimitiveId.Scope == Fixture.Entity.WorldDomain &&
            Proxy.PrimitiveId.Value == ExpectedValue &&
            Proxy.DebugName == StringView("SnapshotTriangle") &&
            Proxy.VertexBuffer == Fixture.MeshAsset.VertexBuffer &&
            Proxy.IndexBuffer == Fixture.MeshAsset.IndexBuffer &&
            Proxy.GraphicsPipeline == Fixture.MaterialAssets[0].GraphicsPipeline &&
            Proxy.VertexCount == 3 && Proxy.IndexCount == 3 &&
            Proxy.IndexFormat == EPixelFormat::R32_UINT &&
            Proxy.PassMask == ERenderMeshPassMask::SceneColor && Proxy.SortKey == 41 &&
            Proxy.MaterialAssetId == Fixture.MaterialId &&
            Proxy.BaseColor == Fixture.MaterialAssets[0].BaseColor,
            "snapshot owns metadata and copies resolved Renderer resources");
        Failures += Expect(HasBounds(
            Proxy.WorldBounds,
            Math::Vector3f(-2.0f, 3.0f, 4.0f),
            Math::Vector3f(10.0f, 7.0f, 8.0f)),
            "world AABB handles rotation and non-uniform scale");
    }
    if (!Scene.Cameras.IsEmpty())
    {
        const FRenderCameraProxy& Camera = Scene.Cameras[0];
        Failures += Expect(Camera.DebugName == StringView("TestCamera") &&
            Camera.PrimitiveId.Scope == Fixture.CameraEntity.WorldDomain &&
            Camera.ViewportWidth == 1280 && Camera.ViewportHeight == 720 &&
            NearlyEqual(Camera.View(0, 3), -4.0f) &&
            NearlyEqual(Camera.View(1, 3), -5.0f) &&
            NearlyEqual(Camera.View(2, 3), -20.0f) &&
            Camera.Projection(1, 1) < 0.0f,
            "snapshot publishes the selected camera matrices and Vulkan Y flip");
    }

    FWorld IdentityWorld;
    FEntity IdentityEntity;
    FEntity IdentityCamera;
    TComponentStorage<FWorldMeshComponent>* const IdentityMeshes =
        IdentityWorld.GetMeshComponents();
    FWorldMeshComponent IdentityComponent;
    IdentityComponent.DebugName = "IdentityMesh";
    IdentityComponent.Mesh = Fixture.MeshHandle;
    IdentityComponent.Material = Fixture.MaterialHandle;
    const bool bIdentityReady = TryAddTestCamera(IdentityWorld, IdentityCamera) &&
        IdentityMeshes != nullptr &&
        IdentityWorld.TryCreateEntity(IdentityEntity) &&
        IdentityWorld.TryAddComponent(
            *IdentityMeshes, IdentityEntity, std::move(IdentityComponent)) ==
            EComponentMutationResult::Added;
    FWorldRenderSnapshot IdentitySnapshot;
    Failures += Expect(bIdentityReady && Renderer.BuildSnapshot(
            IdentityWorld, Fixture.Assets, MakeBuildContext(18), IdentitySnapshot) ==
            EWorldRenderSnapshotBuildResult::Success &&
        IdentitySnapshot.GetRenderScene().Meshes.Size() == 1 &&
        HasBounds(
            IdentitySnapshot.GetRenderScene().Meshes[0].WorldBounds,
            Fixture.MeshAsset.LocalBounds.Min,
            Fixture.MeshAsset.LocalBounds.Max),
        "mesh without Transform uses a validated identity transform");
    return Failures;
}

int RunSnapshotStabilityAndReplacementTests()
{
    int Failures = 0;
    FWorldRendererFixture Fixture;
    const bool bInitialized = Fixture.Initialize();
    FWorldRenderer Renderer;
    FWorldRenderSnapshot BeforeReplace;
    const bool bFirstBuilt = Renderer.BuildSnapshot(
        Fixture.World, Fixture.Assets, MakeBuildContext(20), BeforeReplace) ==
        EWorldRenderSnapshotBuildResult::Success;
    const FRenderPrimitiveId StableId = bFirstBuilt
        ? BeforeReplace.GetRenderScene().Meshes[0].PrimitiveId
        : FRenderPrimitiveId{};
    FRHIPipeline_Graphics* const OldPipeline = bFirstBuilt
        ? BeforeReplace.GetRenderScene().Meshes[0].GraphicsPipeline
        : nullptr;
    FRHIBuffer* const OldIndexBuffer = bFirstBuilt
        ? BeforeReplace.GetRenderScene().Meshes[0].IndexBuffer
        : nullptr;

    TAssetRegistration<FMaterialAsset> Replacement;
    Replacement.Id = Fixture.MaterialId;
    Replacement.SoftPath = "Tests/WorldRenderer/Triangle.material";
    Replacement.DisplayName = "TriangleMaterial";
    Replacement.ProviderName = "BuilderTests";
    Replacement.Object = &Fixture.MaterialAssets[1];
    const FMaterialAssetHandle OldHandle = Fixture.MaterialHandle;
    FMaterialAssetHandle NewHandle;
    const bool bReplaced = Fixture.Assets.Replace(
        OldHandle, Replacement, NewHandle) == EAssetReplaceResult::Success;
    FWorldMeshComponent* const Component = Fixture.MeshStorage == nullptr
        ? nullptr : Fixture.World.GetComponent(*Fixture.MeshStorage, Fixture.Entity);
    if (bReplaced && Component != nullptr)
    {
        Component->Material = NewHandle;
        Fixture.MaterialHandle = NewHandle;
        Component->DebugName = "ChangedAfterSnapshot";
    }

    FMeshAsset ReplacementMesh = Fixture.MeshAsset;
    ReplacementMesh.IndexBuffer = reinterpret_cast<FRHIBuffer*>(
        static_cast<std::uintptr_t>(0x1200));
    ReplacementMesh.IndexCount = 6;
    ReplacementMesh.IndexFormat = EPixelFormat::R16_UINT;
    TAssetRegistration<FMeshAsset> MeshReplacement;
    MeshReplacement.Id = Fixture.MeshId;
    MeshReplacement.SoftPath = "Tests/WorldRenderer/Triangle.mesh";
    MeshReplacement.DisplayName = "TriangleMesh";
    MeshReplacement.ProviderName = "BuilderTests";
    MeshReplacement.Object = &ReplacementMesh;
    const FMeshAssetHandle OldMeshHandle = Fixture.MeshHandle;
    FMeshAssetHandle NewMeshHandle;
    const bool bMeshReplaced = Fixture.Assets.Replace(
        OldMeshHandle, MeshReplacement, NewMeshHandle) == EAssetReplaceResult::Success;
    if (bMeshReplaced && Component != nullptr)
    {
        Component->Mesh = NewMeshHandle;
        Fixture.MeshHandle = NewMeshHandle;
    }

    FWorldRenderSnapshot AfterReplace;
    const bool bSecondBuilt = Renderer.BuildSnapshot(
        Fixture.World, Fixture.Assets, MakeBuildContext(21), AfterReplace) ==
        EWorldRenderSnapshotBuildResult::Success;
    const FRenderScene& OldScene = BeforeReplace.GetRenderScene();
    const FRenderScene& NewScene = AfterReplace.GetRenderScene();
    const FMaterialAsset* Resolved = nullptr;
    const FMeshAsset* ResolvedMesh = nullptr;
    Failures += Expect(bInitialized && bFirstBuilt && bReplaced && bMeshReplaced &&
        Component != nullptr && bSecondBuilt &&
        Fixture.Assets.Resolve(OldHandle, Resolved) == EAssetResolveResult::StaleHandle &&
        Fixture.Assets.Resolve(OldMeshHandle, ResolvedMesh) == EAssetResolveResult::StaleHandle &&
        OldScene.Meshes[0].GraphicsPipeline == OldPipeline &&
        OldScene.Meshes[0].IndexBuffer == OldIndexBuffer &&
        OldScene.Meshes[0].IndexCount == 3 &&
        OldScene.Meshes[0].IndexFormat == EPixelFormat::R32_UINT &&
        OldScene.Meshes[0].DebugName == StringView("SnapshotTriangle") &&
        NewScene.Meshes[0].GraphicsPipeline ==
            Fixture.MaterialAssets[1].GraphicsPipeline &&
        NewScene.Meshes[0].IndexBuffer == ReplacementMesh.IndexBuffer &&
        NewScene.Meshes[0].IndexCount == 6 &&
        NewScene.Meshes[0].IndexFormat == EPixelFormat::R16_UINT &&
        OldScene.Meshes[0].MaterialAssetId == Fixture.MaterialId &&
        NewScene.Meshes[0].MaterialAssetId == Fixture.MaterialId &&
        NewScene.Meshes[0].DebugName == StringView("ChangedAfterSnapshot") &&
        NewScene.Meshes[0].PrimitiveId == StableId,
        "published snapshot owns CPU values and the next build re-resolves Replace");

    FTransform Changed;
    Changed.Translation = Math::Vector3f(9.0f, 0.0f, 0.0f);
    const bool bTransformChanged = Fixture.World.SetLocalTransform(
        Fixture.Entity, Changed) == ETransformUpdateResult::Updated;
    FWorldRenderSnapshot AfterTransform;
    Failures += Expect(bTransformChanged && Renderer.BuildSnapshot(
            Fixture.World, Fixture.Assets, MakeBuildContext(22), AfterTransform) ==
            EWorldRenderSnapshotBuildResult::Success &&
        AfterTransform.GetRenderScene().Meshes[0].PrimitiveId == StableId,
        "primitive ID is stable across committed transform changes");

    const bool bDestroyed = Fixture.World.DestroyEntity(Fixture.Entity);
    FEntity ReplacementEntity;
    FWorldMeshComponent ReplacementComponent;
    ReplacementComponent.Mesh = Fixture.MeshHandle;
    ReplacementComponent.Material = Fixture.MaterialHandle;
    const bool bRecreated = bDestroyed && Fixture.World.TryCreateEntity(ReplacementEntity) &&
        Fixture.World.TryAddComponent(
            *Fixture.MeshStorage, ReplacementEntity, std::move(ReplacementComponent)) ==
            EComponentMutationResult::Added;
    FWorldRenderSnapshot Recreated;
    Failures += Expect(bRecreated && Renderer.BuildSnapshot(
            Fixture.World, Fixture.Assets, MakeBuildContext(23), Recreated) ==
            EWorldRenderSnapshotBuildResult::Success &&
        Recreated.GetRenderScene().Meshes[0].PrimitiveId != StableId,
        "destroy and slot reuse produce a distinct primitive generation");

    FWorld OtherWorld;
    FEntity OtherEntity;
    FEntity OtherCamera;
    TComponentStorage<FWorldMeshComponent>* const OtherMeshes =
        OtherWorld.GetMeshComponents();
    FWorldMeshComponent OtherComponent;
    OtherComponent.Mesh = Fixture.MeshHandle;
    OtherComponent.Material = Fixture.MaterialHandle;
    const bool bOtherReady = TryAddTestCamera(OtherWorld, OtherCamera) &&
        OtherMeshes != nullptr && OtherWorld.TryCreateEntity(OtherEntity) &&
        OtherWorld.TryAddComponent(*OtherMeshes, OtherEntity, std::move(OtherComponent)) ==
            EComponentMutationResult::Added;
    FWorldRenderSnapshot OtherSnapshot;
    Failures += Expect(bOtherReady && Renderer.BuildSnapshot(
            OtherWorld, Fixture.Assets, MakeBuildContext(24), OtherSnapshot) ==
            EWorldRenderSnapshotBuildResult::Success &&
        OtherSnapshot.GetRenderScene().Meshes[0].PrimitiveId.Scope !=
            Recreated.GetRenderScene().Meshes[0].PrimitiveId.Scope,
        "equal entity slots from different Worlds retain distinct primitive scopes");
    return Failures;
}

int RunSnapshotFailureTests()
{
    int Failures = 0;
    FWorldRendererFixture Fixture;
    const bool bInitialized = Fixture.Initialize();
    FWorldRenderer Renderer;
    FWorldRenderSnapshot Snapshot;
    const bool bPublished = Renderer.BuildSnapshot(
        Fixture.World, Fixture.Assets, MakeBuildContext(30), Snapshot) ==
        EWorldRenderSnapshotBuildResult::Success;

    FWorldMeshComponent* const Component = Fixture.MeshStorage == nullptr
        ? nullptr : Fixture.World.GetComponent(*Fixture.MeshStorage, Fixture.Entity);
    const FMeshAssetHandle CurrentMesh = Fixture.MeshHandle;
    if (Component != nullptr)
    {
        Component->Mesh.Reset();
    }
    FWorldRenderSnapshotBuildReport Report;
    Failures += Expect(bInitialized && bPublished && Component != nullptr &&
        Renderer.BuildSnapshot(Fixture.World, Fixture.Assets, MakeBuildContext(31), Snapshot, &Report) ==
            EWorldRenderSnapshotBuildResult::MeshResolveFailed &&
        Report.AssetResolveResult == EAssetResolveResult::InvalidHandle &&
        !Snapshot.IsPublished() && Snapshot.GetFrameIndex() == 0 &&
        Snapshot.GetRenderScene().Meshes.IsEmpty() &&
        Snapshot.GetRenderScene().Cameras.IsEmpty(),
        "any failure unpublishes and clears a previously valid snapshot");

    FAssetRegistry ForeignAssets;
    FAssetId ForeignMeshId;
    FAssetId::TryParse("a2300000-0000-4000-8000-000000000003", ForeignMeshId);
    FMeshAsset ForeignMesh = Fixture.MeshAsset;
    TAssetRegistration<FMeshAsset> ForeignRegistration;
    ForeignRegistration.Id = ForeignMeshId;
    ForeignRegistration.SoftPath = "Tests/WorldRenderer/Foreign.mesh";
    ForeignRegistration.DisplayName = "ForeignMesh";
    ForeignRegistration.ProviderName = "BuilderTests";
    ForeignRegistration.Object = &ForeignMesh;
    FMeshAssetHandle ForeignHandle;
    const bool bForeignRegistered = ForeignAssets.Register(
        ForeignRegistration, ForeignHandle) == EAssetRegisterResult::Success;
    Component->Mesh = ForeignHandle;
    Failures += Expect(bForeignRegistered && Renderer.BuildSnapshot(
            Fixture.World, Fixture.Assets, MakeBuildContext(32), Snapshot, &Report) ==
            EWorldRenderSnapshotBuildResult::MeshResolveFailed &&
        Report.AssetResolveResult == EAssetResolveResult::ForeignRegistry &&
        !Snapshot.IsPublished(),
        "foreign registry handles fail without exposing a partial snapshot");

    FMeshAssetHandle TypeMismatched;
    static_assert(sizeof(TypeMismatched) == sizeof(Fixture.MaterialHandle));
    std::memcpy(&TypeMismatched, &Fixture.MaterialHandle, sizeof(TypeMismatched));
    Component->Mesh = TypeMismatched;
    Failures += Expect(Renderer.BuildSnapshot(
            Fixture.World, Fixture.Assets, MakeBuildContext(33), Snapshot, &Report) ==
            EWorldRenderSnapshotBuildResult::MeshResolveFailed &&
        Report.AssetResolveResult == EAssetResolveResult::TypeMismatch,
        "type-mismatched handle bytes never expose the wrong payload");

    Component->Mesh = CurrentMesh;
    FWorldTickContext Tick;
    Tick.FrameIndex = 34;
    const bool bTicking = Fixture.World.BeginTick(Tick) == EWorldTickResult::Success;
    Failures += Expect(bTicking && Renderer.BuildSnapshot(
            Fixture.World, Fixture.Assets, MakeBuildContext(34), Snapshot, &Report) ==
            EWorldRenderSnapshotBuildResult::WorldNotCommitted &&
        !Snapshot.IsPublished(),
        "WorldRenderer rejects an Updating World instead of sampling an open tick");
    if (bTicking)
    {
        static_cast<void>(Fixture.World.EndTick());
    }

    FTransform InvalidTransform;
    InvalidTransform.Translation.Values[0] =
        (std::numeric_limits<float>::quiet_NaN)();
    const bool bInvalidTransformSet = Fixture.World.SetLocalTransform(
        Fixture.Entity, InvalidTransform) == ETransformUpdateResult::Updated;
    Failures += Expect(bInvalidTransformSet && Renderer.BuildSnapshot(
            Fixture.World, Fixture.Assets, MakeBuildContext(35), Snapshot, &Report) ==
            EWorldRenderSnapshotBuildResult::InvalidTransform &&
        !Snapshot.IsPublished(),
        "non-finite transforms cannot publish non-finite bounds");

    FSnapshotFailingAllocator InvalidRegistryAllocator;
    InvalidRegistryAllocator.ResetAttempts(1);
    FAssetRegistry InvalidRegistry(InvalidRegistryAllocator);
    Failures += Expect(!InvalidRegistry.IsValid() && Renderer.BuildSnapshot(
            Fixture.World, InvalidRegistry, MakeBuildContext(36), Snapshot, &Report) ==
            EWorldRenderSnapshotBuildResult::InvalidRegistry,
        "invalid registry is rejected before asset resolution");
    return Failures;
}

int RunSnapshotBoundsAndOomTests()
{
    int Failures = 0;
    FWorldRendererFixture Fixture;
    const bool bInitialized = Fixture.Initialize();
    FWorldRenderer Renderer;
    FWorldMeshComponent* const Component = Fixture.MeshStorage == nullptr
        ? nullptr : Fixture.World.GetComponent(*Fixture.MeshStorage, Fixture.Entity);

    FMeshAsset InvalidBoundsMesh = Fixture.MeshAsset;
    FAssetId InvalidMeshId;
    FAssetId::TryParse("a2300000-0000-4000-8000-000000000004", InvalidMeshId);
    TAssetRegistration<FMeshAsset> InvalidRegistration;
    InvalidRegistration.Id = InvalidMeshId;
    InvalidRegistration.SoftPath = "Tests/WorldRenderer/InvalidBounds.mesh";
    InvalidRegistration.DisplayName = "InvalidBoundsMesh";
    InvalidRegistration.ProviderName = "BuilderTests";
    InvalidRegistration.Object = &InvalidBoundsMesh;
    FMeshAssetHandle InvalidBoundsHandle;
    InvalidBoundsMesh.LocalBounds = FRenderBounds();
    const bool bInvalidRegistered = Fixture.Assets.Register(
        InvalidRegistration, InvalidBoundsHandle) == EAssetRegisterResult::Success;
    if (Component != nullptr)
    {
        Component->Mesh = InvalidBoundsHandle;
    }
    FWorldRenderSnapshot Snapshot;
    Failures += Expect(bInitialized && Component != nullptr && bInvalidRegistered &&
        Renderer.BuildSnapshot(Fixture.World, Fixture.Assets, MakeBuildContext(40), Snapshot) ==
            EWorldRenderSnapshotBuildResult::InvalidBounds &&
        !Snapshot.IsPublished() && Snapshot.GetRenderScene().Meshes.IsEmpty(),
        "default invalid local bounds are rejected before publication");
    Component->Mesh = Fixture.MeshHandle;

    std::size_t FailedAllocationPoints = 0;
    std::size_t SuccessfulAllocationCount = 0;
    bool bReachedSuccess = false;
    for (std::size_t FailOnAttempt = 1; FailOnAttempt <= 16; ++FailOnAttempt)
    {
        FSnapshotFailingAllocator Allocator;
        {
            FWorldRenderSnapshot FailingSnapshot(Allocator);
            Allocator.ResetAttempts(FailOnAttempt);
            const EWorldRenderSnapshotBuildResult Result = Renderer.BuildSnapshot(
                Fixture.World, Fixture.Assets, MakeBuildContext(41 + FailOnAttempt), FailingSnapshot);
            if (Result == EWorldRenderSnapshotBuildResult::Success)
            {
                bReachedSuccess = true;
                SuccessfulAllocationCount = Allocator.AllocationAttempts;
                Failures += Expect(FailingSnapshot.IsPublished() &&
                    FailingSnapshot.GetRenderScene().Meshes.Size() == 1,
                    "first non-failing allocation point publishes the full snapshot");
                break;
            }
            ++FailedAllocationPoints;
            Failures += Expect(
                Result == EWorldRenderSnapshotBuildResult::AllocationFailed &&
                Allocator.LiveAllocations == 0 && !FailingSnapshot.IsPublished() &&
                FailingSnapshot.GetRenderScene().Meshes.IsEmpty() &&
                FailingSnapshot.GetRenderScene().Cameras.IsEmpty(),
                "every snapshot allocation failure cleans and publishes nothing");
        }
        Failures += Expect(Allocator.LiveAllocations == 0,
            "failed snapshot destruction leaves its allocator balanced");
    }
    Failures += Expect(bReachedSuccess && FailedAllocationPoints >= 4 &&
        SuccessfulAllocationCount >= 4,
        "OOM sweep covers camera and mesh array/name allocation points");
    return Failures;
}

int RunSnapshotMoveTests()
{
    int Failures = 0;
    FWorldRendererFixture Fixture;
    const bool bInitialized = Fixture.Initialize();
    FWorldRenderer Renderer;
    FSnapshotFailingAllocator SourceAllocator;
    FSnapshotFailingAllocator DestinationAllocator;
    {
        FWorldRenderSnapshot Source(SourceAllocator);
        const bool bSourceBuilt = Renderer.BuildSnapshot(
            Fixture.World, Fixture.Assets, MakeBuildContext(60), Source) ==
            EWorldRenderSnapshotBuildResult::Success;
        FWorldRenderSnapshot Constructed(std::move(Source));
        const bool bConstructedTransferred = bSourceBuilt && Constructed.IsPublished() &&
            Constructed.GetFrameIndex() == 60 &&
            Constructed.GetRenderScene().Meshes.Size() == 1 &&
            &Constructed.GetRenderScene().GetAllocator() == &SourceAllocator &&
            !Source.IsPublished() && Source.GetFrameIndex() == 0 &&
            Source.GetRenderScene().Meshes.IsEmpty() &&
            &Source.GetRenderScene().GetAllocator() == &SourceAllocator;
        const bool bSourceRebuilt = Renderer.BuildSnapshot(
            Fixture.World, Fixture.Assets, MakeBuildContext(61), Source) ==
            EWorldRenderSnapshotBuildResult::Success;

        FWorldRenderSnapshot Destination(DestinationAllocator);
        const bool bDestinationBuilt = Renderer.BuildSnapshot(
            Fixture.World, Fixture.Assets, MakeBuildContext(62), Destination) ==
            EWorldRenderSnapshotBuildResult::Success;
        Destination = std::move(Constructed);
        const bool bAssignedTransferred = bDestinationBuilt &&
            Destination.IsPublished() && Destination.GetFrameIndex() == 60 &&
            Destination.GetRenderScene().Meshes.Size() == 1 &&
            &Destination.GetRenderScene().GetAllocator() == &SourceAllocator &&
            DestinationAllocator.LiveAllocations == 0 &&
            !Constructed.IsPublished() && Constructed.GetFrameIndex() == 0 &&
            Constructed.GetRenderScene().Meshes.IsEmpty() &&
            &Constructed.GetRenderScene().GetAllocator() == &SourceAllocator;
        const bool bConstructedRebuilt = Renderer.BuildSnapshot(
            Fixture.World, Fixture.Assets, MakeBuildContext(63), Constructed) ==
            EWorldRenderSnapshotBuildResult::Success;
        Failures += Expect(bInitialized && bConstructedTransferred && bSourceRebuilt &&
            bAssignedTransferred && bConstructedRebuilt &&
            Source.GetFrameIndex() == 61 && Constructed.GetFrameIndex() == 63,
            "snapshot move transfers publication and allocator ownership; moved-from rebuilds");
    }
    Failures += Expect(SourceAllocator.LiveAllocations == 0 &&
        DestinationAllocator.LiveAllocations == 0,
        "moved snapshots deallocate through their exact allocator owners");
    return Failures;
}

int RunSnapshotPayloadAndStaleTests()
{
    int Failures = 0;
    FWorldRendererFixture Fixture;
    const bool bInitialized = Fixture.Initialize();
    FWorldRenderer Renderer;
    FWorldMeshComponent* const Component = Fixture.MeshStorage == nullptr
        ? nullptr : Fixture.World.GetComponent(*Fixture.MeshStorage, Fixture.Entity);

    FEntity SecondEntity;
    FWorldMeshComponent SecondComponent;
    SecondComponent.DebugName = "FailsAfterFirstCandidate";
    SecondComponent.Mesh = Fixture.MeshHandle;
    const bool bSecondReady = Fixture.World.TryCreateEntity(SecondEntity) &&
        Fixture.World.TryAddComponent(
            *Fixture.MeshStorage, SecondEntity, std::move(SecondComponent)) ==
            EComponentMutationResult::Added;
    FWorldRenderSnapshot Snapshot;
    FWorldRenderSnapshotBuildReport Report;
    Failures += Expect(bInitialized && Component != nullptr && bSecondReady &&
        Renderer.BuildSnapshot(Fixture.World, Fixture.Assets, MakeBuildContext(50), Snapshot, &Report) ==
            EWorldRenderSnapshotBuildResult::MaterialResolveFailed &&
        Report.MeshIndex == 1 &&
        Report.AssetResolveResult == EAssetResolveResult::InvalidHandle &&
        Snapshot.GetRenderScene().Meshes.IsEmpty() && !Snapshot.IsPublished(),
        "a later primitive failure discards the already-built candidate prefix");
    static_cast<void>(Fixture.World.DestroyEntity(SecondEntity));

    FMeshAsset InvalidMesh;
    InvalidMesh.LocalBounds = Fixture.MeshAsset.LocalBounds;
    TAssetRegistration<FMeshAsset> MeshReplacement;
    MeshReplacement.Id = Fixture.MeshId;
    MeshReplacement.SoftPath = "Tests/WorldRenderer/Triangle.mesh";
    MeshReplacement.DisplayName = "TriangleMesh";
    MeshReplacement.ProviderName = "BuilderTests";
    MeshReplacement.Object = &InvalidMesh;
    FMeshAssetHandle InvalidMeshHandle;
    const bool bInvalidMeshReplaced = Fixture.Assets.Replace(
        Fixture.MeshHandle, MeshReplacement, InvalidMeshHandle) ==
        EAssetReplaceResult::Success;
    if (bInvalidMeshReplaced)
    {
        Component->Mesh = InvalidMeshHandle;
        Fixture.MeshHandle = InvalidMeshHandle;
    }
    Failures += Expect(bInvalidMeshReplaced && Renderer.BuildSnapshot(
            Fixture.World, Fixture.Assets, MakeBuildContext(51), Snapshot) ==
            EWorldRenderSnapshotBuildResult::InvalidMeshPayload &&
        !Snapshot.IsPublished(),
        "null mesh GPU payload is rejected before publication");

    FMeshAsset NullVertexMesh = Fixture.MeshAsset;
    NullVertexMesh.VertexBuffer = nullptr;
    MeshReplacement.Object = &NullVertexMesh;
    FMeshAssetHandle NullVertexHandle;
    const bool bNullVertexReplaced = Fixture.Assets.Replace(
        Fixture.MeshHandle, MeshReplacement, NullVertexHandle) ==
        EAssetReplaceResult::Success;
    if (bNullVertexReplaced)
    {
        Component->Mesh = NullVertexHandle;
        Fixture.MeshHandle = NullVertexHandle;
    }
    Failures += Expect(bNullVertexReplaced && Renderer.BuildSnapshot(
            Fixture.World, Fixture.Assets, MakeBuildContext(510), Snapshot) ==
            EWorldRenderSnapshotBuildResult::InvalidMeshPayload &&
        !Snapshot.IsPublished(),
        "null vertex buffer is rejected before publication");

    FMeshAsset ZeroVertexMesh = Fixture.MeshAsset;
    ZeroVertexMesh.VertexCount = 0;
    MeshReplacement.Object = &ZeroVertexMesh;
    FMeshAssetHandle ZeroVertexHandle;
    const bool bZeroVertexReplaced = Fixture.Assets.Replace(
        Fixture.MeshHandle, MeshReplacement, ZeroVertexHandle) ==
        EAssetReplaceResult::Success;
    if (bZeroVertexReplaced)
    {
        Component->Mesh = ZeroVertexHandle;
        Fixture.MeshHandle = ZeroVertexHandle;
    }
    Failures += Expect(bZeroVertexReplaced && Renderer.BuildSnapshot(
            Fixture.World, Fixture.Assets, MakeBuildContext(5101), Snapshot) ==
            EWorldRenderSnapshotBuildResult::InvalidMeshPayload &&
        !Snapshot.IsPublished(),
        "zero vertex count is rejected before publication");

    FMeshAsset NullIndexMesh = Fixture.MeshAsset;
    NullIndexMesh.IndexBuffer = nullptr;
    MeshReplacement.Object = &NullIndexMesh;
    FMeshAssetHandle NullIndexHandle;
    const bool bNullIndexReplaced = Fixture.Assets.Replace(
        Fixture.MeshHandle, MeshReplacement, NullIndexHandle) ==
        EAssetReplaceResult::Success;
    if (bNullIndexReplaced)
    {
        Component->Mesh = NullIndexHandle;
        Fixture.MeshHandle = NullIndexHandle;
    }
    Failures += Expect(bNullIndexReplaced && Renderer.BuildSnapshot(
            Fixture.World, Fixture.Assets, MakeBuildContext(511), Snapshot) ==
            EWorldRenderSnapshotBuildResult::InvalidMeshPayload &&
        !Snapshot.IsPublished(),
        "null index buffer is rejected before publication");

    FMeshAsset ZeroIndexMesh = Fixture.MeshAsset;
    ZeroIndexMesh.IndexCount = 0;
    MeshReplacement.Object = &ZeroIndexMesh;
    FMeshAssetHandle ZeroIndexHandle;
    const bool bZeroIndexReplaced = Fixture.Assets.Replace(
        Fixture.MeshHandle, MeshReplacement, ZeroIndexHandle) ==
        EAssetReplaceResult::Success;
    if (bZeroIndexReplaced)
    {
        Component->Mesh = ZeroIndexHandle;
        Fixture.MeshHandle = ZeroIndexHandle;
    }
    Failures += Expect(bZeroIndexReplaced && Renderer.BuildSnapshot(
            Fixture.World, Fixture.Assets, MakeBuildContext(512), Snapshot) ==
            EWorldRenderSnapshotBuildResult::InvalidMeshPayload &&
        !Snapshot.IsPublished(),
        "zero index count is rejected before publication");

    FMeshAsset InvalidIndexFormatMesh = Fixture.MeshAsset;
    InvalidIndexFormatMesh.IndexFormat = EPixelFormat::R8_UINT;
    MeshReplacement.Object = &InvalidIndexFormatMesh;
    FMeshAssetHandle InvalidIndexFormatHandle;
    const bool bInvalidIndexFormatReplaced = Fixture.Assets.Replace(
        Fixture.MeshHandle, MeshReplacement, InvalidIndexFormatHandle) ==
        EAssetReplaceResult::Success;
    if (bInvalidIndexFormatReplaced)
    {
        Component->Mesh = InvalidIndexFormatHandle;
        Fixture.MeshHandle = InvalidIndexFormatHandle;
    }
    Failures += Expect(bInvalidIndexFormatReplaced && Renderer.BuildSnapshot(
            Fixture.World, Fixture.Assets, MakeBuildContext(513), Snapshot) ==
            EWorldRenderSnapshotBuildResult::InvalidMeshPayload &&
        !Snapshot.IsPublished(),
        "non-index RHI format is rejected before publication");

    MeshReplacement.Object = &Fixture.MeshAsset;
    FMeshAssetHandle RestoredMeshHandle;
    const bool bMeshRestored = Fixture.Assets.Replace(
        Fixture.MeshHandle, MeshReplacement, RestoredMeshHandle) ==
        EAssetReplaceResult::Success;
    if (bMeshRestored)
    {
        Component->Mesh = RestoredMeshHandle;
        Fixture.MeshHandle = RestoredMeshHandle;
    }

    FMaterialAsset InvalidMaterial;
    TAssetRegistration<FMaterialAsset> MaterialReplacement;
    MaterialReplacement.Id = Fixture.MaterialId;
    MaterialReplacement.SoftPath = "Tests/WorldRenderer/Triangle.material";
    MaterialReplacement.DisplayName = "TriangleMaterial";
    MaterialReplacement.ProviderName = "BuilderTests";
    MaterialReplacement.Object = &InvalidMaterial;
    FMaterialAssetHandle InvalidMaterialHandle;
    const bool bInvalidMaterialReplaced = Fixture.Assets.Replace(
        Fixture.MaterialHandle, MaterialReplacement, InvalidMaterialHandle) ==
        EAssetReplaceResult::Success;
    if (bInvalidMaterialReplaced)
    {
        Component->Material = InvalidMaterialHandle;
        Fixture.MaterialHandle = InvalidMaterialHandle;
    }
    Failures += Expect(bMeshRestored && bInvalidMaterialReplaced &&
        Renderer.BuildSnapshot(Fixture.World, Fixture.Assets, MakeBuildContext(52), Snapshot) ==
            EWorldRenderSnapshotBuildResult::InvalidMaterialPayload &&
        !Snapshot.IsPublished(),
        "null material GPU payload is rejected before publication");

    FMaterialAsset InvalidColorMaterial;
    InvalidColorMaterial.GraphicsPipeline = Fixture.MaterialAssets[0].GraphicsPipeline;
    InvalidColorMaterial.PassMask = ERenderMeshPassMask::SceneColor;
    InvalidColorMaterial.BaseColor = Math::Vector4f(-1.0f, 1.0f, 1.0f, 1.0f);
    MaterialReplacement.Object = &InvalidColorMaterial;
    FMaterialAssetHandle InvalidColorHandle;
    const bool bInvalidColorReplaced = Fixture.Assets.Replace(
        Fixture.MaterialHandle, MaterialReplacement, InvalidColorHandle) ==
        EAssetReplaceResult::Success;
    if (bInvalidColorReplaced)
    {
        Component->Material = InvalidColorHandle;
        Fixture.MaterialHandle = InvalidColorHandle;
    }
    Failures += Expect(bInvalidColorReplaced && Renderer.BuildSnapshot(
            Fixture.World, Fixture.Assets, MakeBuildContext(521), Snapshot) ==
            EWorldRenderSnapshotBuildResult::InvalidMaterialPayload &&
        !Snapshot.IsPublished(),
        "negative opaque material RGB is rejected while HDR positive values remain valid");

    MaterialReplacement.Object = &Fixture.MaterialAssets[1];
    FMaterialAssetHandle RestoredMaterialHandle;
    const bool bMaterialRestored = Fixture.Assets.Replace(
        Fixture.MaterialHandle, MaterialReplacement, RestoredMaterialHandle) ==
        EAssetReplaceResult::Success;
    if (bMaterialRestored)
    {
        Component->Material = RestoredMaterialHandle;
        Fixture.MaterialHandle = RestoredMaterialHandle;
    }
    const FMeshAssetHandle StaleMeshHandle = Fixture.MeshHandle;
    const bool bMeshUnregistered = Fixture.Assets.Unregister(StaleMeshHandle) ==
        EAssetUnregisterResult::Success;
    Failures += Expect(bMaterialRestored && bMeshUnregistered &&
        Renderer.BuildSnapshot(Fixture.World, Fixture.Assets, MakeBuildContext(53), Snapshot, &Report) ==
            EWorldRenderSnapshotBuildResult::MeshResolveFailed &&
        Report.AssetResolveResult == EAssetResolveResult::StaleHandle &&
        Snapshot.GetRenderScene().Meshes.IsEmpty(),
        "unregistered mesh produces a stale-handle failure without dereference");
    return Failures;
}

int RunCameraContractTests()
{
    int Failures = 0;
    FWorldRendererFixture Fixture;
    const bool bInitialized = Fixture.Initialize();
    FWorldRenderer Renderer;
    FWorldRenderSnapshot Snapshot;
    FWorldRenderSnapshotBuildReport Report;

    FWorldRenderSnapshotBuildContext InvalidViewport = MakeBuildContext(70);
    InvalidViewport.ViewportWidth = 0;
    Failures += Expect(bInitialized && Renderer.BuildSnapshot(
            Fixture.World, Fixture.Assets, InvalidViewport, Snapshot, &Report) ==
            EWorldRenderSnapshotBuildResult::InvalidViewport &&
        !Snapshot.IsPublished(),
        "zero viewport extent is rejected before camera extraction");

    FWorld MissingCameraWorld;
    Failures += Expect(Renderer.BuildSnapshot(
            MissingCameraWorld, Fixture.Assets, MakeBuildContext(71), Snapshot, &Report) ==
            EWorldRenderSnapshotBuildResult::MissingCamera &&
        !Snapshot.IsPublished(),
        "a committed World without an active camera cannot publish");

    FWorld MissingTransformWorld;
    FEntity MissingTransformCamera;
    TComponentStorage<FWorldCameraComponent>* const MissingTransformCameras =
        MissingTransformWorld.GetCameraComponents();
    FWorldCameraComponent MissingTransformComponent;
    const bool bMissingTransformReady = MissingTransformCameras != nullptr &&
        MissingTransformWorld.TryCreateEntity(MissingTransformCamera) &&
        MissingTransformWorld.TryAddComponent(
            *MissingTransformCameras,
            MissingTransformCamera,
            std::move(MissingTransformComponent)) == EComponentMutationResult::Added;
    Failures += Expect(bMissingTransformReady && Renderer.BuildSnapshot(
            MissingTransformWorld,
            Fixture.Assets,
            MakeBuildContext(72),
            Snapshot,
            &Report) == EWorldRenderSnapshotBuildResult::InvalidCameraTransform,
        "the selected camera must have a committed Transform");

    TComponentStorage<FWorldCameraComponent>* const Cameras =
        Fixture.World.GetCameraComponents();
    FEntity HigherPriorityCamera;
    FEntity EqualPriorityLaterCamera;
    const bool bSelectionReady = Cameras != nullptr &&
        TryAddTestCamera(
            Fixture.World,
            HigherPriorityCamera,
            Math::Vector3f(0.0f, 0.0f, 15.0f),
            5,
            "HigherPriority") &&
        TryAddTestCamera(
            Fixture.World,
            EqualPriorityLaterCamera,
            Math::Vector3f(0.0f, 0.0f, 25.0f),
            5,
            "EqualPriorityLater");
    const uint64 ExpectedCameraValue =
        (static_cast<uint64>(HigherPriorityCamera.GetGeneration()) << 32) |
        static_cast<uint64>(HigherPriorityCamera.GetIndex());
    Failures += Expect(bSelectionReady && Renderer.BuildSnapshot(
            Fixture.World, Fixture.Assets, MakeBuildContext(73), Snapshot, &Report) ==
            EWorldRenderSnapshotBuildResult::Success &&
        Snapshot.GetRenderScene().Cameras.Size() == 1 &&
        Snapshot.GetRenderScene().Cameras[0].PrimitiveId.Value == ExpectedCameraValue &&
        Snapshot.GetRenderScene().Cameras[0].DebugName == StringView("HigherPriority"),
        "active camera selection is priority-descending then primitive-ID ascending");

    const FRenderCameraProxy& SelectedCamera = Snapshot.GetRenderScene().Cameras[0];
    const FWorldCameraComponent* const SelectedWorldCamera =
        Fixture.World.GetComponent(*Cameras, HigherPriorityCamera);
    const float NearPlane = SelectedWorldCamera == nullptr ? 0.1f : SelectedWorldCamera->NearPlane;
    const float FarPlane = SelectedWorldCamera == nullptr ? 1000.0f : SelectedWorldCamera->FarPlane;
    const Math::Vector4f NearClip = SelectedCamera.Projection *
        Math::Vector4f(0.0f, 0.0f, -NearPlane, 1.0f);
    const Math::Vector4f FarClip = SelectedCamera.Projection *
        Math::Vector4f(0.0f, 0.0f, -FarPlane, 1.0f);
    Failures += Expect(NearlyEqual(NearClip.Values[2] / NearClip.Values[3], 0.0f) &&
        NearlyEqual(FarClip.Values[2] / FarClip.Values[3], 1.0f) &&
        SelectedCamera.Projection(1, 1) < 0.0f,
        "projection maps the RH -Z near/far planes to Vulkan depth zero/one with Y flip");

    FWorldCameraComponent* const MutableCamera =
        Fixture.World.GetComponent(*Cameras, HigherPriorityCamera);
    const float SavedFar = MutableCamera == nullptr ? 0.0f : MutableCamera->FarPlane;
    if (MutableCamera != nullptr)
    {
        MutableCamera->FarPlane = MutableCamera->NearPlane;
    }
    Failures += Expect(MutableCamera != nullptr && Renderer.BuildSnapshot(
            Fixture.World, Fixture.Assets, MakeBuildContext(74), Snapshot, &Report) ==
            EWorldRenderSnapshotBuildResult::InvalidCamera &&
        !Snapshot.IsPublished(),
        "invalid FOV/near/far camera parameters cannot publish");
    if (MutableCamera != nullptr)
    {
        MutableCamera->FarPlane = SavedFar;
    }
    const float SavedFov = MutableCamera == nullptr
        ? 0.0f : MutableCamera->VerticalFieldOfViewRadians;
    if (MutableCamera != nullptr)
    {
        MutableCamera->VerticalFieldOfViewRadians =
            (std::numeric_limits<float>::denorm_min)();
    }
    Failures += Expect(MutableCamera != nullptr && Renderer.BuildSnapshot(
            Fixture.World, Fixture.Assets, MakeBuildContext(741), Snapshot, &Report) ==
            EWorldRenderSnapshotBuildResult::InvalidCamera &&
        !Snapshot.IsPublished() && Snapshot.GetRenderScene().Cameras.IsEmpty(),
        "finite but numerically unconstructable projection is an invalid camera");
    if (MutableCamera != nullptr)
    {
        MutableCamera->VerticalFieldOfViewRadians = SavedFov;
    }
    FTransform ScaledCamera;
    ScaledCamera.Translation = Math::Vector3f(0.0f, 0.0f, 15.0f);
    ScaledCamera.Scale = Math::Vector3f(2.0f, 1.0f, 1.0f);
    const bool bScaled = Fixture.World.SetLocalTransform(
        HigherPriorityCamera, ScaledCamera) == ETransformUpdateResult::Updated;
    Failures += Expect(bScaled && Renderer.BuildSnapshot(
            Fixture.World, Fixture.Assets, MakeBuildContext(75), Snapshot, &Report) ==
            EWorldRenderSnapshotBuildResult::InvalidCameraTransform &&
        !Snapshot.IsPublished(),
        "non-rigid or non-unit camera bases are rejected");
    return Failures;
}

int RunFrustumCullingAndSortTests()
{
    int Failures = 0;
    FWorldRendererFixture Fixture;
    const bool bInitialized = Fixture.Initialize();
    FWorldRenderer Renderer;
    TComponentStorage<FWorldCameraComponent>* const Cameras =
        Fixture.World.GetCameraComponents();
    FWorldCameraComponent* const Camera = Cameras == nullptr
        ? nullptr : Fixture.World.GetComponent(*Cameras, Fixture.CameraEntity);
    if (Camera != nullptr)
    {
        Camera->VerticalFieldOfViewRadians = 1.5707963268f;
        Camera->NearPlane = 1.0f;
        Camera->FarPlane = 10.0f;
    }
    FTransform CameraTransform;
    const bool bCameraMoved = Fixture.World.SetLocalTransform(
        Fixture.CameraEntity, CameraTransform) == ETransformUpdateResult::Updated;
    FTransform InsideTransform;
    InsideTransform.Translation = Math::Vector3f(0.0f, 0.0f, -5.0f);
    InsideTransform.Scale = Math::Vector3f(0.5f);
    const bool bInsideMoved = Fixture.World.SetLocalTransform(
        Fixture.Entity, InsideTransform) == ETransformUpdateResult::Updated;
    FWorldMeshComponent* const InsideComponent = Fixture.MeshStorage == nullptr
        ? nullptr : Fixture.World.GetComponent(*Fixture.MeshStorage, Fixture.Entity);
    if (InsideComponent != nullptr)
    {
        InsideComponent->SortKey = 9;
    }

    FAssetId LowerMaterialId;
    FAssetId HigherMaterialId;
    FMaterialAsset LowerMaterial = Fixture.MaterialAssets[0];
    FMaterialAsset HigherMaterial = Fixture.MaterialAssets[0];
    FMaterialAssetHandle LowerMaterialHandle;
    FMaterialAssetHandle HigherMaterialHandle;
    TAssetRegistration<FMaterialAsset> LowerRegistration;
    TAssetRegistration<FMaterialAsset> HigherRegistration;
    const bool bMaterialIdsReady = FAssetId::TryParse(
            "a2300000-0000-4000-8000-000000000010", LowerMaterialId) &&
        FAssetId::TryParse(
            "a2300000-0000-4000-8000-000000000020", HigherMaterialId);
    LowerRegistration.Id = LowerMaterialId;
    LowerRegistration.SoftPath = "Tests/WorldRenderer/Lower.material";
    LowerRegistration.DisplayName = "LowerMaterial";
    LowerRegistration.ProviderName = "BuilderTests";
    LowerRegistration.Object = &LowerMaterial;
    HigherRegistration.Id = HigherMaterialId;
    HigherRegistration.SoftPath = "Tests/WorldRenderer/Higher.material";
    HigherRegistration.DisplayName = "HigherMaterial";
    HigherRegistration.ProviderName = "BuilderTests";
    HigherRegistration.Object = &HigherMaterial;
    const bool bMaterialsReady = bMaterialIdsReady &&
        Fixture.Assets.Register(LowerRegistration, LowerMaterialHandle) ==
            EAssetRegisterResult::Success &&
        Fixture.Assets.Register(HigherRegistration, HigherMaterialHandle) ==
            EAssetRegisterResult::Success;

    FEntity OutsideEntity;
    FEntity IntersectEntity;
    FEntity EnclosingEntity;
    FEntity HigherMaterialEntity;
    FEntity LowerMaterialEntity;
    const bool bEntitiesReady = bMaterialsReady &&
        Fixture.World.TryCreateEntity(OutsideEntity) &&
        Fixture.World.TryCreateEntity(IntersectEntity) &&
        Fixture.World.TryCreateEntity(EnclosingEntity) &&
        Fixture.World.TryCreateEntity(HigherMaterialEntity) &&
        Fixture.World.TryCreateEntity(LowerMaterialEntity);
    FTransform OutsideTransform;
    OutsideTransform.Translation = Math::Vector3f(20.0f, 0.0f, -5.0f);
    OutsideTransform.Scale = Math::Vector3f(0.5f);
    FTransform IntersectTransform;
    IntersectTransform.Translation = Math::Vector3f(5.0f, 0.0f, -5.0f);
    IntersectTransform.Scale = Math::Vector3f(0.5f);
    FTransform EnclosingTransform;
    EnclosingTransform.Translation = Math::Vector3f(0.0f, 0.0f, -5.0f);
    EnclosingTransform.Scale = Math::Vector3f(20.0f);
    const bool bTransformsReady = bEntitiesReady &&
        Fixture.World.TryAddTransform(OutsideEntity, OutsideTransform) ==
            EComponentMutationResult::Added &&
        Fixture.World.TryAddTransform(IntersectEntity, IntersectTransform) ==
            EComponentMutationResult::Added &&
        Fixture.World.TryAddTransform(EnclosingEntity, EnclosingTransform) ==
            EComponentMutationResult::Added &&
        Fixture.World.TryAddTransform(HigherMaterialEntity, InsideTransform) ==
            EComponentMutationResult::Added &&
        Fixture.World.TryAddTransform(LowerMaterialEntity, InsideTransform) ==
            EComponentMutationResult::Added;

    auto MakeMesh = [&Fixture](const char* const Name, const uint64 SortKey)
    {
        FWorldMeshComponent Component;
        Component.DebugName = Name;
        Component.Mesh = Fixture.MeshHandle;
        Component.Material = Fixture.MaterialHandle;
        Component.SortKey = SortKey;
        return Component;
    };
    // Add the equal-key components in reverse Entity order. The published order
    // must still follow the primitive-ID tie-break, not dense storage order.
    const bool bComponentsReady = bTransformsReady &&
        Fixture.World.TryAddComponent(
            *Fixture.MeshStorage, OutsideEntity, MakeMesh("Outside", 1)) ==
            EComponentMutationResult::Added &&
        Fixture.World.TryAddComponent(
            *Fixture.MeshStorage, EnclosingEntity, MakeMesh("Enclosing", 3)) ==
            EComponentMutationResult::Added &&
        Fixture.World.TryAddComponent(
            *Fixture.MeshStorage, IntersectEntity, MakeMesh("Intersect", 3)) ==
            EComponentMutationResult::Added;
    FWorldMeshComponent HigherMaterialMesh = MakeMesh("HigherMaterialId", 2);
    HigherMaterialMesh.Material = HigherMaterialHandle;
    FWorldMeshComponent LowerMaterialMesh = MakeMesh("LowerMaterialId", 2);
    LowerMaterialMesh.Material = LowerMaterialHandle;
    // Higher persistent ID has the lower PrimitiveId and is inserted first;
    // material identity must therefore decide the opposite published order.
    const bool bMaterialComponentsReady = bComponentsReady &&
        Fixture.World.TryAddComponent(
            *Fixture.MeshStorage,
            HigherMaterialEntity,
            std::move(HigherMaterialMesh)) == EComponentMutationResult::Added &&
        Fixture.World.TryAddComponent(
            *Fixture.MeshStorage,
            LowerMaterialEntity,
            std::move(LowerMaterialMesh)) == EComponentMutationResult::Added;

    FWorldRenderSnapshot Snapshot;
    FWorldRenderSnapshotBuildReport Report;
    const bool bBuilt = Renderer.BuildSnapshot(
        Fixture.World,
        Fixture.Assets,
        MakeBuildContext(80, 100, 100),
        Snapshot,
        &Report) == EWorldRenderSnapshotBuildResult::Success;
    const FRenderScene& Scene = Snapshot.GetRenderScene();
    Failures += Expect(bInitialized && Camera != nullptr && bCameraMoved && bInsideMoved &&
        bMaterialComponentsReady && bBuilt && Report.SourceMeshCount == 6 &&
        Report.VisibleMeshCount == 5 && Report.CulledMeshCount == 1 &&
        Scene.Meshes.Size() == 5 &&
        Scene.Meshes[0].DebugName == StringView("LowerMaterialId") &&
        Scene.Meshes[0].MaterialAssetId == LowerMaterialId &&
        Scene.Meshes[1].DebugName == StringView("HigherMaterialId") &&
        Scene.Meshes[1].MaterialAssetId == HigherMaterialId &&
        Scene.Meshes[2].DebugName == StringView("Intersect") &&
        Scene.Meshes[3].DebugName == StringView("Enclosing") &&
        Scene.Meshes[4].DebugName == StringView("SnapshotTriangle"),
        "frustum and total sort honor material identity before primitive/dense order");

    FTransform Hidden;
    Hidden.Translation = Math::Vector3f(100.0f, 0.0f, -5.0f);
    Hidden.Scale = Math::Vector3f(0.5f);
    const bool bAllMoved = Fixture.World.SetLocalTransform(Fixture.Entity, Hidden) ==
            ETransformUpdateResult::Updated &&
        Fixture.World.SetLocalTransform(IntersectEntity, Hidden) ==
            ETransformUpdateResult::Updated &&
        Fixture.World.SetLocalTransform(EnclosingEntity, Hidden) ==
            ETransformUpdateResult::Updated &&
        Fixture.World.SetLocalTransform(HigherMaterialEntity, Hidden) ==
            ETransformUpdateResult::Updated &&
        Fixture.World.SetLocalTransform(LowerMaterialEntity, Hidden) ==
            ETransformUpdateResult::Updated;
    FWorldRenderSnapshot EmptyVisible;
    Failures += Expect(bAllMoved && Renderer.BuildSnapshot(
            Fixture.World,
            Fixture.Assets,
            MakeBuildContext(81, 100, 100),
            EmptyVisible,
            &Report) == EWorldRenderSnapshotBuildResult::Success &&
        EmptyVisible.IsPublished() && EmptyVisible.GetRenderScene().Cameras.Size() == 1 &&
        EmptyVisible.GetRenderScene().Meshes.IsEmpty() &&
        Report.SourceMeshCount == 6 && Report.VisibleMeshCount == 0 &&
        Report.CulledMeshCount == 6,
        "zero visible meshes remains a valid camera-only published snapshot");
    return Failures;
}

int RunInvalidClipCoordinateTests()
{
    int Failures = 0;
    FWorldRendererFixture Fixture;
    const bool bInitialized = Fixture.Initialize();
    FTransform CameraTransform;
    FTransform OverflowingClipTransform;
    OverflowingClipTransform.Translation = Math::Vector3f(0.0f, 0.0f, -5.0f);
    // The resulting world AABB remains finite (Y extent 2e38), while the
    // finite projection Y scale makes clip-space multiplication overflow.
    OverflowingClipTransform.Scale = Math::Vector3f(1.0f, 1.0e38f, 1.0f);
    const bool bTransformsReady = bInitialized &&
        Fixture.World.SetLocalTransform(Fixture.CameraEntity, CameraTransform) ==
            ETransformUpdateResult::Updated &&
        Fixture.World.SetLocalTransform(Fixture.Entity, OverflowingClipTransform) ==
            ETransformUpdateResult::Updated;
    FWorldRenderer Renderer;
    FWorldRenderSnapshot Snapshot;
    FWorldRenderSnapshotBuildReport Report;
    Failures += Expect(bTransformsReady && Renderer.BuildSnapshot(
            Fixture.World,
            Fixture.Assets,
            MakeBuildContext(90, 100, 100),
            Snapshot,
            &Report) == EWorldRenderSnapshotBuildResult::InvalidClipCoordinates &&
        !Snapshot.IsPublished() && Snapshot.GetRenderScene().Meshes.IsEmpty() &&
        Snapshot.GetRenderScene().Cameras.IsEmpty(),
        "finite world bounds that overflow clip multiplication fail transactionally");
    return Failures;
}

} // namespace

int RunWorldRendererTests()
{
    static_assert(!std::is_copy_constructible<FWorldRenderSnapshot>::value);
    static_assert(!std::is_copy_assignable<FWorldRenderSnapshot>::value);
    static_assert(std::is_nothrow_move_constructible<FWorldRenderSnapshot>::value);
    static_assert(std::is_nothrow_move_assignable<FWorldRenderSnapshot>::value);
    static_assert(sizeof(FRenderPrimitiveId) == sizeof(uint64) * 2);

    int Failures = 0;
    Failures += RunSnapshotSuccessAndIdentityTests();
    Failures += RunSnapshotStabilityAndReplacementTests();
    Failures += RunSnapshotFailureTests();
    Failures += RunSnapshotBoundsAndOomTests();
    Failures += RunSnapshotPayloadAndStaleTests();
    Failures += RunSnapshotMoveTests();
    Failures += RunCameraContractTests();
    Failures += RunFrustumCullingAndSortTests();
    Failures += RunInvalidClipCoordinateTests();
    return Failures;
}

} // namespace LE
