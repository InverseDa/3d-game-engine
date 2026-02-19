#pragma once

#include "CoreMinimal.h"

#include <functional>

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

namespace std
{
    template<>
    struct hash<FRFGPassHandle>
    {
        size_t operator()(const FRFGPassHandle& Handle) const noexcept
        {
            return hash<uint32>()(Handle.Id);
        }
    };

    template<>
    struct hash<FRFGResourceHandle>
    {
        size_t operator()(const FRFGResourceHandle& Handle) const noexcept
        {
            return hash<uint32>()(Handle.Id);
        }
    };
}
