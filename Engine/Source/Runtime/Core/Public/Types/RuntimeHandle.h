#pragma once

#include "Containers/Array.h"
#include "Containers/HashMap.h"
#include "Types/EngineTypes.h"

#include <cstddef>
#include <limits>
#include <type_traits>

namespace LE
{

template <typename DomainTag, typename IndexType = uint32, typename GenerationType = uint32>
struct TRuntimeHandle
{
    static_assert(std::is_integral<IndexType>::value && std::is_unsigned<IndexType>::value
        && !std::is_same<IndexType, bool>::value && sizeof(IndexType) <= sizeof(std::size_t),
        "runtime handle index must be a non-bool unsigned integer no wider than size_t");
    static_assert(std::is_integral<GenerationType>::value && std::is_unsigned<GenerationType>::value
        && !std::is_same<GenerationType, bool>::value,
        "runtime handle generation must be a non-bool unsigned integer");

    static constexpr IndexType InvalidIndex = (std::numeric_limits<IndexType>::max)();
    static constexpr GenerationType InvalidGeneration = 0;

    IndexType Index = InvalidIndex;
    GenerationType Generation = InvalidGeneration;

    bool IsValid() const noexcept
    {
        return Index != InvalidIndex && Generation != InvalidGeneration;
    }

    void Reset() noexcept
    {
        Index = InvalidIndex;
        Generation = InvalidGeneration;
    }

    friend bool operator==(const TRuntimeHandle& Left, const TRuntimeHandle& Right) noexcept
    {
        return Left.Index == Right.Index && Left.Generation == Right.Generation;
    }

    friend bool operator!=(const TRuntimeHandle& Left, const TRuntimeHandle& Right) noexcept
    {
        return !(Left == Right);
    }
};

template <typename DomainTag, typename IndexType, typename GenerationType>
struct DefaultHash<TRuntimeHandle<DomainTag, IndexType, GenerationType>>
{
    std::size_t operator()(
        const TRuntimeHandle<DomainTag, IndexType, GenerationType>& Handle) const noexcept
    {
        const std::size_t IndexHash = DefaultHash<IndexType>{}(Handle.Index);
        const std::size_t GenerationHash = DefaultHash<GenerationType>{}(Handle.Generation);
        return IndexHash ^ (GenerationHash + static_cast<std::size_t>(0x9e3779b9U)
            + (IndexHash << 6) + (IndexHash >> 2));
    }
};

// A minimal, pool-relative issuance domain for runtime handles. It owns no
// business objects, is intentionally non-copyable/non-movable, and is not
// thread-safe. Callers must externally synchronize access and must not mix
// handles between distinct pools that use the same DomainTag.
template <typename DomainTag, typename IndexType = uint32, typename GenerationType = uint32>
class TRuntimeHandlePool
{
public:
    using HandleType = TRuntimeHandle<DomainTag, IndexType, GenerationType>;
    using SizeType = std::size_t;

    TRuntimeHandlePool() noexcept = default;
    explicit TRuntimeHandlePool(IAllocator& Allocator) noexcept
        : Slots(Allocator)
        , FreeIndices(Allocator)
    {
    }
    ~TRuntimeHandlePool() = default;

    TRuntimeHandlePool(const TRuntimeHandlePool&) = delete;
    TRuntimeHandlePool& operator=(const TRuntimeHandlePool&) = delete;
    TRuntimeHandlePool(TRuntimeHandlePool&&) = delete;
    TRuntimeHandlePool& operator=(TRuntimeHandlePool&&) = delete;

    bool TryAllocate(HandleType& OutHandle)
    {
        while (!FreeIndices.IsEmpty())
        {
            const IndexType Index = FreeIndices.Back();
            FreeIndices.PopBack();
            FSlot& Slot = Slots[static_cast<SizeType>(Index)];
            if (Slot.bRetired || Slot.bAlive)
            {
                continue;
            }
            if (Slot.Generation == MaximumGeneration())
            {
                Slot.bRetired = true;
                continue;
            }

            ++Slot.Generation;
            Slot.bAlive = true;
            ++AliveCountValue;
            OutHandle = HandleType{ Index, Slot.Generation };
            return true;
        }

        if (Slots.Size() >= static_cast<SizeType>(HandleType::InvalidIndex))
        {
            return false;
        }

        FSlot NewSlot;
        NewSlot.Generation = static_cast<GenerationType>(1);
        NewSlot.bAlive = true;
        if (!Slots.TryPushBack(NewSlot))
        {
            return false;
        }

        const IndexType Index = static_cast<IndexType>(Slots.Size() - 1);
        ++AliveCountValue;
        OutHandle = HandleType{ Index, NewSlot.Generation };
        return true;
    }

    bool Release(const HandleType Handle)
    {
        if (!IsAlive(Handle))
        {
            return false;
        }

        FSlot& Slot = Slots[static_cast<SizeType>(Handle.Index)];
        Slot.bAlive = false;
        --AliveCountValue;

        if (Slot.Generation == MaximumGeneration())
        {
            Slot.bRetired = true;
            return true;
        }

        if (!FreeIndices.TryPushBack(Handle.Index))
        {
            // Release has completed. Safely sacrifice future reuse rather than
            // invoking the global OOM path or risking an untracked free slot.
            Slot.bRetired = true;
        }
        return true;
    }

    bool IsAlive(const HandleType Handle) const noexcept
    {
        if (!Handle.IsValid() || static_cast<SizeType>(Handle.Index) >= Slots.Size())
        {
            return false;
        }
        const FSlot& Slot = Slots[static_cast<SizeType>(Handle.Index)];
        return Slot.bAlive && !Slot.bRetired && Slot.Generation == Handle.Generation;
    }

    SizeType GetSlotCount() const noexcept { return Slots.Size(); }
    SizeType GetAliveCount() const noexcept { return AliveCountValue; }

private:
    struct FSlot
    {
        GenerationType Generation = 0;
        bool bAlive = false;
        bool bRetired = false;
    };

    static constexpr GenerationType MaximumGeneration() noexcept
    {
        return (std::numeric_limits<GenerationType>::max)();
    }

    Array<FSlot> Slots;
    Array<IndexType> FreeIndices;
    SizeType AliveCountValue = 0;
};

} // namespace LE
