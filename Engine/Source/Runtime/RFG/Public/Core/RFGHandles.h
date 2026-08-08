#pragma once

#include "CoreMinimal.h"

namespace LE
{

struct FRFGPassHandle
{
    static constexpr uint32 InvalidId = 0xFFFFFFFFu;

    uint32 Id = InvalidId;

    bool IsValid() const { return Id != InvalidId; }
    void Reset() { Id = InvalidId; }

    friend bool operator==(const FRFGPassHandle& Lhs, const FRFGPassHandle& Rhs)
    {
        return Lhs.Id == Rhs.Id;
    }
};

struct FRFGResourceHandle
{
    static constexpr uint32 InvalidId = 0xFFFFFFFFu;

    uint32 Id = InvalidId;

    bool IsValid() const { return Id != InvalidId; }
    void Reset() { Id = InvalidId; }

    friend bool operator==(const FRFGResourceHandle& Lhs, const FRFGResourceHandle& Rhs)
    {
        return Lhs.Id == Rhs.Id;
    }
};

struct FRFGPassHandleHash
{
    std::size_t operator()(const FRFGPassHandle& Handle) const noexcept
    {
        return LE::DefaultHash<uint32>{}(Handle.Id);
    }
};

struct FRFGResourceHandleHash
{
    std::size_t operator()(const FRFGResourceHandle& Handle) const noexcept
    {
        return LE::DefaultHash<uint32>{}(Handle.Id);
    }
};

} // namespace LE
