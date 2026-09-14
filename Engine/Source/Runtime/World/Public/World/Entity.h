#pragma once

#include "CoreMinimal.h"
#include "Types/RuntimeHandle.h"

#include <cstddef>
#include <type_traits>

namespace LE
{

struct FEntitySlotTag;
using FEntitySlotHandle = TRuntimeHandle<FEntitySlotTag>;

// FEntity is a non-owning, World-relative runtime identity. The slot keeps the
// index+generation ABA protection while WorldDomain rejects handles issued by
// another FWorld whose slot happens to have the same pair.
struct FEntity
{
    FEntitySlotHandle Slot;
    uint64 WorldDomain = 0;

    bool IsValid() const noexcept
    {
        return WorldDomain != 0 && Slot.IsValid();
    }

    uint32 GetIndex() const noexcept { return Slot.Index; }
    uint32 GetGeneration() const noexcept { return Slot.Generation; }

    void Reset() noexcept
    {
        Slot.Reset();
        WorldDomain = 0;
    }

    friend bool operator==(const FEntity& Left, const FEntity& Right) noexcept
    {
        return Left.Slot == Right.Slot && Left.WorldDomain == Right.WorldDomain;
    }

    friend bool operator!=(const FEntity& Left, const FEntity& Right) noexcept
    {
        return !(Left == Right);
    }
};

template <>
struct DefaultHash<FEntity>
{
    std::size_t operator()(const FEntity& Entity) const noexcept
    {
        const std::size_t SlotHash = DefaultHash<FEntitySlotHandle>{}(Entity.Slot);
        const std::size_t DomainHash = DefaultHash<uint64>{}(Entity.WorldDomain);
        return SlotHash ^ (DomainHash + static_cast<std::size_t>(0x9e3779b9U)
            + (SlotHash << 6) + (SlotHash >> 2));
    }
};

static_assert(std::is_trivially_copyable<FEntity>::value,
    "FEntity must remain a trivially copyable runtime identity");

} // namespace LE
