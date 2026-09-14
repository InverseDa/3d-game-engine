#pragma once

#include "World/World.h"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace LE
{

struct FDeferredEntitySlotTag;
using FDeferredEntitySlotHandle = TRuntimeHandle<FDeferredEntitySlotTag>;

class FDeferredEntityToken
{
public:
    bool IsValid() const noexcept { return Slot.IsValid() && WorldDomain != 0; }
    void Reset() noexcept
    {
        Slot.Reset();
        WorldDomain = 0;
    }

    friend bool operator==(const FDeferredEntityToken& Left,
        const FDeferredEntityToken& Right) noexcept
    {
        return Left.Slot == Right.Slot && Left.WorldDomain == Right.WorldDomain;
    }

    friend bool operator!=(const FDeferredEntityToken& Left,
        const FDeferredEntityToken& Right) noexcept
    {
        return !(Left == Right);
    }

private:
    friend class FWorldCommandBuffer;
    FDeferredEntitySlotHandle Slot;
    uint64 WorldDomain = 0;
};

class FWorldCommandEntityTarget
{
public:
    FWorldCommandEntityTarget() noexcept = default;
    FWorldCommandEntityTarget(const FEntity Entity) noexcept : EntityValue(Entity) {}
    FWorldCommandEntityTarget(const FDeferredEntityToken Token) noexcept
        : TokenValue(Token), bDeferred(true) {}

private:
    friend class FWorldCommandBuffer;
    FEntity EntityValue;
    FDeferredEntityToken TokenValue;
    bool bDeferred = false;
};

enum class EWorldCommandRecordResult : uint8
{
    Recorded = 0,
    InvalidWorld,
    InvalidState,
    InvalidEntity,
    InvalidStorage,
    InvalidToken,
    AliasedPayload,
    AllocationFailed,
};

enum class EWorldCommandFailureReason : uint8
{
    None = 0,
    InvalidEntity,
    InvalidStorage,
    InvalidToken,
    DependencyFailed,
    AllocationFailed,
    AlreadyPresent,
    MissingComponent,
    CycleRejected,
    ReentrantMutation,
};

struct FWorldCommandFlushResult
{
    static constexpr std::size_t InvalidCommandIndex = static_cast<std::size_t>(-1);
    std::size_t ExecutedCount = 0;
    std::size_t SucceededCount = 0;
    std::size_t FailedCount = 0;
    std::size_t FirstFailureIndex = InvalidCommandIndex;
    EWorldCommandFailureReason FirstFailureReason = EWorldCommandFailureReason::None;

    bool Succeeded() const noexcept { return FailedCount == 0; }
};

enum class EDeferredEntityResolveResult : uint8
{
    Resolved = 0,
    Pending,
    CreationFailed,
    NoLongerAlive,
    InvalidToken,
    InvalidState,
};

class WORLD_API FWorldCommandBuffer final
{
public:
    FWorldCommandBuffer(const FWorldCommandBuffer&) = delete;
    FWorldCommandBuffer& operator=(const FWorldCommandBuffer&) = delete;
    FWorldCommandBuffer(FWorldCommandBuffer&&) = delete;
    FWorldCommandBuffer& operator=(FWorldCommandBuffer&&) = delete;

    EWorldCommandRecordResult TryCreateEntity(FDeferredEntityToken& OutToken) noexcept;
    EWorldCommandRecordResult TryDestroyEntity(FWorldCommandEntityTarget Target) noexcept;
    EWorldCommandRecordResult TryAddTransform(
        FWorldCommandEntityTarget Target,
        const FTransform& Transform = FTransform{}) noexcept;
    EWorldCommandRecordResult TrySetLocalTransform(
        FWorldCommandEntityTarget Target,
        const FTransform& Transform) noexcept;
    EWorldCommandRecordResult TrySetTransformParent(
        FWorldCommandEntityTarget Child,
        FWorldCommandEntityTarget Parent) noexcept;
    EWorldCommandRecordResult TryClearTransformParent(FWorldCommandEntityTarget Child) noexcept;
    EWorldCommandRecordResult TryRemoveTransform(FWorldCommandEntityTarget Target) noexcept;

    EDeferredEntityResolveResult TryResolveDeferredEntity(
        FDeferredEntityToken Token,
        FEntity& OutEntity) noexcept;
    bool DiscardDeferredEntityToken(FDeferredEntityToken Token) noexcept;
    std::size_t GetPendingCommandCount() const noexcept;

    template <typename T>
    EWorldCommandRecordResult TryAddComponent(
        TComponentStorage<T>& Storage,
        FWorldCommandEntityTarget Target,
        const T& Value) noexcept
    {
        static_assert(std::is_nothrow_copy_constructible<T>::value,
            "deferred component copy payload must be nothrow copy constructible");
        return TryRecordComponentValue<T, EComponentCommandOperation::Add>(Storage, Target, Value);
    }

    template <typename T>
    EWorldCommandRecordResult TryAddComponent(
        TComponentStorage<T>& Storage,
        FWorldCommandEntityTarget Target,
        T&& Value) noexcept
    {
        return TryRecordComponentValue<T, EComponentCommandOperation::Add>(
            Storage, Target, std::move(Value));
    }

    template <typename T>
    EWorldCommandRecordResult TrySetComponent(
        TComponentStorage<T>& Storage,
        FWorldCommandEntityTarget Target,
        const T& Value) noexcept
    {
        static_assert(std::is_nothrow_copy_constructible<T>::value,
            "deferred component copy payload must be nothrow copy constructible");
        return TryRecordComponentValue<T, EComponentCommandOperation::Set>(Storage, Target, Value);
    }

    template <typename T>
    EWorldCommandRecordResult TrySetComponent(
        TComponentStorage<T>& Storage,
        FWorldCommandEntityTarget Target,
        T&& Value) noexcept
    {
        return TryRecordComponentValue<T, EComponentCommandOperation::Set>(
            Storage, Target, std::move(Value));
    }

    template <typename T>
    EWorldCommandRecordResult TryRemoveComponent(
        TComponentStorage<T>& Storage,
        FWorldCommandEntityTarget Target) noexcept
    {
        using FPayload = TRemoveComponentPayload<T>;
        FCommandReservation Reservation;
        const EWorldCommandRecordResult Result = BeginRecord(
            Target, Storage, sizeof(FPayload), alignof(FPayload),
            &ExecuteRemoveComponent<T>, &DestroyPayload<FPayload>, Reservation);
        if (Result != EWorldCommandRecordResult::Recorded) return Result;
        new (Reservation.Payload) FPayload{&Storage, Target};
        CommitRecord(Reservation);
        return EWorldCommandRecordResult::Recorded;
    }

private:
    friend class FWorld;

    enum class EComponentCommandOperation : uint8 { Add, Set };
    using FExecuteThunk = EWorldCommandFailureReason (*)(FWorldCommandBuffer&, void*) noexcept;
    using FDestroyThunk = void (*)(void*) noexcept;

    struct FCommandReservation { void* Node = nullptr; void* Payload = nullptr; };
    struct FImpl;

    template <typename T, EComponentCommandOperation Operation>
    struct TComponentValuePayload
    {
        TComponentStorage<T>* Storage = nullptr;
        FWorldCommandEntityTarget Target;
        T Value;
    };

    template <typename T>
    struct TRemoveComponentPayload
    {
        TComponentStorage<T>* Storage = nullptr;
        FWorldCommandEntityTarget Target;
    };

    FWorldCommandBuffer(FWorld& Owner, IAllocator& Allocator) noexcept;
    ~FWorldCommandBuffer() noexcept;
    bool IsValid() const noexcept;
    bool CanRecord() const noexcept;
    EWorldCommandRecordResult BeginRecord(
        FWorldCommandEntityTarget Target,
        const FComponentStorageNode& Storage,
        std::size_t PayloadSize,
        std::size_t PayloadAlignment,
        FExecuteThunk Execute,
        FDestroyThunk Destroy,
        FCommandReservation& OutReservation) noexcept;
    EWorldCommandRecordResult BeginRecord(
        FWorldCommandEntityTarget Target,
        std::size_t PayloadSize,
        std::size_t PayloadAlignment,
        FExecuteThunk Execute,
        FDestroyThunk Destroy,
        FCommandReservation& OutReservation) noexcept;
    EWorldCommandRecordResult BeginRecordUnvalidated(
        std::size_t PayloadSize,
        std::size_t PayloadAlignment,
        FExecuteThunk Execute,
        FDestroyThunk Destroy,
        FCommandReservation& OutReservation) noexcept;
    void CommitRecord(FCommandReservation& Reservation) noexcept;
    bool ValidateTargetForRecord(FWorldCommandEntityTarget Target) const noexcept;
    EWorldCommandFailureReason ResolveTarget(
        FWorldCommandEntityTarget Target,
        FEntity& OutEntity) noexcept;
    FWorldCommandFlushResult Flush() noexcept;
    void CancelAll() noexcept;

    template <typename Payload>
    static void DestroyPayload(void* Address) noexcept
    {
        static_cast<Payload*>(Address)->~Payload();
    }

    template <typename T, EComponentCommandOperation Operation, typename ValueType>
    EWorldCommandRecordResult TryRecordComponentValue(
        TComponentStorage<T>& Storage,
        FWorldCommandEntityTarget Target,
        ValueType&& Value) noexcept
    {
        static_assert(std::is_nothrow_constructible<T, ValueType&&>::value,
            "deferred component payload construction must not throw");
        if constexpr (!std::is_const<typename std::remove_reference<ValueType>::type>::value)
        {
            const Span<const T> Components = Storage.GetComponents();
            for (const T& Component : Components)
            {
                if (&Component == &Value) return EWorldCommandRecordResult::AliasedPayload;
            }
        }
        using FPayload = TComponentValuePayload<T, Operation>;
        FCommandReservation Reservation;
        const EWorldCommandRecordResult Result = BeginRecord(
            Target, Storage, sizeof(FPayload), alignof(FPayload),
            &ExecuteComponentValue<T, Operation>, &DestroyPayload<FPayload>, Reservation);
        if (Result != EWorldCommandRecordResult::Recorded) return Result;
        new (Reservation.Payload) FPayload{
            &Storage, Target, T(std::forward<ValueType>(Value))};
        CommitRecord(Reservation);
        return EWorldCommandRecordResult::Recorded;
    }

    template <typename T, EComponentCommandOperation Operation>
    static EWorldCommandFailureReason ExecuteComponentValue(
        FWorldCommandBuffer& Buffer, void* Address) noexcept
    {
        auto& Payload = *static_cast<TComponentValuePayload<T, Operation>*>(Address);
        FEntity Entity;
        const EWorldCommandFailureReason Resolved = Buffer.ResolveTarget(Payload.Target, Entity);
        if (Resolved != EWorldCommandFailureReason::None) return Resolved;
        const EComponentMutationResult Result = Operation == EComponentCommandOperation::Add
            ? Buffer.GetOwner().TryAddComponent(*Payload.Storage, Entity, std::move(Payload.Value))
            : Buffer.GetOwner().TrySetComponent(*Payload.Storage, Entity, std::move(Payload.Value));
        return MapComponentMutationResult(Result);
    }

    template <typename T>
    static EWorldCommandFailureReason ExecuteRemoveComponent(
        FWorldCommandBuffer& Buffer, void* Address) noexcept
    {
        auto& Payload = *static_cast<TRemoveComponentPayload<T>*>(Address);
        FEntity Entity;
        const EWorldCommandFailureReason Resolved = Buffer.ResolveTarget(Payload.Target, Entity);
        if (Resolved != EWorldCommandFailureReason::None) return Resolved;
        return MapComponentRemoveResult(
            Buffer.GetOwner().RemoveComponent(*Payload.Storage, Entity));
    }

    static EWorldCommandFailureReason MapComponentMutationResult(
        EComponentMutationResult Result) noexcept;
    static EWorldCommandFailureReason MapComponentRemoveResult(
        EComponentRemoveResult Result) noexcept;
    static EWorldCommandFailureReason ExecuteCreate(
        FWorldCommandBuffer& Buffer, void* Address) noexcept;
    static EWorldCommandFailureReason ExecuteDestroy(
        FWorldCommandBuffer& Buffer, void* Address) noexcept;
    static EWorldCommandFailureReason ExecuteAddTransform(
        FWorldCommandBuffer& Buffer, void* Address) noexcept;
    static EWorldCommandFailureReason ExecuteSetLocalTransform(
        FWorldCommandBuffer& Buffer, void* Address) noexcept;
    static EWorldCommandFailureReason ExecuteSetTransformParent(
        FWorldCommandBuffer& Buffer, void* Address) noexcept;
    static EWorldCommandFailureReason ExecuteClearTransformParent(
        FWorldCommandBuffer& Buffer, void* Address) noexcept;
    static EWorldCommandFailureReason ExecuteRemoveTransform(
        FWorldCommandBuffer& Buffer, void* Address) noexcept;
    FWorld& GetOwner() noexcept;

    FImpl* Impl = nullptr;
};

static_assert(std::is_trivially_copyable<FDeferredEntityToken>::value,
    "deferred Entity tokens must remain trivially copyable values");
static_assert(std::is_trivially_copyable<FWorldCommandEntityTarget>::value,
    "deferred command targets must remain trivially copyable values");

} // namespace LE
