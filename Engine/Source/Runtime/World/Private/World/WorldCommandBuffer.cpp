#include "World/WorldCommandBuffer.h"

#include "Containers/Array.h"
#include "Memory/Allocator.h"

#include <limits>
#include <new>

namespace LE
{
namespace
{

constexpr uint32 InvalidTokenIndex = (std::numeric_limits<uint32>::max)();

enum class ETokenState : uint8
{
    Free = 0,
    Pending,
    Created,
    CreationFailed,
};

struct FTokenRecord
{
    FEntity Entity;
    uint32 Generation = FDeferredEntitySlotHandle::InvalidGeneration;
    uint32 NextFree = InvalidTokenIndex;
    ETokenState State = ETokenState::Free;
};

struct FCommandNode
{
    FCommandNode* Next = nullptr;
    EWorldCommandFailureReason (*Execute)(FWorldCommandBuffer&, void*) noexcept = nullptr;
    void (*Destroy)(void*) noexcept = nullptr;
    std::size_t AllocationSize = 0;
    std::size_t AllocationAlignment = 0;
    std::size_t PayloadOffset = 0;
};

struct FCreatePayload { FDeferredEntityToken Token; };
struct FTargetPayload { FWorldCommandEntityTarget Target; };
struct FTransformPayload { FWorldCommandEntityTarget Target; FTransform Transform; };
struct FParentPayload
{
    FWorldCommandEntityTarget Child;
    FWorldCommandEntityTarget Parent;
};

bool TryAlignUp(const std::size_t Value, const std::size_t Alignment,
    std::size_t& OutValue) noexcept
{
    const std::size_t Mask = Alignment - 1;
    if (Value > (std::numeric_limits<std::size_t>::max)() - Mask) return false;
    OutValue = (Value + Mask) & ~Mask;
    return true;
}

} // namespace

struct FWorldCommandBuffer::FImpl
{
    FImpl(FWorld& InOwner, IAllocator& InAllocator) noexcept
        : Owner(&InOwner), Allocator(&InAllocator), Tokens(InAllocator) {}

    FWorld* Owner = nullptr;
    IAllocator* Allocator = nullptr;
    Array<FTokenRecord> Tokens;
    FCommandNode* First = nullptr;
    FCommandNode* Last = nullptr;
    uint32 FirstFreeToken = InvalidTokenIndex;
    std::size_t PendingCount = 0;
    bool bRecording = false;
    bool bFlushing = false;
    bool bCanceling = false;
};

FWorldCommandBuffer::FWorldCommandBuffer(FWorld& Owner, IAllocator& Allocator) noexcept
{
    FImpl* const Storage = TryAllocateArray<FImpl>(Allocator, 1);
    if (Storage != nullptr) Impl = new (Storage) FImpl(Owner, Allocator);
}

FWorldCommandBuffer::~FWorldCommandBuffer() noexcept
{
    if (Impl == nullptr) return;
    CancelAll();
    IAllocator& Allocator = *Impl->Allocator;
    Impl->~FImpl();
    DeallocateArray(Allocator, Impl, 1);
    Impl = nullptr;
}

bool FWorldCommandBuffer::IsValid() const noexcept
{
    return Impl != nullptr && Impl->Owner != nullptr && Impl->Owner->IsValid();
}

bool FWorldCommandBuffer::CanRecord() const noexcept
{
    return IsValid() && !Impl->bRecording && !Impl->bFlushing && !Impl->bCanceling &&
        Impl->Owner->GetTickState() != EWorldTickState::Flushing;
}

EWorldCommandRecordResult FWorldCommandBuffer::BeginRecordUnvalidated(
    const std::size_t PayloadSize,
    const std::size_t PayloadAlignment,
    const FExecuteThunk Execute,
    const FDestroyThunk Destroy,
    FCommandReservation& OutReservation) noexcept
{
    if (!IsValid()) return EWorldCommandRecordResult::InvalidWorld;
    if (!CanRecord()) return EWorldCommandRecordResult::InvalidState;
    if (PayloadSize == 0 || PayloadAlignment == 0 ||
        (PayloadAlignment & (PayloadAlignment - 1)) != 0 || Execute == nullptr || Destroy == nullptr)
    {
        return EWorldCommandRecordResult::InvalidState;
    }

    std::size_t PayloadOffset = 0;
    if (!TryAlignUp(sizeof(FCommandNode), PayloadAlignment, PayloadOffset) ||
        PayloadSize > (std::numeric_limits<std::size_t>::max)() - PayloadOffset)
    {
        return EWorldCommandRecordResult::AllocationFailed;
    }
    const std::size_t AllocationSize = PayloadOffset + PayloadSize;
    const std::size_t AllocationAlignment = PayloadAlignment > alignof(FCommandNode)
        ? PayloadAlignment : alignof(FCommandNode);
    void* const Allocation = TryAllocateBytes(
        *Impl->Allocator, AllocationSize, AllocationAlignment);
    if (Allocation == nullptr) return EWorldCommandRecordResult::AllocationFailed;

    FCommandNode* const Node = new (Allocation) FCommandNode();
    Node->Execute = Execute;
    Node->Destroy = Destroy;
    Node->AllocationSize = AllocationSize;
    Node->AllocationAlignment = AllocationAlignment;
    Node->PayloadOffset = PayloadOffset;
    Impl->bRecording = true;
    Impl->Owner->BeginCommandPayloadLifecycle();
    OutReservation.Node = Node;
    OutReservation.Payload = static_cast<uint8*>(Allocation) + PayloadOffset;
    return EWorldCommandRecordResult::Recorded;
}

EWorldCommandRecordResult FWorldCommandBuffer::BeginRecord(
    const FWorldCommandEntityTarget Target,
    const std::size_t PayloadSize,
    const std::size_t PayloadAlignment,
    const FExecuteThunk Execute,
    const FDestroyThunk Destroy,
    FCommandReservation& OutReservation) noexcept
{
    if (!IsValid()) return EWorldCommandRecordResult::InvalidWorld;
    if (!CanRecord()) return EWorldCommandRecordResult::InvalidState;
    if (!ValidateTargetForRecord(Target))
    {
        return Target.bDeferred
            ? EWorldCommandRecordResult::InvalidToken
            : EWorldCommandRecordResult::InvalidEntity;
    }
    return BeginRecordUnvalidated(PayloadSize, PayloadAlignment, Execute, Destroy, OutReservation);
}

EWorldCommandRecordResult FWorldCommandBuffer::BeginRecord(
    const FWorldCommandEntityTarget Target,
    const FComponentStorageNode& Storage,
    const std::size_t PayloadSize,
    const std::size_t PayloadAlignment,
    const FExecuteThunk Execute,
    const FDestroyThunk Destroy,
    FCommandReservation& OutReservation) noexcept
{
    if (!IsValid()) return EWorldCommandRecordResult::InvalidWorld;
    if (!CanRecord()) return EWorldCommandRecordResult::InvalidState;
    if (!Impl->Owner->IsStorageUsable(Storage))
        return EWorldCommandRecordResult::InvalidStorage;
    return BeginRecord(Target, PayloadSize, PayloadAlignment, Execute, Destroy, OutReservation);
}

void FWorldCommandBuffer::CommitRecord(FCommandReservation& Reservation) noexcept
{
    FCommandNode* const Node = static_cast<FCommandNode*>(Reservation.Node);
    if (Impl == nullptr || !Impl->bRecording || Node == nullptr)
        HandleContractViolation("invalid World command reservation commit", __FILE__, __LINE__);
    if (Impl->Last != nullptr) Impl->Last->Next = Node;
    else Impl->First = Node;
    Impl->Last = Node;
    ++Impl->PendingCount;
    Impl->Owner->EndCommandPayloadLifecycle();
    Impl->bRecording = false;
    Reservation = {};
}

bool FWorldCommandBuffer::ValidateTargetForRecord(
    const FWorldCommandEntityTarget Target) const noexcept
{
    if (!Target.bDeferred) return Impl->Owner->IsAlive(Target.EntityValue);
    if (!Target.TokenValue.IsValid() ||
        Target.TokenValue.WorldDomain != Impl->Owner->GetRuntimeDomain() ||
        Target.TokenValue.Slot.Index >= Impl->Tokens.Size()) return false;
    const FTokenRecord& Record = Impl->Tokens[Target.TokenValue.Slot.Index];
    if (Record.Generation != Target.TokenValue.Slot.Generation) return false;
    if (Record.State == ETokenState::Pending) return true;
    return Record.State == ETokenState::Created && Impl->Owner->IsAlive(Record.Entity);
}

EWorldCommandFailureReason FWorldCommandBuffer::ResolveTarget(
    const FWorldCommandEntityTarget Target, FEntity& OutEntity) noexcept
{
    if (!Target.bDeferred)
    {
        if (!Impl->Owner->IsAlive(Target.EntityValue))
            return EWorldCommandFailureReason::InvalidEntity;
        OutEntity = Target.EntityValue;
        return EWorldCommandFailureReason::None;
    }
    if (!Target.TokenValue.IsValid() ||
        Target.TokenValue.WorldDomain != Impl->Owner->GetRuntimeDomain() ||
        Target.TokenValue.Slot.Index >= Impl->Tokens.Size())
        return EWorldCommandFailureReason::InvalidToken;
    FTokenRecord& Record = Impl->Tokens[Target.TokenValue.Slot.Index];
    if (Record.Generation != Target.TokenValue.Slot.Generation || Record.State == ETokenState::Free)
        return EWorldCommandFailureReason::InvalidToken;
    if (Record.State != ETokenState::Created)
        return EWorldCommandFailureReason::DependencyFailed;
    if (!Impl->Owner->IsAlive(Record.Entity))
        return EWorldCommandFailureReason::InvalidEntity;
    OutEntity = Record.Entity;
    return EWorldCommandFailureReason::None;
}

std::size_t FWorldCommandBuffer::GetPendingCommandCount() const noexcept
{
    return Impl == nullptr ? 0 : Impl->PendingCount;
}

FWorld& FWorldCommandBuffer::GetOwner() noexcept { return *Impl->Owner; }

EWorldCommandFailureReason FWorldCommandBuffer::MapComponentMutationResult(
    const EComponentMutationResult Result) noexcept
{
    switch (Result)
    {
    case EComponentMutationResult::Added:
    case EComponentMutationResult::Replaced: return EWorldCommandFailureReason::None;
    case EComponentMutationResult::AlreadyPresent: return EWorldCommandFailureReason::AlreadyPresent;
    case EComponentMutationResult::InvalidEntity: return EWorldCommandFailureReason::InvalidEntity;
    case EComponentMutationResult::InvalidStorage: return EWorldCommandFailureReason::InvalidStorage;
    case EComponentMutationResult::AllocationFailed: return EWorldCommandFailureReason::AllocationFailed;
    }
    return EWorldCommandFailureReason::ReentrantMutation;
}

EWorldCommandFailureReason FWorldCommandBuffer::MapComponentRemoveResult(
    const EComponentRemoveResult Result) noexcept
{
    switch (Result)
    {
    case EComponentRemoveResult::Removed: return EWorldCommandFailureReason::None;
    case EComponentRemoveResult::NotPresent: return EWorldCommandFailureReason::MissingComponent;
    case EComponentRemoveResult::InvalidEntity: return EWorldCommandFailureReason::InvalidEntity;
    case EComponentRemoveResult::InvalidStorage: return EWorldCommandFailureReason::InvalidStorage;
    }
    return EWorldCommandFailureReason::ReentrantMutation;
}

EWorldCommandRecordResult FWorldCommandBuffer::TryCreateEntity(
    FDeferredEntityToken& OutToken) noexcept
{
    if (!IsValid()) return EWorldCommandRecordResult::InvalidWorld;
    if (!CanRecord()) return EWorldCommandRecordResult::InvalidState;

    uint32 TokenIndex = Impl->FirstFreeToken;
    const bool bNewToken = TokenIndex == InvalidTokenIndex;
    if (bNewToken)
    {
        if (Impl->Tokens.Size() >= static_cast<std::size_t>(InvalidTokenIndex))
            return EWorldCommandRecordResult::AllocationFailed;
        if (Impl->Tokens.Size() == Impl->Tokens.Capacity())
        {
            const std::size_t Current = Impl->Tokens.Capacity();
            const std::size_t Maximum = Array<FTokenRecord>::MaxSize();
            const std::size_t Requested = Current == 0 ? 4 :
                (Current > Maximum / 2 ? Maximum : Current * 2);
            if (Requested <= Current || !Impl->Tokens.TryReserve(Requested))
                return EWorldCommandRecordResult::AllocationFailed;
        }
        TokenIndex = static_cast<uint32>(Impl->Tokens.Size());
    }

    FCommandReservation Reservation;
    EWorldCommandRecordResult Result = BeginRecordUnvalidated(
        sizeof(FCreatePayload), alignof(FCreatePayload),
        &ExecuteCreate, &DestroyPayload<FCreatePayload>, Reservation);
    if (Result != EWorldCommandRecordResult::Recorded) return Result;

    if (bNewToken)
    {
        if (!Impl->Tokens.TryPushBack(FTokenRecord{}))
            HandleContractViolation("reserved deferred token append failed", __FILE__, __LINE__);
        Impl->Tokens[TokenIndex].Generation = 1;
    }
    else
    {
        Impl->FirstFreeToken = Impl->Tokens[TokenIndex].NextFree;
    }

    FTokenRecord& Record = Impl->Tokens[TokenIndex];
    Record.Entity.Reset();
    Record.NextFree = InvalidTokenIndex;
    Record.State = ETokenState::Pending;
    FDeferredEntityToken Token;
    Token.Slot = FDeferredEntitySlotHandle{TokenIndex, Record.Generation};
    Token.WorldDomain = Impl->Owner->GetRuntimeDomain();
    new (Reservation.Payload) FCreatePayload{Token};
    CommitRecord(Reservation);
    OutToken = Token;
    return EWorldCommandRecordResult::Recorded;
}

EWorldCommandRecordResult FWorldCommandBuffer::TryDestroyEntity(
    const FWorldCommandEntityTarget Target) noexcept
{
    FCommandReservation Reservation;
    const EWorldCommandRecordResult Result = BeginRecord(
        Target, sizeof(FTargetPayload), alignof(FTargetPayload),
        &ExecuteDestroy, &DestroyPayload<FTargetPayload>, Reservation);
    if (Result != EWorldCommandRecordResult::Recorded) return Result;
    new (Reservation.Payload) FTargetPayload{Target};
    CommitRecord(Reservation);
    return EWorldCommandRecordResult::Recorded;
}

EWorldCommandRecordResult FWorldCommandBuffer::TryAddTransform(
    const FWorldCommandEntityTarget Target, const FTransform& Transform) noexcept
{
    FCommandReservation Reservation;
    const EWorldCommandRecordResult Result = BeginRecord(
        Target, sizeof(FTransformPayload), alignof(FTransformPayload),
        &ExecuteAddTransform, &DestroyPayload<FTransformPayload>, Reservation);
    if (Result != EWorldCommandRecordResult::Recorded) return Result;
    new (Reservation.Payload) FTransformPayload{Target, Transform};
    CommitRecord(Reservation);
    return EWorldCommandRecordResult::Recorded;
}

EWorldCommandRecordResult FWorldCommandBuffer::TrySetLocalTransform(
    const FWorldCommandEntityTarget Target, const FTransform& Transform) noexcept
{
    FCommandReservation Reservation;
    const EWorldCommandRecordResult Result = BeginRecord(
        Target, sizeof(FTransformPayload), alignof(FTransformPayload),
        &ExecuteSetLocalTransform, &DestroyPayload<FTransformPayload>, Reservation);
    if (Result != EWorldCommandRecordResult::Recorded) return Result;
    new (Reservation.Payload) FTransformPayload{Target, Transform};
    CommitRecord(Reservation);
    return EWorldCommandRecordResult::Recorded;
}

EWorldCommandRecordResult FWorldCommandBuffer::TrySetTransformParent(
    const FWorldCommandEntityTarget Child,
    const FWorldCommandEntityTarget Parent) noexcept
{
    if (!IsValid()) return EWorldCommandRecordResult::InvalidWorld;
    if (!CanRecord()) return EWorldCommandRecordResult::InvalidState;
    if (!ValidateTargetForRecord(Child))
        return Child.bDeferred ? EWorldCommandRecordResult::InvalidToken
                               : EWorldCommandRecordResult::InvalidEntity;
    if (!ValidateTargetForRecord(Parent))
        return Parent.bDeferred ? EWorldCommandRecordResult::InvalidToken
                                : EWorldCommandRecordResult::InvalidEntity;
    FCommandReservation Reservation;
    const EWorldCommandRecordResult Result = BeginRecordUnvalidated(
        sizeof(FParentPayload), alignof(FParentPayload),
        &ExecuteSetTransformParent, &DestroyPayload<FParentPayload>, Reservation);
    if (Result != EWorldCommandRecordResult::Recorded) return Result;
    new (Reservation.Payload) FParentPayload{Child, Parent};
    CommitRecord(Reservation);
    return EWorldCommandRecordResult::Recorded;
}

EWorldCommandRecordResult FWorldCommandBuffer::TryClearTransformParent(
    const FWorldCommandEntityTarget Child) noexcept
{
    FCommandReservation Reservation;
    const EWorldCommandRecordResult Result = BeginRecord(
        Child, sizeof(FTargetPayload), alignof(FTargetPayload),
        &ExecuteClearTransformParent, &DestroyPayload<FTargetPayload>, Reservation);
    if (Result != EWorldCommandRecordResult::Recorded) return Result;
    new (Reservation.Payload) FTargetPayload{Child};
    CommitRecord(Reservation);
    return EWorldCommandRecordResult::Recorded;
}

EWorldCommandRecordResult FWorldCommandBuffer::TryRemoveTransform(
    const FWorldCommandEntityTarget Target) noexcept
{
    FCommandReservation Reservation;
    const EWorldCommandRecordResult Result = BeginRecord(
        Target, sizeof(FTargetPayload), alignof(FTargetPayload),
        &ExecuteRemoveTransform, &DestroyPayload<FTargetPayload>, Reservation);
    if (Result != EWorldCommandRecordResult::Recorded) return Result;
    new (Reservation.Payload) FTargetPayload{Target};
    CommitRecord(Reservation);
    return EWorldCommandRecordResult::Recorded;
}

FWorldCommandFlushResult FWorldCommandBuffer::Flush() noexcept
{
    FWorldCommandFlushResult Result;
    if (Impl == nullptr || Impl->bRecording || Impl->bFlushing || Impl->bCanceling)
        return Result;
    Impl->bFlushing = true;
    FCommandNode* Node = Impl->First;
    Impl->First = nullptr;
    Impl->Last = nullptr;
    Impl->PendingCount = 0;
    while (Node != nullptr)
    {
        FCommandNode* const Next = Node->Next;
        void* const Payload = reinterpret_cast<uint8*>(Node) + Node->PayloadOffset;
        Impl->Owner->BeginCommandExecution();
        const EWorldCommandFailureReason Failure = Node->Execute(*this, Payload);
        Impl->Owner->EndCommandExecution();
        if (Failure == EWorldCommandFailureReason::None) ++Result.SucceededCount;
        else
        {
            if (Result.FailedCount == 0)
            {
                Result.FirstFailureIndex = Result.ExecutedCount;
                Result.FirstFailureReason = Failure;
            }
            ++Result.FailedCount;
        }
        ++Result.ExecutedCount;
        Impl->Owner->BeginCommandPayloadLifecycle();
        Node->Destroy(Payload);
        Impl->Owner->EndCommandPayloadLifecycle();
        Impl->Allocator->Deallocate(Node, Node->AllocationSize, Node->AllocationAlignment);
        Node = Next;
    }
    Impl->bFlushing = false;
    return Result;
}

void FWorldCommandBuffer::CancelAll() noexcept
{
    if (Impl == nullptr || Impl->bCanceling) return;
    if (Impl->bRecording || Impl->bFlushing)
        HandleContractViolation("World command cancellation during record/flush", __FILE__, __LINE__);
    Impl->bCanceling = true;
    FCommandNode* Node = Impl->First;
    while (Node != nullptr)
    {
        FCommandNode* const Next = Node->Next;
        void* const Payload = reinterpret_cast<uint8*>(Node) + Node->PayloadOffset;
        Impl->Owner->BeginCommandPayloadLifecycle();
        Node->Destroy(Payload);
        Impl->Owner->EndCommandPayloadLifecycle();
        Impl->Allocator->Deallocate(Node, Node->AllocationSize, Node->AllocationAlignment);
        Node = Next;
    }
    Impl->First = nullptr;
    Impl->Last = nullptr;
    Impl->PendingCount = 0;
    Impl->FirstFreeToken = InvalidTokenIndex;
    for (std::size_t Index = Impl->Tokens.Size(); Index != 0; --Index)
    {
        FTokenRecord& Record = Impl->Tokens[Index - 1];
        Record.Entity.Reset();
        if (Record.State != ETokenState::Free &&
            Record.Generation != FDeferredEntitySlotHandle::InvalidGeneration &&
            Record.Generation != (std::numeric_limits<uint32>::max)())
        {
            ++Record.Generation;
        }
        Record.State = ETokenState::Free;
        if (Record.Generation != FDeferredEntitySlotHandle::InvalidGeneration &&
            Record.Generation != (std::numeric_limits<uint32>::max)())
        {
            Record.NextFree = Impl->FirstFreeToken;
            Impl->FirstFreeToken = static_cast<uint32>(Index - 1);
        }
        else Record.NextFree = InvalidTokenIndex;
    }
    Impl->bCanceling = false;
}

EDeferredEntityResolveResult FWorldCommandBuffer::TryResolveDeferredEntity(
    const FDeferredEntityToken Token, FEntity& OutEntity) noexcept
{
    if (Impl != nullptr && (Impl->bRecording || Impl->bFlushing || Impl->bCanceling))
        return EDeferredEntityResolveResult::InvalidState;
    if (!IsValid() || !Token.IsValid() || Token.WorldDomain != Impl->Owner->GetRuntimeDomain() ||
        Token.Slot.Index >= Impl->Tokens.Size()) return EDeferredEntityResolveResult::InvalidToken;
    FTokenRecord& Record = Impl->Tokens[Token.Slot.Index];
    if (Record.Generation != Token.Slot.Generation || Record.State == ETokenState::Free)
        return EDeferredEntityResolveResult::InvalidToken;
    if (Record.State == ETokenState::Pending) return EDeferredEntityResolveResult::Pending;

    EDeferredEntityResolveResult Result = EDeferredEntityResolveResult::CreationFailed;
    if (Record.State == ETokenState::Created)
    {
        if (Impl->Owner->IsAlive(Record.Entity))
        {
            OutEntity = Record.Entity;
            Result = EDeferredEntityResolveResult::Resolved;
        }
        else Result = EDeferredEntityResolveResult::NoLongerAlive;
    }
    Record.Entity.Reset();
    Record.State = ETokenState::Free;
    if (Record.Generation != (std::numeric_limits<uint32>::max)())
    {
        ++Record.Generation;
        Record.NextFree = Impl->FirstFreeToken;
        Impl->FirstFreeToken = Token.Slot.Index;
    }
    return Result;
}

bool FWorldCommandBuffer::DiscardDeferredEntityToken(const FDeferredEntityToken Token) noexcept
{
    FEntity Ignored;
    const EDeferredEntityResolveResult Result = TryResolveDeferredEntity(Token, Ignored);
    return Result == EDeferredEntityResolveResult::Resolved ||
        Result == EDeferredEntityResolveResult::CreationFailed ||
        Result == EDeferredEntityResolveResult::NoLongerAlive;
}

EWorldCommandFailureReason FWorldCommandBuffer::ExecuteCreate(
    FWorldCommandBuffer& Buffer, void* Address) noexcept
{
    const FDeferredEntityToken Token = static_cast<FCreatePayload*>(Address)->Token;
    if (Token.Slot.Index >= Buffer.Impl->Tokens.Size())
        return EWorldCommandFailureReason::InvalidToken;
    FTokenRecord& Record = Buffer.Impl->Tokens[Token.Slot.Index];
    if (Record.Generation != Token.Slot.Generation || Record.State != ETokenState::Pending)
        return EWorldCommandFailureReason::InvalidToken;
    FEntity Entity;
    if (!Buffer.Impl->Owner->TryCreateEntity(Entity))
    {
        Record.State = ETokenState::CreationFailed;
        return EWorldCommandFailureReason::AllocationFailed;
    }
    Record.Entity = Entity;
    Record.State = ETokenState::Created;
    return EWorldCommandFailureReason::None;
}

EWorldCommandFailureReason FWorldCommandBuffer::ExecuteDestroy(
    FWorldCommandBuffer& Buffer, void* Address) noexcept
{
    FEntity Entity;
    const EWorldCommandFailureReason Resolved = Buffer.ResolveTarget(
        static_cast<FTargetPayload*>(Address)->Target, Entity);
    if (Resolved != EWorldCommandFailureReason::None) return Resolved;
    return Buffer.Impl->Owner->DestroyEntity(Entity)
        ? EWorldCommandFailureReason::None : EWorldCommandFailureReason::InvalidEntity;
}

EWorldCommandFailureReason FWorldCommandBuffer::ExecuteAddTransform(
    FWorldCommandBuffer& Buffer, void* Address) noexcept
{
    auto& Payload = *static_cast<FTransformPayload*>(Address);
    FEntity Entity;
    const EWorldCommandFailureReason Resolved = Buffer.ResolveTarget(Payload.Target, Entity);
    if (Resolved != EWorldCommandFailureReason::None) return Resolved;
    return MapComponentMutationResult(Buffer.Impl->Owner->TryAddTransform(Entity, Payload.Transform));
}

EWorldCommandFailureReason FWorldCommandBuffer::ExecuteSetLocalTransform(
    FWorldCommandBuffer& Buffer, void* Address) noexcept
{
    auto& Payload = *static_cast<FTransformPayload*>(Address);
    FEntity Entity;
    const EWorldCommandFailureReason Resolved = Buffer.ResolveTarget(Payload.Target, Entity);
    if (Resolved != EWorldCommandFailureReason::None) return Resolved;
    switch (Buffer.Impl->Owner->SetLocalTransform(Entity, Payload.Transform))
    {
    case ETransformUpdateResult::Updated:
    case ETransformUpdateResult::Unchanged: return EWorldCommandFailureReason::None;
    case ETransformUpdateResult::InvalidEntity: return EWorldCommandFailureReason::InvalidEntity;
    case ETransformUpdateResult::MissingTransform: return EWorldCommandFailureReason::MissingComponent;
    }
    return EWorldCommandFailureReason::ReentrantMutation;
}

EWorldCommandFailureReason FWorldCommandBuffer::ExecuteSetTransformParent(
    FWorldCommandBuffer& Buffer, void* Address) noexcept
{
    auto& Payload = *static_cast<FParentPayload*>(Address);
    FEntity Child;
    FEntity Parent;
    EWorldCommandFailureReason Resolved = Buffer.ResolveTarget(Payload.Child, Child);
    if (Resolved != EWorldCommandFailureReason::None) return Resolved;
    Resolved = Buffer.ResolveTarget(Payload.Parent, Parent);
    if (Resolved != EWorldCommandFailureReason::None) return Resolved;
    switch (Buffer.Impl->Owner->SetTransformParent(Child, Parent))
    {
    case ETransformParentResult::Reparented:
    case ETransformParentResult::Unchanged: return EWorldCommandFailureReason::None;
    case ETransformParentResult::InvalidEntity: return EWorldCommandFailureReason::InvalidEntity;
    case ETransformParentResult::MissingTransform:
    case ETransformParentResult::MissingParentTransform: return EWorldCommandFailureReason::MissingComponent;
    case ETransformParentResult::CycleRejected: return EWorldCommandFailureReason::CycleRejected;
    }
    return EWorldCommandFailureReason::ReentrantMutation;
}

EWorldCommandFailureReason FWorldCommandBuffer::ExecuteClearTransformParent(
    FWorldCommandBuffer& Buffer, void* Address) noexcept
{
    FEntity Entity;
    const EWorldCommandFailureReason Resolved = Buffer.ResolveTarget(
        static_cast<FTargetPayload*>(Address)->Target, Entity);
    if (Resolved != EWorldCommandFailureReason::None) return Resolved;
    switch (Buffer.Impl->Owner->ClearTransformParent(Entity))
    {
    case ETransformParentResult::Reparented:
    case ETransformParentResult::Unchanged: return EWorldCommandFailureReason::None;
    case ETransformParentResult::InvalidEntity: return EWorldCommandFailureReason::InvalidEntity;
    case ETransformParentResult::MissingTransform:
    case ETransformParentResult::MissingParentTransform: return EWorldCommandFailureReason::MissingComponent;
    case ETransformParentResult::CycleRejected: return EWorldCommandFailureReason::CycleRejected;
    }
    return EWorldCommandFailureReason::ReentrantMutation;
}

EWorldCommandFailureReason FWorldCommandBuffer::ExecuteRemoveTransform(
    FWorldCommandBuffer& Buffer, void* Address) noexcept
{
    FEntity Entity;
    const EWorldCommandFailureReason Resolved = Buffer.ResolveTarget(
        static_cast<FTargetPayload*>(Address)->Target, Entity);
    if (Resolved != EWorldCommandFailureReason::None) return Resolved;
    return MapComponentRemoveResult(Buffer.Impl->Owner->RemoveTransform(Entity));
}

} // namespace LE
