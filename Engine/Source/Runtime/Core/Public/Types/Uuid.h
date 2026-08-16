#pragma once

#include "Containers/HashMap.h"
#include "Containers/Span.h"
#include "Containers/String.h"
#include "Containers/StringView.h"
#include "Types/EngineTypes.h"

#include <cstddef>

namespace LE
{

// A UUID value stored in RFC network byte order. The nil UUID is the invalid
// sentinel; parsing the nil spelling still succeeds as UUID syntax.
class CORE_API FUuid
{
public:
    static constexpr std::size_t ByteCount = 16;
    static constexpr std::size_t CanonicalStringLength = 36;

    constexpr FUuid() noexcept = default;

    bool IsValid() const noexcept;
    uint8 GetVersion() const noexcept { return static_cast<uint8>(Bytes[6] >> 4); }
    bool HasRfcVariant() const noexcept { return (Bytes[8] & 0xC0u) == 0x80u; }
    Span<const uint8> GetBytes() const noexcept { return Span<const uint8>(Bytes, ByteCount); }

    String ToString() const;

    // Accepts only the lowercase canonical 8-4-4-4-12 spelling. On failure,
    // OutUuid is unchanged.
    static bool TryParse(StringView Text, FUuid& OutUuid) noexcept;

    // Produces an RFC variant, version-4 UUID using the operating-system CSPRNG.
    // Unsupported platforms and entropy failures return false without changing
    // OutUuid.
    static bool TryGenerate(FUuid& OutUuid) noexcept;

    friend CORE_API bool operator==(const FUuid& Left, const FUuid& Right) noexcept;
    friend bool operator!=(const FUuid& Left, const FUuid& Right) noexcept { return !(Left == Right); }
    friend CORE_API bool operator<(const FUuid& Left, const FUuid& Right) noexcept;

private:
    uint8 Bytes[ByteCount]{};
};

CORE_API bool operator==(const FUuid& Left, const FUuid& Right) noexcept;
CORE_API bool operator<(const FUuid& Left, const FUuid& Right) noexcept;

struct CORE_API FUuidHash
{
    std::size_t operator()(const FUuid& Value) const noexcept;
};

template <>
struct DefaultHash<FUuid> : FUuidHash
{
};

// DomainTag provides compile-time separation without assigning Entity, Asset,
// reflection, or serialization meaning in Core.
template <typename DomainTag>
class TStableId
{
public:
    constexpr TStableId() noexcept = default;

    static TStableId FromUuid(const FUuid& Uuid) noexcept
    {
        TStableId Result;
        Result.Value = Uuid;
        return Result;
    }

    bool IsValid() const noexcept { return Value.IsValid(); }
    const FUuid& GetUuid() const noexcept { return Value; }
    String ToString() const { return Value.ToString(); }

    static bool TryParse(const StringView Text, TStableId& OutId) noexcept
    {
        FUuid Parsed;
        if (!FUuid::TryParse(Text, Parsed) || !Parsed.IsValid())
        {
            return false;
        }
        OutId.Value = Parsed;
        return true;
    }

    static bool TryGenerate(TStableId& OutId) noexcept
    {
        FUuid Generated;
        if (!FUuid::TryGenerate(Generated))
        {
            return false;
        }
        OutId.Value = Generated;
        return true;
    }

    friend bool operator==(const TStableId& Left, const TStableId& Right) noexcept
    {
        return Left.Value == Right.Value;
    }

    friend bool operator!=(const TStableId& Left, const TStableId& Right) noexcept
    {
        return !(Left == Right);
    }

    friend bool operator<(const TStableId& Left, const TStableId& Right) noexcept
    {
        return Left.Value < Right.Value;
    }

private:
    FUuid Value;
};

template <typename DomainTag>
struct DefaultHash<TStableId<DomainTag>>
{
    std::size_t operator()(const TStableId<DomainTag>& Value) const noexcept
    {
        return FUuidHash{}(Value.GetUuid());
    }
};

} // namespace LE
