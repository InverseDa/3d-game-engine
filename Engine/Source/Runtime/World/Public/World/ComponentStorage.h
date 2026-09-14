#pragma once

#include "Containers/Span.h"
#include "Memory/Allocator.h"
#include "Memory/Relocation.h"
#include "World/Entity.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace LE
{

class FWorld;
class FComponentStorageNode;

namespace Detail
{
WORLD_API void DetachComponentStorage(FComponentStorageNode& Storage) noexcept;
}

// Intrusive, non-owning registration node. FWorld never deletes a registered
// storage; it only invokes the creator-instantiated thunks while that module is
// loaded. Destruction detaches in O(1) without allocating.
class WORLD_API FComponentStorageNode
{
public:
    FComponentStorageNode(const FComponentStorageNode&) = delete;
    FComponentStorageNode& operator=(const FComponentStorageNode&) = delete;
    FComponentStorageNode(FComponentStorageNode&&) = delete;
    FComponentStorageNode& operator=(FComponentStorageNode&&) = delete;

    bool IsBound() const noexcept { return Owner != nullptr; }

protected:
    using FRemoveThunk = bool (*)(FComponentStorageNode&, FEntity) noexcept;
    using FClearThunk = void (*)(FComponentStorageNode&) noexcept;

    FComponentStorageNode(FRemoveThunk InRemove, FClearThunk InClear) noexcept
        : RemoveThunk(InRemove)
        , ClearThunk(InClear)
    {
    }

    ~FComponentStorageNode();

    uint64 GetBoundWorldDomain() const noexcept { return WorldDomain; }

private:
    friend class FWorld;
    friend void Detail::DetachComponentStorage(FComponentStorageNode& Storage) noexcept;

    FWorld* Owner = nullptr;
    FComponentStorageNode* Previous = nullptr;
    FComponentStorageNode* Next = nullptr;
    uint64 WorldDomain = 0;
    FRemoveThunk RemoveThunk = nullptr;
    FClearThunk ClearThunk = nullptr;
};

enum class EComponentMutationResult : uint8
{
    Added = 0,
    Replaced,
    AlreadyPresent,
    InvalidEntity,
    InvalidStorage,
    AllocationFailed,
};

enum class EComponentRemoveResult : uint8
{
    Removed = 0,
    NotPresent,
    InvalidEntity,
    InvalidStorage,
};

template <typename T>
class TComponentStorage final : public FComponentStorageNode
{
    static_assert(std::is_nothrow_move_constructible<T>::value,
        "component storage requires nothrow move construction");
    static_assert(std::is_nothrow_destructible<T>::value,
        "component storage requires nothrow destruction");

public:
    using SizeType = std::size_t;

    TComponentStorage() noexcept
        : FComponentStorageNode(&RemoveThunkImpl, &ClearThunkImpl)
        , Allocator(&GetDefaultAllocator())
    {
    }

    explicit TComponentStorage(IAllocator& InAllocator) noexcept
        : FComponentStorageNode(&RemoveThunkImpl, &ClearThunkImpl)
        , Allocator(&InAllocator)
    {
    }

    ~TComponentStorage()
    {
        Detail::DetachComponentStorage(*this);
        DestroyAndDeallocate();
    }

    TComponentStorage(const TComponentStorage&) = delete;
    TComponentStorage& operator=(const TComponentStorage&) = delete;
    TComponentStorage(TComponentStorage&&) = delete;
    TComponentStorage& operator=(TComponentStorage&&) = delete;

    SizeType Size() const noexcept { return SizeValue; }
    bool IsEmpty() const noexcept { return SizeValue == 0; }
    uint64 GetMutationVersion() const noexcept { return MutationVersion; }
    IAllocator& GetAllocator() const noexcept { return *Allocator; }

    Span<T> GetComponents() noexcept { return Span<T>(DenseComponents, SizeValue); }
    Span<const T> GetComponents() const noexcept
    {
        return Span<const T>(DenseComponents, SizeValue);
    }
    Span<const FEntity> GetEntities() const noexcept
    {
        return Span<const FEntity>(DenseEntities, SizeValue);
    }

private:
    friend class FWorld;

    struct FSparseEntry
    {
        SizeType DenseIndex = InvalidDenseIndex();
        uint32 Generation = FEntitySlotHandle::InvalidGeneration;
    };

    struct FLayout
    {
        SizeType SparseOffset = 0;
        SizeType EntitiesOffset = 0;
        SizeType ComponentsOffset = 0;
        SizeType TotalSize = 0;
        SizeType Alignment = 0;
    };

    static constexpr SizeType InvalidDenseIndex() noexcept
    {
        return (std::numeric_limits<SizeType>::max)();
    }

    static bool TryAddSize(
        const SizeType Left,
        const SizeType Right,
        SizeType& Out) noexcept
    {
        if (Right > (std::numeric_limits<SizeType>::max)() - Left)
        {
            Out = 0;
            return false;
        }
        Out = Left + Right;
        return true;
    }

    static bool TryAlignUp(
        const SizeType Value,
        const SizeType Alignment,
        SizeType& Out) noexcept
    {
        const SizeType Mask = Alignment - 1;
        if (Value > (std::numeric_limits<SizeType>::max)() - Mask)
        {
            Out = 0;
            return false;
        }
        Out = (Value + Mask) & ~Mask;
        return true;
    }

    static bool TryComputeLayout(
        const SizeType SparseCapacity,
        const SizeType DenseCapacity,
        FLayout& Out) noexcept
    {
        FLayout Candidate;
        Candidate.Alignment = alignof(FSparseEntry);
        if (Candidate.Alignment < alignof(FEntity)) Candidate.Alignment = alignof(FEntity);
        if (Candidate.Alignment < alignof(T)) Candidate.Alignment = alignof(T);

        SizeType SparseBytes = 0;
        SizeType EntityBytes = 0;
        SizeType ComponentBytes = 0;
        if (!TryMultiplySize(sizeof(FSparseEntry), SparseCapacity, SparseBytes) ||
            !TryMultiplySize(sizeof(FEntity), DenseCapacity, EntityBytes) ||
            !TryMultiplySize(sizeof(T), DenseCapacity, ComponentBytes))
        {
            return false;
        }

        SizeType Cursor = SparseBytes;
        if (!TryAlignUp(Cursor, alignof(FEntity), Candidate.EntitiesOffset) ||
            !TryAddSize(Candidate.EntitiesOffset, EntityBytes, Cursor) ||
            !TryAlignUp(Cursor, alignof(T), Candidate.ComponentsOffset) ||
            !TryAddSize(Candidate.ComponentsOffset, ComponentBytes, Candidate.TotalSize))
        {
            return false;
        }
        Out = Candidate;
        return true;
    }

    static SizeType NextCapacity(const SizeType Current, const SizeType Required) noexcept
    {
        SizeType Candidate = Current == 0 ? 4 : Current;
        while (Candidate < Required)
        {
            if (Candidate > (std::numeric_limits<SizeType>::max)() / 2)
            {
                return Required;
            }
            Candidate *= 2;
        }
        return Candidate;
    }

    bool TryEnsureCapacity(const SizeType RequiredSparse, const SizeType RequiredDense) noexcept
    {
        if (RequiredSparse <= SparseCapacity && RequiredDense <= DenseCapacity)
        {
            return true;
        }

        const SizeType NewSparseCapacity = NextCapacity(SparseCapacity, RequiredSparse);
        const SizeType NewDenseCapacity = NextCapacity(DenseCapacity, RequiredDense);
        if (NewSparseCapacity < RequiredSparse || NewDenseCapacity < RequiredDense)
        {
            return false;
        }

        FLayout Layout;
        if (!TryComputeLayout(NewSparseCapacity, NewDenseCapacity, Layout) ||
            Layout.TotalSize == 0)
        {
            return false;
        }

        void* const NewBlock = TryAllocateBytes(*Allocator, Layout.TotalSize, Layout.Alignment);
        if (NewBlock == nullptr)
        {
            return false;
        }

        uint8* const Bytes = static_cast<uint8*>(NewBlock);
        FSparseEntry* const NewSparse = reinterpret_cast<FSparseEntry*>(
            Bytes + Layout.SparseOffset);
        FEntity* const NewEntities = reinterpret_cast<FEntity*>(
            Bytes + Layout.EntitiesOffset);
        T* const NewComponents = reinterpret_cast<T*>(
            Bytes + Layout.ComponentsOffset);

        for (SizeType Index = 0; Index < NewSparseCapacity; ++Index)
        {
            new (NewSparse + Index) FSparseEntry();
        }
        for (SizeType Index = 0; Index < SparseCapacity; ++Index)
        {
            NewSparse[Index] = SparseEntries[Index];
        }
        for (SizeType Index = 0; Index < SizeValue; ++Index)
        {
            new (NewEntities + Index) FEntity(DenseEntities[Index]);
        }
        RelocateConstructRange(NewComponents, DenseComponents, SizeValue);

        if (Block != nullptr)
        {
            Allocator->Deallocate(Block, BlockSize, BlockAlignment);
        }
        Block = NewBlock;
        BlockSize = Layout.TotalSize;
        BlockAlignment = Layout.Alignment;
        SparseEntries = NewSparse;
        DenseEntities = NewEntities;
        DenseComponents = NewComponents;
        SparseCapacity = NewSparseCapacity;
        DenseCapacity = NewDenseCapacity;
        return true;
    }

    bool IsEntityRepresentable(const FEntity Entity) const noexcept
    {
        return Entity.IsValid() && Entity.WorldDomain == GetBoundWorldDomain();
    }

    SizeType FindDenseIndex(const FEntity Entity) const noexcept
    {
        if (!IsEntityRepresentable(Entity) ||
            static_cast<SizeType>(Entity.GetIndex()) >= SparseCapacity)
        {
            return InvalidDenseIndex();
        }
        const FSparseEntry& Entry = SparseEntries[static_cast<SizeType>(Entity.GetIndex())];
        if (Entry.Generation != Entity.GetGeneration() || Entry.DenseIndex >= SizeValue ||
            DenseEntities[Entry.DenseIndex] != Entity)
        {
            return InvalidDenseIndex();
        }
        return Entry.DenseIndex;
    }

    SizeType FindAlias(const T* const Value) const noexcept
    {
        if (Value == nullptr || DenseComponents == nullptr)
        {
            return InvalidDenseIndex();
        }
        const std::uintptr_t Address = reinterpret_cast<std::uintptr_t>(Value);
        const std::uintptr_t Begin = reinterpret_cast<std::uintptr_t>(DenseComponents);
        SizeType ByteSize = 0;
        if (!TryMultiplySize(sizeof(T), SizeValue, ByteSize) || Address < Begin ||
            Address - Begin >= ByteSize || (Address - Begin) % sizeof(T) != 0)
        {
            return InvalidDenseIndex();
        }
        return static_cast<SizeType>((Address - Begin) / sizeof(T));
    }

    template <typename ValueType>
    EComponentMutationResult TryAddValue(const FEntity Entity, ValueType&& Value) noexcept
    {
        if (FindDenseIndex(Entity) != InvalidDenseIndex())
        {
            return EComponentMutationResult::AlreadyPresent;
        }

        const SizeType EntityIndex = static_cast<SizeType>(Entity.GetIndex());
        const SizeType AliasIndex = FindAlias(&Value);
        if (EntityIndex == (std::numeric_limits<SizeType>::max)() ||
            !TryEnsureCapacity(EntityIndex + 1, SizeValue + 1))
        {
            return EComponentMutationResult::AllocationFailed;
        }

        if constexpr (std::is_const<typename std::remove_reference<ValueType>::type>::value)
        {
            const T& Rebound = AliasIndex == InvalidDenseIndex()
                ? Value
                : DenseComponents[AliasIndex];
            new (DenseComponents + SizeValue) T(Rebound);
        }
        else
        {
            T& Rebound = AliasIndex == InvalidDenseIndex()
                ? Value
                : DenseComponents[AliasIndex];
            new (DenseComponents + SizeValue) T(std::move(Rebound));
        }
        new (DenseEntities + SizeValue) FEntity(Entity);
        SparseEntries[EntityIndex].DenseIndex = SizeValue;
        SparseEntries[EntityIndex].Generation = Entity.GetGeneration();
        ++SizeValue;
        AdvanceMutationVersion();
        return EComponentMutationResult::Added;
    }

    EComponentMutationResult TryAdd(const FEntity Entity, const T& Value) noexcept
    {
        static_assert(std::is_nothrow_copy_constructible<T>::value,
            "copy Add requires nothrow copy construction");
        return TryAddValue(Entity, Value);
    }

    EComponentMutationResult TryAdd(const FEntity Entity, T&& Value) noexcept
    {
        return TryAddValue(Entity, std::move(Value));
    }

    template <typename ValueType>
    EComponentMutationResult TrySetValue(const FEntity Entity, ValueType&& Value) noexcept
    {
        const SizeType DenseIndex = FindDenseIndex(Entity);
        if (DenseIndex == InvalidDenseIndex())
        {
            return TryAddValue(Entity, std::forward<ValueType>(Value));
        }

        T Staged(std::forward<ValueType>(Value));
        DenseComponents[DenseIndex].~T();
        new (DenseComponents + DenseIndex) T(std::move(Staged));
        AdvanceMutationVersion();
        return EComponentMutationResult::Replaced;
    }

    EComponentMutationResult TrySet(const FEntity Entity, const T& Value) noexcept
    {
        static_assert(std::is_nothrow_copy_constructible<T>::value,
            "copy Set requires nothrow copy construction");
        return TrySetValue(Entity, Value);
    }

    EComponentMutationResult TrySet(const FEntity Entity, T&& Value) noexcept
    {
        return TrySetValue(Entity, std::move(Value));
    }

    T* Find(const FEntity Entity) noexcept
    {
        const SizeType DenseIndex = FindDenseIndex(Entity);
        return DenseIndex == InvalidDenseIndex() ? nullptr : DenseComponents + DenseIndex;
    }

    const T* Find(const FEntity Entity) const noexcept
    {
        const SizeType DenseIndex = FindDenseIndex(Entity);
        return DenseIndex == InvalidDenseIndex() ? nullptr : DenseComponents + DenseIndex;
    }

    bool Remove(const FEntity Entity) noexcept
    {
        const SizeType DenseIndex = FindDenseIndex(Entity);
        if (DenseIndex == InvalidDenseIndex())
        {
            return false;
        }

        const SizeType LastIndex = SizeValue - 1;
        SparseEntries[static_cast<SizeType>(Entity.GetIndex())] = FSparseEntry{};
        DenseComponents[DenseIndex].~T();
        if (DenseIndex != LastIndex)
        {
            new (DenseComponents + DenseIndex) T(std::move(DenseComponents[LastIndex]));
            DenseComponents[LastIndex].~T();
            DenseEntities[DenseIndex] = DenseEntities[LastIndex];
            const FEntity MovedEntity = DenseEntities[DenseIndex];
            FSparseEntry& MovedEntry = SparseEntries[
                static_cast<SizeType>(MovedEntity.GetIndex())];
            MovedEntry.DenseIndex = DenseIndex;
            MovedEntry.Generation = MovedEntity.GetGeneration();
        }
        --SizeValue;
        AdvanceMutationVersion();
        return true;
    }

    void Clear() noexcept
    {
        DestroyRange(DenseComponents, SizeValue);
        SizeValue = 0;
        for (SizeType Index = 0; Index < SparseCapacity; ++Index)
        {
            SparseEntries[Index] = FSparseEntry{};
        }
        AdvanceMutationVersion();
    }

    void DestroyAndDeallocate() noexcept
    {
        DestroyRange(DenseComponents, SizeValue);
        if (Block != nullptr)
        {
            Allocator->Deallocate(Block, BlockSize, BlockAlignment);
        }
        Block = nullptr;
        BlockSize = 0;
        BlockAlignment = 0;
        SparseEntries = nullptr;
        DenseEntities = nullptr;
        DenseComponents = nullptr;
        SparseCapacity = 0;
        DenseCapacity = 0;
        SizeValue = 0;
    }

    void AdvanceMutationVersion() noexcept
    {
        ++MutationVersion;
        if (MutationVersion == 0)
        {
            HandleContractViolation("component storage mutation version overflow", __FILE__, __LINE__);
        }
    }

    static bool RemoveThunkImpl(FComponentStorageNode& Node, const FEntity Entity) noexcept
    {
        return static_cast<TComponentStorage&>(Node).Remove(Entity);
    }

    static void ClearThunkImpl(FComponentStorageNode& Node) noexcept
    {
        static_cast<TComponentStorage&>(Node).Clear();
    }

    IAllocator* Allocator = nullptr;
    void* Block = nullptr;
    SizeType BlockSize = 0;
    SizeType BlockAlignment = 0;
    FSparseEntry* SparseEntries = nullptr;
    FEntity* DenseEntities = nullptr;
    T* DenseComponents = nullptr;
    SizeType SparseCapacity = 0;
    SizeType DenseCapacity = 0;
    SizeType SizeValue = 0;
    uint64 MutationVersion = 1;
};

} // namespace LE
