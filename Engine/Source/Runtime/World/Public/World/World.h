#pragma once

#include "Asset/RenderAssetTypes.h"
#include "CoreMinimal.h"
#include "Reflection/ReflectionMacros.h"
#include "World/ComponentStorage.h"
#include "World/NativeGameplaySystem.h"
#include "World/Transform.h"

namespace LE
{

class FWorldCommandBuffer;
struct FWorldCommandFlushResult;

struct FWorldPrimitive
{
    LE::String DebugName;
};

struct FWorldMeshComponent : public FWorldPrimitive
{
    FMeshAssetHandle Mesh;
    FMaterialAssetHandle Material;
    uint64 SortKey = 0;
};

// Renderer-neutral authored camera data. Camera pose is supplied by the
// FTransform attached to the same Entity; viewport-dependent matrices are
// derived downstream by WorldRenderer.
struct FWorldCameraComponent : public FWorldPrimitive
{
    float VerticalFieldOfViewRadians = 1.0471975512f;
    float NearPlane = 0.1f;
    float FarPlane = 1000.0f;
    int32 Priority = 0;
    bool bActive = true;
};

struct FWorldTickContext
{
    uint64 FrameIndex = 0;
    double DeltaSeconds = 0.0;
    double ElapsedSeconds = 0.0;
};

enum class EWorldTickResult : uint8
{
    Success = 0,
    CompletedWithCommandFailures,
    InvalidWorld,
    InvalidContext,
    InvalidState,
    TransformUpdateFailed,
};

enum class EWorldTickState : uint8
{
    Idle = 0,
    Updating,
    Flushing,
};

LE_CLASS("50000000-0000-4000-8000-000000000001")
class WORLD_API FWorld
{
    LE_GENERATED_BODY()

public:
    FWorld() noexcept;
    explicit FWorld(IAllocator& Allocator) noexcept;
    ~FWorld() noexcept;

    FWorld(const FWorld&) = delete;
    FWorld& operator=(const FWorld&) = delete;
    FWorld(FWorld&&) = delete;
    FWorld& operator=(FWorld&&) = delete;

    bool IsValid() const noexcept;
    bool TryCreateEntity(FEntity& OutEntity) noexcept;
    bool DestroyEntity(FEntity Entity) noexcept;
    bool IsAlive(FEntity Entity) const noexcept;
    bool Reset() noexcept;

    EWorldTickResult BeginTick(
        const FWorldTickContext& Context,
        FWorldCommandFlushResult* OutFlushResult = nullptr) noexcept;
    EWorldTickResult EndTick(
        FWorldCommandFlushResult* OutFlushResult = nullptr) noexcept;
    EWorldTickState GetTickState() const noexcept;
    bool TryGetTickContext(FWorldTickContext& OutContext) const noexcept;
    FWorldCommandBuffer* GetCommandBuffer() noexcept;
    const FWorldCommandBuffer* GetCommandBuffer() const noexcept;

    ENativeGameplaySystemRegisterResult RegisterNativeGameplaySystem(
        const FNativeGameplaySystemDescriptor& Descriptor) noexcept;
    ENativeGameplaySystemUnregisterResult UnregisterNativeGameplaySystem(
        StringView Name) noexcept;
    ENativeGameplaySystemEnableResult SetNativeGameplaySystemEnabled(
        StringView Name,
        bool bEnabled) noexcept;
    ENativeGameplayFixedStepConfigResult ConfigureNativeGameplayFixedStep(
        const FNativeGameplayFixedStepConfig& Config) noexcept;
    ENativeGameplayScheduleResult CompileNativeGameplaySystemSchedule() noexcept;
    ENativeGameplayUpdateResult UpdateNativeGameplaySystems(
        FNativeGameplayUpdateReport* OutReport = nullptr) noexcept;
    // Borrowed registry views. A successful register/unregister or World
    // destruction invalidates them.
    StringView GetNativeGameplayDiagnosticSystemName() const noexcept;
    StringView GetNativeGameplayDiagnosticDependencyName() const noexcept;

    std::size_t GetEntityCount() const noexcept;

    EComponentMutationResult TryAddTransform(
        FEntity Entity,
        const FTransform& LocalTransform = FTransform{}) noexcept;
    ETransformUpdateResult SetLocalTransform(
        FEntity Entity,
        const FTransform& LocalTransform) noexcept;
    ETransformParentResult SetTransformParent(FEntity Child, FEntity Parent) noexcept;
    ETransformParentResult ClearTransformParent(FEntity Child) noexcept;
    EComponentRemoveResult RemoveTransform(FEntity Entity) noexcept;

    bool TryGetLocalTransform(FEntity Entity, FTransform& OutTransform) const noexcept;
    bool TryGetTransformParent(FEntity Entity, FEntity& OutParent) const noexcept;
    bool HasTransform(FEntity Entity) const noexcept;
    bool TryGetWorldMatrix(FEntity Entity, Math::Matrix4f& OutMatrix) const noexcept;
    bool UpdateTransforms() const noexcept;
    uint64 GetTransformMutationVersion() const noexcept;

    TComponentStorage<FWorldMeshComponent>* GetMeshComponents() noexcept;
    const TComponentStorage<FWorldMeshComponent>* GetMeshComponents() const noexcept;
    TComponentStorage<FWorldCameraComponent>* GetCameraComponents() noexcept;
    const TComponentStorage<FWorldCameraComponent>* GetCameraComponents() const noexcept;

    template <typename T>
    bool AttachComponentStorage(TComponentStorage<T>& Storage) noexcept
    {
        return AttachStorage(Storage);
    }

    template <typename T>
    EComponentMutationResult TryAddComponent(
        TComponentStorage<T>& Storage,
        const FEntity Entity,
        const T& Value) noexcept
    {
        if (!IsStorageUsable(Storage))
        {
            return EComponentMutationResult::InvalidStorage;
        }
        if (!IsAlive(Entity))
        {
            return EComponentMutationResult::InvalidEntity;
        }
        if (!BeginStructuralMutation())
        {
            return EComponentMutationResult::InvalidStorage;
        }
        const EComponentMutationResult Result = Storage.TryAdd(Entity, Value);
        EndStructuralMutation();
        return Result;
    }

    template <typename T>
    EComponentMutationResult TryAddComponent(
        TComponentStorage<T>& Storage,
        const FEntity Entity,
        T&& Value) noexcept
    {
        if (!IsStorageUsable(Storage))
        {
            return EComponentMutationResult::InvalidStorage;
        }
        if (!IsAlive(Entity))
        {
            return EComponentMutationResult::InvalidEntity;
        }
        if (!BeginStructuralMutation())
        {
            return EComponentMutationResult::InvalidStorage;
        }
        const EComponentMutationResult Result = Storage.TryAdd(Entity, std::move(Value));
        EndStructuralMutation();
        return Result;
    }

    template <typename T>
    EComponentMutationResult TrySetComponent(
        TComponentStorage<T>& Storage,
        const FEntity Entity,
        const T& Value) noexcept
    {
        if (!IsStorageUsable(Storage))
        {
            return EComponentMutationResult::InvalidStorage;
        }
        if (!IsAlive(Entity))
        {
            return EComponentMutationResult::InvalidEntity;
        }
        if (!BeginStructuralMutation())
        {
            return EComponentMutationResult::InvalidStorage;
        }
        const EComponentMutationResult Result = Storage.TrySet(Entity, Value);
        EndStructuralMutation();
        return Result;
    }

    template <typename T>
    EComponentMutationResult TrySetComponent(
        TComponentStorage<T>& Storage,
        const FEntity Entity,
        T&& Value) noexcept
    {
        if (!IsStorageUsable(Storage))
        {
            return EComponentMutationResult::InvalidStorage;
        }
        if (!IsAlive(Entity))
        {
            return EComponentMutationResult::InvalidEntity;
        }
        if (!BeginStructuralMutation())
        {
            return EComponentMutationResult::InvalidStorage;
        }
        const EComponentMutationResult Result = Storage.TrySet(Entity, std::move(Value));
        EndStructuralMutation();
        return Result;
    }

    template <typename T>
    T* GetComponent(TComponentStorage<T>& Storage, const FEntity Entity) noexcept
    {
        return IsStorageUsable(Storage) && IsAlive(Entity) ? Storage.Find(Entity) : nullptr;
    }

    template <typename T>
    const T* GetComponent(
        const TComponentStorage<T>& Storage,
        const FEntity Entity) const noexcept
    {
        return IsStorageUsable(Storage) && IsAlive(Entity) ? Storage.Find(Entity) : nullptr;
    }

    template <typename T>
    EComponentRemoveResult RemoveComponent(
        TComponentStorage<T>& Storage,
        const FEntity Entity) noexcept
    {
        if (!IsStorageUsable(Storage))
        {
            return EComponentRemoveResult::InvalidStorage;
        }
        if (!IsAlive(Entity))
        {
            return EComponentRemoveResult::InvalidEntity;
        }
        if (!BeginStructuralMutation())
        {
            return EComponentRemoveResult::InvalidStorage;
        }
        const bool bRemoved = Storage.Remove(Entity);
        EndStructuralMutation();
        return bRemoved ? EComponentRemoveResult::Removed : EComponentRemoveResult::NotPresent;
    }

private:
    friend class FComponentStorageNode;
    friend class FWorldCommandBuffer;
    friend void Detail::DetachComponentStorage(FComponentStorageNode& Storage) noexcept;

    struct FImpl;

    bool AttachStorage(FComponentStorageNode& Storage) noexcept;
    void DetachStorage(FComponentStorageNode& Storage) noexcept;
    bool IsStorageUsable(const FComponentStorageNode& Storage) const noexcept;
    bool BeginStructuralMutation() noexcept;
    void EndStructuralMutation() noexcept;
    void BeginCommandExecution() noexcept;
    void EndCommandExecution() noexcept;
    void BeginCommandPayloadLifecycle() noexcept;
    void EndCommandPayloadLifecycle() noexcept;
    uint64 GetRuntimeDomain() const noexcept;

    bool RemoveTransformDuringStructuralMutation(FEntity Entity) noexcept;
    void MarkTransformSubtreeDirty(FEntity Root) noexcept;
    FEntity NextTransformInSubtree(FEntity Current, FEntity Root) const noexcept;
    bool WouldCreateTransformCycle(FEntity Child, FEntity Parent) const noexcept;

    FImpl* Impl = nullptr;
};

} // namespace LE

#include "World/WorldCommandBuffer.h"
