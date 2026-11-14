#include "World/World.h"

#include "Containers/Array.h"
#include "Containers/String.h"
#include "Memory/Allocator.h"

#include <atomic>
#include <cmath>
#include <limits>
#include <new>

namespace LE
{
namespace
{

std::atomic<uint64> GNextWorldDomain{1};

struct FTransformComponent
{
    FTransform Local;
    Math::Matrix4f LocalToWorld = Math::Matrix4f::Identity();
    FEntity Parent;
    FEntity FirstChild;
    FEntity PreviousSibling;
    FEntity NextSibling;
    bool bWorldDirty = true;
};

FTransform CanonicalizeTransform(const FTransform& Transform) noexcept
{
    FTransform Result = Transform;
    Result.Rotation = Transform.NormalizedRotation();
    return Result;
}

struct FRegisteredNativeGameplaySystem
{
    explicit FRegisteredNativeGameplaySystem(IAllocator& Allocator) noexcept
        : Name(Allocator)
        , Dependencies(Allocator)
    {
    }

    String Name;
    Array<String> Dependencies;
    void* ProviderContext = nullptr;
    FNativeGameplaySystemUpdate Update = nullptr;
    ENativeGameplaySystemPhase Phase = ENativeGameplaySystemPhase::Variable;
    bool bEnabled = true;
};

bool IsAsciiLetter(const char Character) noexcept
{
    return (Character >= 'A' && Character <= 'Z') ||
        (Character >= 'a' && Character <= 'z');
}

bool IsAsciiDigit(const char Character) noexcept
{
    return Character >= '0' && Character <= '9';
}

bool IsValidNativeGameplaySystemName(const StringView Name) noexcept
{
    if (Name.IsEmpty() || (!IsAsciiLetter(Name[0]) && !IsAsciiDigit(Name[0])))
    {
        return false;
    }
    for (std::size_t Index = 0; Index < Name.Size(); ++Index)
    {
        const char Character = Name[Index];
        if (!IsAsciiLetter(Character) && !IsAsciiDigit(Character) &&
            Character != '.' && Character != '_' && Character != '-')
        {
            return false;
        }
    }
    return true;
}

std::size_t FindNativeGameplaySystemIndex(
    const Array<FRegisteredNativeGameplaySystem>& Systems,
    const StringView Name) noexcept
{
    for (std::size_t Index = 0; Index < Systems.Size(); ++Index)
    {
        if (Systems[Index].Name == Name)
        {
            return Index;
        }
    }
    return Systems.Size();
}

double SaturatingAddFinite(const double Left, const double Right) noexcept
{
    const double Maximum = (std::numeric_limits<double>::max)();
    return Left > Maximum - Right ? Maximum : Left + Right;
}

double NormalizeFixedAccumulator(
    const double Value,
    const double FixedDeltaSeconds) noexcept
{
    const double Scale = std::fmax(1.0, std::fabs(Value / FixedDeltaSeconds));
    const double Tolerance =
        64.0 * std::numeric_limits<double>::epsilon() * Scale * FixedDeltaSeconds;
    if (std::fabs(Value) <= Tolerance)
    {
        return 0.0;
    }
    return Value < 0.0 ? 0.0 : Value;
}

uint64 AcquireWorldDomain() noexcept
{
    uint64 Candidate = GNextWorldDomain.load(std::memory_order_relaxed);
    while (Candidate != 0)
    {
        const uint64 Next = Candidate == (std::numeric_limits<uint64>::max)()
            ? uint64{0}
            : Candidate + 1;
        if (GNextWorldDomain.compare_exchange_weak(
                Candidate,
                Next,
                std::memory_order_relaxed,
                std::memory_order_relaxed))
        {
            return Candidate;
        }
    }
    return 0;
}

} // namespace

struct FWorld::FImpl
{
    explicit FImpl(IAllocator& InAllocator, const uint64 InWorldDomain) noexcept
        : Allocator(&InAllocator)
        , WorldDomain(InWorldDomain)
        , EntitySlots(InAllocator)
        , AliveEntities(InAllocator)
        , TransformComponents(InAllocator)
        , MeshComponents(InAllocator)
        , CameraComponents(InAllocator)
        , DirectionalLightComponents(InAllocator)
        , NativeGameplaySystems(InAllocator)
        , FixedSystemOrder(InAllocator)
        , VariableSystemOrder(InAllocator)
    {
    }

    IAllocator* Allocator = nullptr;
    uint64 WorldDomain = 0;
    TRuntimeHandlePool<FEntitySlotTag> EntitySlots;
    Array<FEntity> AliveEntities;
    FComponentStorageNode* FirstStorage = nullptr;
    TComponentStorage<FTransformComponent> TransformComponents;
    TComponentStorage<FWorldMeshComponent> MeshComponents;
    TComponentStorage<FWorldCameraComponent> CameraComponents;
    TComponentStorage<FWorldDirectionalLightComponent> DirectionalLightComponents;
    FWorldCommandBuffer* CommandBuffer = nullptr;
    Array<FRegisteredNativeGameplaySystem> NativeGameplaySystems;
    Array<std::size_t> FixedSystemOrder;
    Array<std::size_t> VariableSystemOrder;
    FWorldTickContext TickContext;
    FNativeGameplayFixedStepConfig FixedStepConfig;
    double FixedAccumulatorSeconds = 0.0;
    double FixedSimulationSeconds = 0.0;
    uint64 NextFixedTickIndex = 0;
    std::size_t GameplayDiagnosticSystemIndex = (std::numeric_limits<std::size_t>::max)();
    std::size_t GameplayDiagnosticDependencyIndex = (std::numeric_limits<std::size_t>::max)();
    EWorldTickState TickState = EWorldTickState::Idle;
    bool bStructurallyMutating = false;
    bool bApplyingCommand = false;
    bool bCommandPayloadLifecycle = false;
    bool bTransformsDirty = false;
    bool bHasTickContext = false;
    bool bGameplayScheduleDirty = true;
    bool bGameplayScheduleCompiled = false;
    bool bExecutingGameplaySystems = false;
    bool bGameplaySystemsUpdatedThisTick = false;
    bool bFixedTimelineExhausted = false;
};

FComponentStorageNode::~FComponentStorageNode()
{
    Detail::DetachComponentStorage(*this);
}

namespace Detail
{

void DetachComponentStorage(FComponentStorageNode& Storage) noexcept
{
    if (Storage.Owner != nullptr)
    {
        Storage.Owner->DetachStorage(Storage);
    }
}

} // namespace Detail

FWorld::FWorld() noexcept
    : FWorld(GetDefaultAllocator())
{
}

FWorld::FWorld(IAllocator& Allocator) noexcept
{
    const uint64 WorldDomain = AcquireWorldDomain();
    if (WorldDomain == 0)
    {
        return;
    }

    FImpl* const Storage = TryAllocateArray<FImpl>(Allocator, 1);
    if (Storage == nullptr)
    {
        return;
    }
    Impl = new (Storage) FImpl(Allocator, WorldDomain);
    if (!AttachStorage(Impl->MeshComponents))
    {
        Impl->~FImpl();
        DeallocateArray(Allocator, Impl, 1);
        Impl = nullptr;
        return;
    }
    if (!AttachStorage(Impl->CameraComponents) || !AttachStorage(Impl->DirectionalLightComponents))
    {
        Impl->~FImpl();
        DeallocateArray(Allocator, Impl, 1);
        Impl = nullptr;
        return;
    }
    if (!AttachStorage(Impl->TransformComponents))
    {
        Impl->~FImpl();
        DeallocateArray(Allocator, Impl, 1);
        Impl = nullptr;
        return;
    }

    FWorldCommandBuffer* const CommandStorage =
        TryAllocateArray<FWorldCommandBuffer>(Allocator, 1);
    if (CommandStorage == nullptr)
    {
        Impl->~FImpl();
        DeallocateArray(Allocator, Impl, 1);
        Impl = nullptr;
        return;
    }
    FWorldCommandBuffer* const Commands =
        new (CommandStorage) FWorldCommandBuffer(*this, Allocator);
    if (!Commands->IsValid())
    {
        Commands->~FWorldCommandBuffer();
        DeallocateArray(Allocator, Commands, 1);
        Impl->~FImpl();
        DeallocateArray(Allocator, Impl, 1);
        Impl = nullptr;
        return;
    }
    Impl->CommandBuffer = Commands;
}

FWorld::~FWorld() noexcept
{
    if (Impl == nullptr)
    {
        return;
    }

    IAllocator& Allocator = *Impl->Allocator;
    if (Impl->CommandBuffer != nullptr)
    {
        Impl->CommandBuffer->CancelAll();
        Impl->CommandBuffer->~FWorldCommandBuffer();
        DeallocateArray(Allocator, Impl->CommandBuffer, 1);
        Impl->CommandBuffer = nullptr;
    }

    Impl->bStructurallyMutating = true;
    for (FComponentStorageNode* Storage = Impl->FirstStorage;
         Storage != nullptr;
         Storage = Storage->Next)
    {
        Storage->ClearThunk(*Storage);
    }
    for (const FEntity Entity : Impl->AliveEntities)
    {
        static_cast<void>(Impl->EntitySlots.Release(Entity.Slot));
    }
    Impl->AliveEntities.Clear();

    FComponentStorageNode* Storage = Impl->FirstStorage;
    while (Storage != nullptr)
    {
        FComponentStorageNode* const Next = Storage->Next;
        Storage->Owner = nullptr;
        Storage->Previous = nullptr;
        Storage->Next = nullptr;
        Storage->WorldDomain = 0;
        Storage = Next;
    }
    Impl->FirstStorage = nullptr;
    Impl->bStructurallyMutating = false;

    Impl->~FImpl();
    DeallocateArray(Allocator, Impl, 1);
    Impl = nullptr;
}

bool FWorld::IsValid() const noexcept
{
    return Impl != nullptr && Impl->WorldDomain != 0;
}

bool FWorld::TryCreateEntity(FEntity& OutEntity) noexcept
{
    if (!BeginStructuralMutation())
    {
        return false;
    }

    if (Impl->AliveEntities.Size() == Impl->AliveEntities.Capacity())
    {
        const std::size_t CurrentCapacity = Impl->AliveEntities.Capacity();
        const std::size_t MaximumCapacity = Array<FEntity>::MaxSize();
        if (CurrentCapacity == MaximumCapacity)
        {
            EndStructuralMutation();
            return false;
        }
        const std::size_t RequestedCapacity = CurrentCapacity == 0
            ? (MaximumCapacity < 4 ? MaximumCapacity : 4)
            : (CurrentCapacity > MaximumCapacity / 2
                ? MaximumCapacity
                : CurrentCapacity * 2);
        if (RequestedCapacity <= CurrentCapacity ||
            !Impl->AliveEntities.TryReserve(RequestedCapacity))
        {
            EndStructuralMutation();
            return false;
        }
    }

    FEntitySlotHandle Slot;
    if (!Impl->EntitySlots.TryAllocate(Slot))
    {
        EndStructuralMutation();
        return false;
    }

    const FEntity Candidate{Slot, Impl->WorldDomain};
    if (!Impl->AliveEntities.TryPushBack(Candidate))
    {
        HandleContractViolation(
            "reserved World live-Entity append unexpectedly failed", __FILE__, __LINE__);
    }
    OutEntity = Candidate;
    EndStructuralMutation();
    return true;
}

bool FWorld::DestroyEntity(const FEntity Entity) noexcept
{
    if (!IsAlive(Entity) || !BeginStructuralMutation())
    {
        return false;
    }

    static_cast<void>(RemoveTransformDuringStructuralMutation(Entity));

    for (FComponentStorageNode* Storage = Impl->FirstStorage;
         Storage != nullptr;
         Storage = Storage->Next)
    {
        static_cast<void>(Storage->RemoveThunk(*Storage, Entity));
    }

    if (!Impl->EntitySlots.Release(Entity.Slot))
    {
        HandleContractViolation("live Entity release failed", __FILE__, __LINE__);
    }

    for (std::size_t Index = 0; Index < Impl->AliveEntities.Size(); ++Index)
    {
        if (Impl->AliveEntities[Index] != Entity)
        {
            continue;
        }
        const std::size_t LastIndex = Impl->AliveEntities.Size() - 1;
        if (Index != LastIndex)
        {
            Impl->AliveEntities[Index] = Impl->AliveEntities[LastIndex];
        }
        Impl->AliveEntities.PopBack();
        EndStructuralMutation();
        return true;
    }

    HandleContractViolation("live Entity missing from dense list", __FILE__, __LINE__);
}

bool FWorld::IsAlive(const FEntity Entity) const noexcept
{
    return IsValid() && Entity.WorldDomain == Impl->WorldDomain &&
        Impl->EntitySlots.IsAlive(Entity.Slot);
}

bool FWorld::Reset() noexcept
{
    if (!IsValid() || Impl->TickState != EWorldTickState::Idle)
    {
        return false;
    }
    Impl->CommandBuffer->CancelAll();
    if (!BeginStructuralMutation())
    {
        return false;
    }

    for (FComponentStorageNode* Storage = Impl->FirstStorage;
         Storage != nullptr;
         Storage = Storage->Next)
    {
        Storage->ClearThunk(*Storage);
    }
    for (const FEntity Entity : Impl->AliveEntities)
    {
        if (!Impl->EntitySlots.Release(Entity.Slot))
        {
            HandleContractViolation("live Entity reset release failed", __FILE__, __LINE__);
        }
    }
    Impl->AliveEntities.Clear();
    Impl->bTransformsDirty = false;
    Impl->TickContext = {};
    Impl->bHasTickContext = false;
    Impl->FixedAccumulatorSeconds = 0.0;
    Impl->FixedSimulationSeconds = 0.0;
    Impl->NextFixedTickIndex = 0;
    Impl->bFixedTimelineExhausted = false;
    Impl->bGameplaySystemsUpdatedThisTick = false;
    EndStructuralMutation();
    return true;
}

EWorldTickResult FWorld::BeginTick(
    const FWorldTickContext& Context,
    FWorldCommandFlushResult* const OutFlushResult) noexcept
{
    if (!IsValid()) return EWorldTickResult::InvalidWorld;
    if (Impl->TickState != EWorldTickState::Idle || Impl->bStructurallyMutating)
        return EWorldTickResult::InvalidState;
    if (!std::isfinite(Context.DeltaSeconds) || !std::isfinite(Context.ElapsedSeconds) ||
        Context.DeltaSeconds < 0.0 || Context.ElapsedSeconds < 0.0 ||
        (Impl->bHasTickContext &&
            (Context.FrameIndex <= Impl->TickContext.FrameIndex ||
             Context.ElapsedSeconds < Impl->TickContext.ElapsedSeconds)))
    {
        return EWorldTickResult::InvalidContext;
    }

    Impl->TickState = EWorldTickState::Flushing;
    const FWorldCommandFlushResult FlushResult = Impl->CommandBuffer->Flush();
    if (!UpdateTransforms())
    {
        if (OutFlushResult != nullptr) *OutFlushResult = FlushResult;
        Impl->TickState = EWorldTickState::Idle;
        return EWorldTickResult::TransformUpdateFailed;
    }
    Impl->TickContext = Context;
    Impl->bHasTickContext = true;
    Impl->bGameplaySystemsUpdatedThisTick = false;
    Impl->TickState = EWorldTickState::Updating;
    if (OutFlushResult != nullptr) *OutFlushResult = FlushResult;
    return FlushResult.Succeeded()
        ? EWorldTickResult::Success
        : EWorldTickResult::CompletedWithCommandFailures;
}

EWorldTickResult FWorld::EndTick(
    FWorldCommandFlushResult* const OutFlushResult) noexcept
{
    if (!IsValid()) return EWorldTickResult::InvalidWorld;
    if (Impl->TickState != EWorldTickState::Updating || Impl->bStructurallyMutating)
        return EWorldTickResult::InvalidState;

    Impl->TickState = EWorldTickState::Flushing;
    const FWorldCommandFlushResult FlushResult = Impl->CommandBuffer->Flush();
    const bool bTransformsUpdated = UpdateTransforms();
    Impl->TickState = EWorldTickState::Idle;
    if (OutFlushResult != nullptr) *OutFlushResult = FlushResult;
    if (!bTransformsUpdated) return EWorldTickResult::TransformUpdateFailed;
    return FlushResult.Succeeded()
        ? EWorldTickResult::Success
        : EWorldTickResult::CompletedWithCommandFailures;
}

EWorldTickState FWorld::GetTickState() const noexcept
{
    return IsValid() ? Impl->TickState : EWorldTickState::Idle;
}

bool FWorld::TryGetTickContext(FWorldTickContext& OutContext) const noexcept
{
    if (!IsValid() || !Impl->bHasTickContext) return false;
    OutContext = Impl->TickContext;
    return true;
}

FWorldCommandBuffer* FWorld::GetCommandBuffer() noexcept
{
    return IsValid() ? Impl->CommandBuffer : nullptr;
}

const FWorldCommandBuffer* FWorld::GetCommandBuffer() const noexcept
{
    return IsValid() ? Impl->CommandBuffer : nullptr;
}

ENativeGameplaySystemRegisterResult FWorld::RegisterNativeGameplaySystem(
    const FNativeGameplaySystemDescriptor& Descriptor) noexcept
{
    if (!IsValid()) return ENativeGameplaySystemRegisterResult::InvalidWorld;
    if (Impl->TickState != EWorldTickState::Idle || Impl->bExecutingGameplaySystems)
        return ENativeGameplaySystemRegisterResult::InvalidState;
    if (!IsValidNativeGameplaySystemName(Descriptor.Name) || Descriptor.Update == nullptr ||
        (Descriptor.Phase != ENativeGameplaySystemPhase::Fixed &&
         Descriptor.Phase != ENativeGameplaySystemPhase::Variable))
    {
        return ENativeGameplaySystemRegisterResult::InvalidDescriptor;
    }
    for (std::size_t DependencyIndex = 0;
         DependencyIndex < Descriptor.Dependencies.Size();
         ++DependencyIndex)
    {
        const StringView Dependency = Descriptor.Dependencies[DependencyIndex];
        if (!IsValidNativeGameplaySystemName(Dependency))
        {
            return ENativeGameplaySystemRegisterResult::InvalidDescriptor;
        }
        for (std::size_t PreviousIndex = 0; PreviousIndex < DependencyIndex; ++PreviousIndex)
        {
            if (Descriptor.Dependencies[PreviousIndex] == Dependency)
            {
                return ENativeGameplaySystemRegisterResult::InvalidDescriptor;
            }
        }
    }
    if (FindNativeGameplaySystemIndex(Impl->NativeGameplaySystems, Descriptor.Name) !=
        Impl->NativeGameplaySystems.Size())
    {
        return ENativeGameplaySystemRegisterResult::DuplicateName;
    }

    FRegisteredNativeGameplaySystem Candidate(*Impl->Allocator);
    Candidate.ProviderContext = Descriptor.ProviderContext;
    Candidate.Update = Descriptor.Update;
    Candidate.Phase = Descriptor.Phase;
    Candidate.bEnabled = Descriptor.bEnabled;
    if (!Candidate.Name.TryAssign(Descriptor.Name) ||
        !Candidate.Dependencies.TryReserve(Descriptor.Dependencies.Size()))
    {
        return ENativeGameplaySystemRegisterResult::OutOfMemory;
    }
    for (const StringView DependencyView : Descriptor.Dependencies)
    {
        String Dependency(*Impl->Allocator);
        if (!Dependency.TryAssign(DependencyView) ||
            !Candidate.Dependencies.TryPushBack(std::move(Dependency)))
        {
            return ENativeGameplaySystemRegisterResult::OutOfMemory;
        }
    }
    if (!Impl->NativeGameplaySystems.TryPushBack(std::move(Candidate)))
    {
        return ENativeGameplaySystemRegisterResult::OutOfMemory;
    }

    Impl->bGameplayScheduleDirty = true;
    Impl->GameplayDiagnosticSystemIndex = (std::numeric_limits<std::size_t>::max)();
    Impl->GameplayDiagnosticDependencyIndex = (std::numeric_limits<std::size_t>::max)();
    return ENativeGameplaySystemRegisterResult::Success;
}

ENativeGameplaySystemUnregisterResult FWorld::UnregisterNativeGameplaySystem(
    const StringView Name) noexcept
{
    if (!IsValid()) return ENativeGameplaySystemUnregisterResult::InvalidWorld;
    if (Impl->TickState != EWorldTickState::Idle || Impl->bExecutingGameplaySystems)
        return ENativeGameplaySystemUnregisterResult::InvalidState;
    const std::size_t Index = FindNativeGameplaySystemIndex(Impl->NativeGameplaySystems, Name);
    if (Index == Impl->NativeGameplaySystems.Size())
        return ENativeGameplaySystemUnregisterResult::NotFound;
    Impl->NativeGameplaySystems.Erase(Index);
    Impl->bGameplayScheduleDirty = true;
    Impl->GameplayDiagnosticSystemIndex = (std::numeric_limits<std::size_t>::max)();
    Impl->GameplayDiagnosticDependencyIndex = (std::numeric_limits<std::size_t>::max)();
    return ENativeGameplaySystemUnregisterResult::Success;
}

ENativeGameplaySystemEnableResult FWorld::SetNativeGameplaySystemEnabled(
    const StringView Name,
    const bool bEnabled) noexcept
{
    if (!IsValid()) return ENativeGameplaySystemEnableResult::InvalidWorld;
    if (Impl->TickState != EWorldTickState::Idle || Impl->bExecutingGameplaySystems)
        return ENativeGameplaySystemEnableResult::InvalidState;
    const std::size_t Index = FindNativeGameplaySystemIndex(Impl->NativeGameplaySystems, Name);
    if (Index == Impl->NativeGameplaySystems.Size())
        return ENativeGameplaySystemEnableResult::NotFound;
    Impl->NativeGameplaySystems[Index].bEnabled = bEnabled;
    return ENativeGameplaySystemEnableResult::Success;
}

ENativeGameplayFixedStepConfigResult FWorld::ConfigureNativeGameplayFixedStep(
    const FNativeGameplayFixedStepConfig& Config) noexcept
{
    if (!IsValid()) return ENativeGameplayFixedStepConfigResult::InvalidWorld;
    if (Impl->TickState != EWorldTickState::Idle || Impl->bExecutingGameplaySystems)
        return ENativeGameplayFixedStepConfigResult::InvalidState;
    const double MaximumAccumulatedSeconds =
        Config.FixedDeltaSeconds * static_cast<double>(Config.MaxSubsteps);
    if (!std::isfinite(Config.FixedDeltaSeconds) || Config.FixedDeltaSeconds <= 0.0 ||
        Config.MaxSubsteps == 0 || Config.MaxSubsteps > MaxNativeGameplayFixedSubsteps ||
        !std::isfinite(MaximumAccumulatedSeconds))
    {
        return ENativeGameplayFixedStepConfigResult::InvalidConfig;
    }
    Impl->FixedStepConfig = Config;
    Impl->FixedAccumulatorSeconds = 0.0;
    Impl->FixedSimulationSeconds = 0.0;
    Impl->NextFixedTickIndex = 0;
    Impl->bFixedTimelineExhausted = false;
    return ENativeGameplayFixedStepConfigResult::Success;
}

ENativeGameplayScheduleResult FWorld::CompileNativeGameplaySystemSchedule() noexcept
{
    if (!IsValid()) return ENativeGameplayScheduleResult::InvalidWorld;
    if (Impl->TickState != EWorldTickState::Idle || Impl->bExecutingGameplaySystems)
        return ENativeGameplayScheduleResult::InvalidState;

    Impl->GameplayDiagnosticSystemIndex = (std::numeric_limits<std::size_t>::max)();
    Impl->GameplayDiagnosticDependencyIndex = (std::numeric_limits<std::size_t>::max)();
    for (std::size_t SystemIndex = 0;
         SystemIndex < Impl->NativeGameplaySystems.Size();
         ++SystemIndex)
    {
        const FRegisteredNativeGameplaySystem& System =
            Impl->NativeGameplaySystems[SystemIndex];
        for (std::size_t DependencyIndex = 0;
             DependencyIndex < System.Dependencies.Size();
             ++DependencyIndex)
        {
            const std::size_t FoundIndex = FindNativeGameplaySystemIndex(
                Impl->NativeGameplaySystems, System.Dependencies[DependencyIndex]);
            if (FoundIndex == Impl->NativeGameplaySystems.Size())
            {
                Impl->GameplayDiagnosticSystemIndex = SystemIndex;
                Impl->GameplayDiagnosticDependencyIndex = DependencyIndex;
                return ENativeGameplayScheduleResult::MissingDependency;
            }
            if (Impl->NativeGameplaySystems[FoundIndex].Phase != System.Phase)
            {
                Impl->GameplayDiagnosticSystemIndex = SystemIndex;
                Impl->GameplayDiagnosticDependencyIndex = DependencyIndex;
                return ENativeGameplayScheduleResult::CrossPhaseDependency;
            }
        }
    }

    Array<std::size_t> CandidateFixedOrder(*Impl->Allocator);
    Array<std::size_t> CandidateVariableOrder(*Impl->Allocator);
    Array<uint8> Planned(*Impl->Allocator);
    if (!CandidateFixedOrder.TryReserve(Impl->NativeGameplaySystems.Size()) ||
        !CandidateVariableOrder.TryReserve(Impl->NativeGameplaySystems.Size()) ||
        !Planned.TryResize(Impl->NativeGameplaySystems.Size()))
    {
        return ENativeGameplayScheduleResult::OutOfMemory;
    }

    for (const ENativeGameplaySystemPhase Phase :
         {ENativeGameplaySystemPhase::Fixed, ENativeGameplaySystemPhase::Variable})
    {
        std::size_t PhaseSystemCount = 0;
        std::size_t PlannedPhaseSystemCount = 0;
        for (const FRegisteredNativeGameplaySystem& System : Impl->NativeGameplaySystems)
        {
            if (System.Phase == Phase) ++PhaseSystemCount;
        }
        Array<std::size_t>& CandidateOrder = Phase == ENativeGameplaySystemPhase::Fixed
            ? CandidateFixedOrder : CandidateVariableOrder;
        while (PlannedPhaseSystemCount < PhaseSystemCount)
        {
            std::size_t SelectedIndex = Impl->NativeGameplaySystems.Size();
            for (std::size_t SystemIndex = 0;
                 SystemIndex < Impl->NativeGameplaySystems.Size();
                 ++SystemIndex)
            {
                const FRegisteredNativeGameplaySystem& Candidate =
                    Impl->NativeGameplaySystems[SystemIndex];
                if (Candidate.Phase != Phase || Planned[SystemIndex] != 0)
                {
                    continue;
                }
                bool bReady = true;
                for (const String& Dependency : Candidate.Dependencies)
                {
                    const std::size_t DependencySystemIndex = FindNativeGameplaySystemIndex(
                        Impl->NativeGameplaySystems, Dependency);
                    if (Planned[DependencySystemIndex] == 0)
                    {
                        bReady = false;
                        break;
                    }
                }
                if (bReady && (SelectedIndex == Impl->NativeGameplaySystems.Size() ||
                    Candidate.Name < Impl->NativeGameplaySystems[SelectedIndex].Name))
                {
                    SelectedIndex = SystemIndex;
                }
            }
            if (SelectedIndex == Impl->NativeGameplaySystems.Size())
            {
                for (std::size_t SystemIndex = 0;
                     SystemIndex < Impl->NativeGameplaySystems.Size();
                     ++SystemIndex)
                {
                    const FRegisteredNativeGameplaySystem& Candidate =
                        Impl->NativeGameplaySystems[SystemIndex];
                    if (Candidate.Phase == Phase && Planned[SystemIndex] == 0 &&
                        (Impl->GameplayDiagnosticSystemIndex ==
                            (std::numeric_limits<std::size_t>::max)() ||
                         Candidate.Name < Impl->NativeGameplaySystems[
                            Impl->GameplayDiagnosticSystemIndex].Name))
                    {
                        Impl->GameplayDiagnosticSystemIndex = SystemIndex;
                    }
                }
                return ENativeGameplayScheduleResult::DependencyCycle;
            }
            Planned[SelectedIndex] = 1;
            if (!CandidateOrder.TryPushBack(SelectedIndex))
            {
                return ENativeGameplayScheduleResult::OutOfMemory;
            }
            ++PlannedPhaseSystemCount;
        }
    }

    Impl->FixedSystemOrder = std::move(CandidateFixedOrder);
    Impl->VariableSystemOrder = std::move(CandidateVariableOrder);
    Impl->bGameplayScheduleCompiled = true;
    Impl->bGameplayScheduleDirty = false;
    return ENativeGameplayScheduleResult::Success;
}

ENativeGameplayUpdateResult FWorld::UpdateNativeGameplaySystems(
    FNativeGameplayUpdateReport* const OutReport) noexcept
{
    if (!IsValid()) return ENativeGameplayUpdateResult::InvalidWorld;
    if (Impl->TickState != EWorldTickState::Updating || Impl->bExecutingGameplaySystems ||
        Impl->bGameplaySystemsUpdatedThisTick)
    {
        return ENativeGameplayUpdateResult::InvalidState;
    }
    if (!Impl->bGameplayScheduleCompiled || Impl->bGameplayScheduleDirty)
    {
        return ENativeGameplayUpdateResult::DirtySchedule;
    }

    Impl->bGameplaySystemsUpdatedThisTick = true;
    Impl->bExecutingGameplaySystems = true;
    FNativeGameplayUpdateReport Report;
    const double MaximumAccumulatedSeconds = Impl->FixedStepConfig.FixedDeltaSeconds *
        static_cast<double>(Impl->FixedStepConfig.MaxSubsteps);
    const double TotalSeconds = SaturatingAddFinite(
        Impl->FixedAccumulatorSeconds, Impl->TickContext.DeltaSeconds);
    if (TotalSeconds > MaximumAccumulatedSeconds)
    {
        Report.DroppedSeconds = TotalSeconds - MaximumAccumulatedSeconds;
        Impl->FixedAccumulatorSeconds = MaximumAccumulatedSeconds;
    }
    else
    {
        Impl->FixedAccumulatorSeconds = TotalSeconds;
    }

    const double Ratio = Impl->FixedAccumulatorSeconds /
        Impl->FixedStepConfig.FixedDeltaSeconds;
    const double RatioTolerance = 64.0 * std::numeric_limits<double>::epsilon() *
        std::fmax(1.0, std::fabs(Ratio));
    double DueStepValue = std::floor(Ratio + RatioTolerance);
    if (DueStepValue < 0.0) DueStepValue = 0.0;
    if (DueStepValue > static_cast<double>(Impl->FixedStepConfig.MaxSubsteps))
        DueStepValue = static_cast<double>(Impl->FixedStepConfig.MaxSubsteps);
    const uint32 DueSteps = static_cast<uint32>(DueStepValue);

    ENativeGameplayUpdateResult Result = ENativeGameplayUpdateResult::Success;
    for (uint32 SubstepIndex = 0; SubstepIndex < DueSteps; ++SubstepIndex)
    {
        if (Impl->bFixedTimelineExhausted)
        {
            Result = ENativeGameplayUpdateResult::TimelineExhausted;
            break;
        }
        ++Report.FixedStepsAttempted;
        Impl->FixedAccumulatorSeconds = NormalizeFixedAccumulator(
            Impl->FixedAccumulatorSeconds - Impl->FixedStepConfig.FixedDeltaSeconds,
            Impl->FixedStepConfig.FixedDeltaSeconds);
        const uint64 FixedTickIndex = Impl->NextFixedTickIndex;
        if (FixedTickIndex == Impl->FixedStepConfig.LastFixedTickIndex)
            Impl->bFixedTimelineExhausted = true;
        else
            ++Impl->NextFixedTickIndex;
        Impl->FixedSimulationSeconds = SaturatingAddFinite(
            Impl->FixedSimulationSeconds, Impl->FixedStepConfig.FixedDeltaSeconds);

        FNativeGameplaySystemUpdateContext Context;
        Context.World = this;
        Context.Commands = Impl->CommandBuffer;
        Context.FrameIndex = Impl->TickContext.FrameIndex;
        Context.FrameDeltaSeconds = Impl->TickContext.DeltaSeconds;
        Context.FrameElapsedSeconds = Impl->TickContext.ElapsedSeconds;
        Context.StepDeltaSeconds = Impl->FixedStepConfig.FixedDeltaSeconds;
        Context.StepElapsedSeconds = Impl->FixedSimulationSeconds;
        Context.FixedTickIndex = FixedTickIndex;
        Context.FixedSubstepIndex = SubstepIndex;
        Context.Phase = ENativeGameplaySystemPhase::Fixed;
        bool bStepSucceeded = true;
        for (const std::size_t SystemIndex : Impl->FixedSystemOrder)
        {
            FRegisteredNativeGameplaySystem& System =
                Impl->NativeGameplaySystems[SystemIndex];
            if (!System.bEnabled) continue;
            ++Report.FixedCallbacksAttempted;
            if (System.Update(System.ProviderContext, Context) !=
                ENativeGameplaySystemResult::Success)
            {
                Report.FailedSystemName = System.Name;
                Report.FailedSubstepIndex = SubstepIndex;
                Report.FailedPhase = ENativeGameplaySystemPhase::Fixed;
                Result = ENativeGameplayUpdateResult::SystemFailed;
                bStepSucceeded = false;
                break;
            }
        }
        if (!bStepSucceeded) break;
        ++Report.FixedStepsCompleted;
    }

    if (Result == ENativeGameplayUpdateResult::Success)
    {
        FNativeGameplaySystemUpdateContext Context;
        Context.World = this;
        Context.Commands = Impl->CommandBuffer;
        Context.FrameIndex = Impl->TickContext.FrameIndex;
        Context.FrameDeltaSeconds = Impl->TickContext.DeltaSeconds;
        Context.FrameElapsedSeconds = Impl->TickContext.ElapsedSeconds;
        Context.StepDeltaSeconds = Impl->TickContext.DeltaSeconds;
        Context.StepElapsedSeconds = Impl->TickContext.ElapsedSeconds;
        Context.Phase = ENativeGameplaySystemPhase::Variable;
        for (const std::size_t SystemIndex : Impl->VariableSystemOrder)
        {
            FRegisteredNativeGameplaySystem& System =
                Impl->NativeGameplaySystems[SystemIndex];
            if (!System.bEnabled) continue;
            ++Report.VariableCallbacksAttempted;
            if (System.Update(System.ProviderContext, Context) !=
                ENativeGameplaySystemResult::Success)
            {
                Report.FailedSystemName = System.Name;
                Report.FailedPhase = ENativeGameplaySystemPhase::Variable;
                Result = ENativeGameplayUpdateResult::SystemFailed;
                break;
            }
        }
    }

    Impl->bExecutingGameplaySystems = false;
    Report.NextFixedTickIndex = Impl->NextFixedTickIndex;
    Report.FixedAccumulatorSeconds = Impl->FixedAccumulatorSeconds;
    Report.FixedSimulationSeconds = Impl->FixedSimulationSeconds;
    Report.bFixedTimelineExhausted = Impl->bFixedTimelineExhausted;
    if (OutReport != nullptr) *OutReport = Report;
    return Result;
}

StringView FWorld::GetNativeGameplayDiagnosticSystemName() const noexcept
{
    if (!IsValid() || Impl->GameplayDiagnosticSystemIndex >=
        Impl->NativeGameplaySystems.Size())
    {
        return {};
    }
    return Impl->NativeGameplaySystems[Impl->GameplayDiagnosticSystemIndex].Name;
}

StringView FWorld::GetNativeGameplayDiagnosticDependencyName() const noexcept
{
    if (!IsValid() || Impl->GameplayDiagnosticSystemIndex >=
        Impl->NativeGameplaySystems.Size())
    {
        return {};
    }
    const FRegisteredNativeGameplaySystem& System =
        Impl->NativeGameplaySystems[Impl->GameplayDiagnosticSystemIndex];
    if (Impl->GameplayDiagnosticDependencyIndex >= System.Dependencies.Size())
    {
        return {};
    }
    return System.Dependencies[Impl->GameplayDiagnosticDependencyIndex];
}

std::size_t FWorld::GetEntityCount() const noexcept
{
    return Impl == nullptr ? 0 : Impl->AliveEntities.Size();
}

EComponentMutationResult FWorld::TryAddTransform(
    const FEntity Entity,
    const FTransform& LocalTransform) noexcept
{
    if (!IsAlive(Entity))
    {
        return EComponentMutationResult::InvalidEntity;
    }
    if (!BeginStructuralMutation())
    {
        return EComponentMutationResult::InvalidStorage;
    }

    FTransformComponent Component;
    Component.Local = CanonicalizeTransform(LocalTransform);
    const EComponentMutationResult Result =
        Impl->TransformComponents.TryAdd(Entity, std::move(Component));
    if (Result == EComponentMutationResult::Added)
    {
        Impl->bTransformsDirty = true;
    }
    EndStructuralMutation();
    return Result;
}

ETransformUpdateResult FWorld::SetLocalTransform(
    const FEntity Entity,
    const FTransform& LocalTransform) noexcept
{
    if (!IsAlive(Entity))
    {
        return ETransformUpdateResult::InvalidEntity;
    }
    if (Impl->bStructurallyMutating)
    {
        return ETransformUpdateResult::InvalidEntity;
    }
    FTransformComponent* const Component = Impl->TransformComponents.Find(Entity);
    if (Component == nullptr)
    {
        return ETransformUpdateResult::MissingTransform;
    }
    const FTransform Canonical = CanonicalizeTransform(LocalTransform);
    if (Component->Local == Canonical)
    {
        return ETransformUpdateResult::Unchanged;
    }
    if (!BeginStructuralMutation())
    {
        return ETransformUpdateResult::InvalidEntity;
    }

    Component->Local = Canonical;
    MarkTransformSubtreeDirty(Entity);
    Impl->TransformComponents.AdvanceMutationVersion();
    EndStructuralMutation();
    return ETransformUpdateResult::Updated;
}

ETransformParentResult FWorld::SetTransformParent(
    const FEntity Child,
    const FEntity Parent) noexcept
{
    if (!IsAlive(Child) || !IsAlive(Parent))
    {
        return ETransformParentResult::InvalidEntity;
    }
    if (Impl->bStructurallyMutating)
    {
        return ETransformParentResult::InvalidEntity;
    }
    FTransformComponent* const ChildComponent = Impl->TransformComponents.Find(Child);
    if (ChildComponent == nullptr)
    {
        return ETransformParentResult::MissingTransform;
    }
    FTransformComponent* const ParentComponent = Impl->TransformComponents.Find(Parent);
    if (ParentComponent == nullptr)
    {
        return ETransformParentResult::MissingParentTransform;
    }
    if (ChildComponent->Parent == Parent)
    {
        return ETransformParentResult::Unchanged;
    }
    if (Child == Parent || WouldCreateTransformCycle(Child, Parent))
    {
        return ETransformParentResult::CycleRejected;
    }
    if (!BeginStructuralMutation())
    {
        return ETransformParentResult::InvalidEntity;
    }

    if (ChildComponent->Parent.IsValid())
    {
        FTransformComponent* const OldParent =
            Impl->TransformComponents.Find(ChildComponent->Parent);
        if (OldParent == nullptr)
        {
            HandleContractViolation("Transform parent link is stale", __FILE__, __LINE__);
        }
        if (ChildComponent->PreviousSibling.IsValid())
        {
            FTransformComponent* const Previous =
                Impl->TransformComponents.Find(ChildComponent->PreviousSibling);
            if (Previous == nullptr)
            {
                HandleContractViolation("Transform previous-sibling link is stale", __FILE__, __LINE__);
            }
            Previous->NextSibling = ChildComponent->NextSibling;
        }
        else
        {
            OldParent->FirstChild = ChildComponent->NextSibling;
        }
        if (ChildComponent->NextSibling.IsValid())
        {
            FTransformComponent* const Next =
                Impl->TransformComponents.Find(ChildComponent->NextSibling);
            if (Next == nullptr)
            {
                HandleContractViolation("Transform next-sibling link is stale", __FILE__, __LINE__);
            }
            Next->PreviousSibling = ChildComponent->PreviousSibling;
        }
    }

    ChildComponent->Parent = Parent;
    ChildComponent->PreviousSibling.Reset();
    ChildComponent->NextSibling = ParentComponent->FirstChild;
    if (ParentComponent->FirstChild.IsValid())
    {
        FTransformComponent* const First =
            Impl->TransformComponents.Find(ParentComponent->FirstChild);
        if (First == nullptr)
        {
            HandleContractViolation("Transform first-child link is stale", __FILE__, __LINE__);
        }
        First->PreviousSibling = Child;
    }
    ParentComponent->FirstChild = Child;
    MarkTransformSubtreeDirty(Child);
    Impl->TransformComponents.AdvanceMutationVersion();
    EndStructuralMutation();
    return ETransformParentResult::Reparented;
}

ETransformParentResult FWorld::ClearTransformParent(const FEntity Child) noexcept
{
    if (!IsAlive(Child))
    {
        return ETransformParentResult::InvalidEntity;
    }
    if (Impl->bStructurallyMutating)
    {
        return ETransformParentResult::InvalidEntity;
    }
    FTransformComponent* const ChildComponent = Impl->TransformComponents.Find(Child);
    if (ChildComponent == nullptr)
    {
        return ETransformParentResult::MissingTransform;
    }
    if (!ChildComponent->Parent.IsValid())
    {
        return ETransformParentResult::Unchanged;
    }
    if (!BeginStructuralMutation())
    {
        return ETransformParentResult::InvalidEntity;
    }

    FTransformComponent* const Parent =
        Impl->TransformComponents.Find(ChildComponent->Parent);
    if (Parent == nullptr)
    {
        HandleContractViolation("Transform parent link is stale", __FILE__, __LINE__);
    }
    if (ChildComponent->PreviousSibling.IsValid())
    {
        FTransformComponent* const Previous =
            Impl->TransformComponents.Find(ChildComponent->PreviousSibling);
        if (Previous == nullptr)
        {
            HandleContractViolation("Transform previous-sibling link is stale", __FILE__, __LINE__);
        }
        Previous->NextSibling = ChildComponent->NextSibling;
    }
    else
    {
        Parent->FirstChild = ChildComponent->NextSibling;
    }
    if (ChildComponent->NextSibling.IsValid())
    {
        FTransformComponent* const Next =
            Impl->TransformComponents.Find(ChildComponent->NextSibling);
        if (Next == nullptr)
        {
            HandleContractViolation("Transform next-sibling link is stale", __FILE__, __LINE__);
        }
        Next->PreviousSibling = ChildComponent->PreviousSibling;
    }
    ChildComponent->Parent.Reset();
    ChildComponent->PreviousSibling.Reset();
    ChildComponent->NextSibling.Reset();
    MarkTransformSubtreeDirty(Child);
    Impl->TransformComponents.AdvanceMutationVersion();
    EndStructuralMutation();
    return ETransformParentResult::Reparented;
}

EComponentRemoveResult FWorld::RemoveTransform(const FEntity Entity) noexcept
{
    if (!IsAlive(Entity))
    {
        return EComponentRemoveResult::InvalidEntity;
    }
    if (Impl->bStructurallyMutating)
    {
        return EComponentRemoveResult::InvalidStorage;
    }
    if (Impl->TransformComponents.Find(Entity) == nullptr)
    {
        return EComponentRemoveResult::NotPresent;
    }
    if (!BeginStructuralMutation())
    {
        return EComponentRemoveResult::InvalidStorage;
    }
    const bool bRemoved = RemoveTransformDuringStructuralMutation(Entity);
    EndStructuralMutation();
    return bRemoved ? EComponentRemoveResult::Removed : EComponentRemoveResult::NotPresent;
}

bool FWorld::TryGetLocalTransform(
    const FEntity Entity,
    FTransform& OutTransform) const noexcept
{
    if (!IsAlive(Entity) || Impl->bStructurallyMutating)
    {
        return false;
    }
    const FTransformComponent* const Component = Impl->TransformComponents.Find(Entity);
    if (Component == nullptr)
    {
        return false;
    }
    OutTransform = Component->Local;
    return true;
}

bool FWorld::TryGetTransformParent(
    const FEntity Entity,
    FEntity& OutParent) const noexcept
{
    if (!IsAlive(Entity) || Impl->bStructurallyMutating)
    {
        return false;
    }
    const FTransformComponent* const Component = Impl->TransformComponents.Find(Entity);
    if (Component == nullptr)
    {
        return false;
    }
    OutParent = Component->Parent;
    return true;
}

bool FWorld::HasTransform(const FEntity Entity) const noexcept
{
    return IsAlive(Entity) && !Impl->bStructurallyMutating &&
        Impl->TransformComponents.Find(Entity) != nullptr;
}

bool FWorld::TryGetWorldMatrix(
    const FEntity Entity,
    Math::Matrix4f& OutMatrix) const noexcept
{
    if (!IsAlive(Entity) || !UpdateTransforms())
    {
        return false;
    }
    const FTransformComponent* const Component = Impl->TransformComponents.Find(Entity);
    if (Component == nullptr)
    {
        return false;
    }
    OutMatrix = Component->LocalToWorld;
    return true;
}

bool FWorld::UpdateTransforms() const noexcept
{
    if (!IsValid() || Impl->bStructurallyMutating)
    {
        return false;
    }
    if (!Impl->bTransformsDirty)
    {
        return true;
    }
    Impl->bStructurallyMutating = true;
    const Span<const FEntity> Entities = Impl->TransformComponents.GetEntities();
    std::size_t VisitedCount = 0;
    for (const FEntity Root : Entities)
    {
        FTransformComponent* const RootComponent = Impl->TransformComponents.Find(Root);
        if (RootComponent == nullptr)
        {
            HandleContractViolation("Transform dense Entity mapping is stale", __FILE__, __LINE__);
        }
        if (RootComponent->Parent.IsValid())
        {
            continue;
        }

        FEntity Current = Root;
        while (Current.IsValid())
        {
            FTransformComponent* const Component = Impl->TransformComponents.Find(Current);
            if (Component == nullptr)
            {
                HandleContractViolation("Transform hierarchy link is stale", __FILE__, __LINE__);
            }
            if (Component->bWorldDirty)
            {
                const Math::Matrix4f LocalMatrix = Component->Local.ToMatrix();
                if (Component->Parent.IsValid())
                {
                    const FTransformComponent* const Parent =
                        Impl->TransformComponents.Find(Component->Parent);
                    if (Parent == nullptr || Parent->bWorldDirty)
                    {
                        HandleContractViolation(
                            "Transform update order is not parent-first", __FILE__, __LINE__);
                    }
                    Component->LocalToWorld = Parent->LocalToWorld * LocalMatrix;
                }
                else
                {
                    Component->LocalToWorld = LocalMatrix;
                }
                Component->bWorldDirty = false;
            }
            ++VisitedCount;
            if (VisitedCount > Entities.Size())
            {
                HandleContractViolation("Transform hierarchy contains a cycle", __FILE__, __LINE__);
            }
            Current = NextTransformInSubtree(Current, Root);
        }
    }
    if (VisitedCount != Entities.Size())
    {
        HandleContractViolation("Transform hierarchy has unreachable nodes", __FILE__, __LINE__);
    }
    Impl->bTransformsDirty = false;
    Impl->bStructurallyMutating = false;
    return true;
}

uint64 FWorld::GetTransformMutationVersion() const noexcept
{
    return IsValid() ? Impl->TransformComponents.GetMutationVersion() : 0;
}

bool FWorld::RemoveTransformDuringStructuralMutation(const FEntity Entity) noexcept
{
    FTransformComponent* const Component = Impl->TransformComponents.Find(Entity);
    if (Component == nullptr)
    {
        return false;
    }

    if (Component->Parent.IsValid())
    {
        FTransformComponent* const Parent =
            Impl->TransformComponents.Find(Component->Parent);
        if (Parent == nullptr)
        {
            HandleContractViolation("Transform parent link is stale", __FILE__, __LINE__);
        }
        if (Component->PreviousSibling.IsValid())
        {
            FTransformComponent* const Previous =
                Impl->TransformComponents.Find(Component->PreviousSibling);
            if (Previous == nullptr)
            {
                HandleContractViolation("Transform previous-sibling link is stale", __FILE__, __LINE__);
            }
            Previous->NextSibling = Component->NextSibling;
        }
        else
        {
            Parent->FirstChild = Component->NextSibling;
        }
        if (Component->NextSibling.IsValid())
        {
            FTransformComponent* const Next =
                Impl->TransformComponents.Find(Component->NextSibling);
            if (Next == nullptr)
            {
                HandleContractViolation("Transform next-sibling link is stale", __FILE__, __LINE__);
            }
            Next->PreviousSibling = Component->PreviousSibling;
        }
    }

    FEntity Child = Component->FirstChild;
    while (Child.IsValid())
    {
        FTransformComponent* const ChildComponent = Impl->TransformComponents.Find(Child);
        if (ChildComponent == nullptr)
        {
            HandleContractViolation("Transform child link is stale", __FILE__, __LINE__);
        }
        const FEntity NextChild = ChildComponent->NextSibling;
        ChildComponent->Parent.Reset();
        ChildComponent->PreviousSibling.Reset();
        ChildComponent->NextSibling.Reset();
        MarkTransformSubtreeDirty(Child);
        Child = NextChild;
    }
    return Impl->TransformComponents.Remove(Entity);
}

void FWorld::MarkTransformSubtreeDirty(const FEntity Root) noexcept
{
    Impl->bTransformsDirty = true;
    FEntity Current = Root;
    std::size_t VisitedCount = 0;
    while (Current.IsValid())
    {
        FTransformComponent* const Component = Impl->TransformComponents.Find(Current);
        if (Component == nullptr)
        {
            HandleContractViolation("Transform dirty link is stale", __FILE__, __LINE__);
        }
        Component->bWorldDirty = true;
        ++VisitedCount;
        if (VisitedCount > Impl->TransformComponents.Size())
        {
            HandleContractViolation("Transform hierarchy contains a cycle", __FILE__, __LINE__);
        }
        Current = NextTransformInSubtree(Current, Root);
    }
}

FEntity FWorld::NextTransformInSubtree(
    const FEntity Current,
    const FEntity Root) const noexcept
{
    const FTransformComponent* Component = Impl->TransformComponents.Find(Current);
    if (Component == nullptr)
    {
        HandleContractViolation("Transform traversal link is stale", __FILE__, __LINE__);
    }
    if (Component->FirstChild.IsValid())
    {
        return Component->FirstChild;
    }

    FEntity Cursor = Current;
    while (Cursor != Root)
    {
        Component = Impl->TransformComponents.Find(Cursor);
        if (Component == nullptr)
        {
            HandleContractViolation("Transform traversal link is stale", __FILE__, __LINE__);
        }
        if (Component->NextSibling.IsValid())
        {
            return Component->NextSibling;
        }
        Cursor = Component->Parent;
    }
    return FEntity{};
}

bool FWorld::WouldCreateTransformCycle(
    const FEntity Child,
    FEntity Parent) const noexcept
{
    std::size_t VisitedCount = 0;
    while (Parent.IsValid())
    {
        if (Parent == Child)
        {
            return true;
        }
        const FTransformComponent* const Component =
            Impl->TransformComponents.Find(Parent);
        if (Component == nullptr)
        {
            HandleContractViolation("Transform parent chain is stale", __FILE__, __LINE__);
        }
        Parent = Component->Parent;
        ++VisitedCount;
        if (VisitedCount > Impl->TransformComponents.Size())
        {
            HandleContractViolation("Transform hierarchy contains a cycle", __FILE__, __LINE__);
        }
    }
    return false;
}

TComponentStorage<FWorldMeshComponent>* FWorld::GetMeshComponents() noexcept
{
    return Impl == nullptr ? nullptr : &Impl->MeshComponents;
}

const TComponentStorage<FWorldMeshComponent>* FWorld::GetMeshComponents() const noexcept
{
    return Impl == nullptr ? nullptr : &Impl->MeshComponents;
}

TComponentStorage<FWorldCameraComponent>* FWorld::GetCameraComponents() noexcept
{
    return Impl == nullptr ? nullptr : &Impl->CameraComponents;
}

const TComponentStorage<FWorldCameraComponent>* FWorld::GetCameraComponents() const noexcept
{
    return Impl == nullptr ? nullptr : &Impl->CameraComponents;
}

TComponentStorage<FWorldDirectionalLightComponent>* FWorld::GetDirectionalLightComponents() noexcept
{
    return Impl == nullptr ? nullptr : &Impl->DirectionalLightComponents;
}

const TComponentStorage<FWorldDirectionalLightComponent>* FWorld::GetDirectionalLightComponents() const noexcept
{
    return Impl == nullptr ? nullptr : &Impl->DirectionalLightComponents;
}

bool FWorld::AttachStorage(FComponentStorageNode& Storage) noexcept
{
    if (!IsValid() || Impl->TickState != EWorldTickState::Idle ||
        Impl->bStructurallyMutating || Storage.Owner != nullptr ||
        Storage.RemoveThunk == nullptr || Storage.ClearThunk == nullptr)
    {
        return false;
    }
    Storage.Owner = this;
    Storage.WorldDomain = Impl->WorldDomain;
    Storage.Previous = nullptr;
    Storage.Next = Impl->FirstStorage;
    if (Impl->FirstStorage != nullptr)
    {
        Impl->FirstStorage->Previous = &Storage;
    }
    Impl->FirstStorage = &Storage;
    return true;
}

void FWorld::DetachStorage(FComponentStorageNode& Storage) noexcept
{
    if (Storage.Owner != this || Impl == nullptr)
    {
        return;
    }
    if (Impl->bStructurallyMutating || Impl->bCommandPayloadLifecycle ||
        Impl->TickState != EWorldTickState::Idle ||
        (Impl->CommandBuffer != nullptr &&
            Impl->CommandBuffer->GetPendingCommandCount() != 0))
    {
        HandleContractViolation(
            "component storage detached during World structural mutation", __FILE__, __LINE__);
    }
    if (Storage.Previous != nullptr)
    {
        Storage.Previous->Next = Storage.Next;
    }
    else
    {
        Impl->FirstStorage = Storage.Next;
    }
    if (Storage.Next != nullptr)
    {
        Storage.Next->Previous = Storage.Previous;
    }
    Storage.Owner = nullptr;
    Storage.Previous = nullptr;
    Storage.Next = nullptr;
    Storage.WorldDomain = 0;
}

bool FWorld::IsStorageUsable(const FComponentStorageNode& Storage) const noexcept
{
    return IsValid() && Storage.Owner == this &&
        Storage.WorldDomain == Impl->WorldDomain;
}

bool FWorld::BeginStructuralMutation() noexcept
{
    if (!IsValid() || Impl->bStructurallyMutating || Impl->bCommandPayloadLifecycle ||
        (Impl->TickState != EWorldTickState::Idle &&
            !(Impl->TickState == EWorldTickState::Flushing && Impl->bApplyingCommand)))
    {
        return false;
    }
    Impl->bStructurallyMutating = true;
    return true;
}

void FWorld::BeginCommandExecution() noexcept
{
    if (!IsValid() || Impl->TickState != EWorldTickState::Flushing ||
        Impl->bApplyingCommand || Impl->bStructurallyMutating)
    {
        HandleContractViolation("invalid deferred World command execution begin", __FILE__, __LINE__);
    }
    Impl->bApplyingCommand = true;
}

void FWorld::EndCommandExecution() noexcept
{
    if (!IsValid() || Impl->TickState != EWorldTickState::Flushing ||
        !Impl->bApplyingCommand || Impl->bStructurallyMutating)
    {
        HandleContractViolation("invalid deferred World command execution end", __FILE__, __LINE__);
    }
    Impl->bApplyingCommand = false;
}

uint64 FWorld::GetRuntimeDomain() const noexcept
{
    return IsValid() ? Impl->WorldDomain : 0;
}

void FWorld::BeginCommandPayloadLifecycle() noexcept
{
    if (!IsValid() || Impl->bCommandPayloadLifecycle || Impl->bStructurallyMutating ||
        Impl->bApplyingCommand)
    {
        HandleContractViolation("invalid deferred command payload lifecycle begin", __FILE__, __LINE__);
    }
    Impl->bCommandPayloadLifecycle = true;
}

void FWorld::EndCommandPayloadLifecycle() noexcept
{
    if (!IsValid() || !Impl->bCommandPayloadLifecycle || Impl->bStructurallyMutating ||
        Impl->bApplyingCommand)
    {
        HandleContractViolation("invalid deferred command payload lifecycle end", __FILE__, __LINE__);
    }
    Impl->bCommandPayloadLifecycle = false;
}

void FWorld::EndStructuralMutation() noexcept
{
    if (Impl == nullptr || !Impl->bStructurallyMutating)
    {
        HandleContractViolation("unbalanced World structural mutation", __FILE__, __LINE__);
    }
    Impl->bStructurallyMutating = false;
}

} // namespace LE
