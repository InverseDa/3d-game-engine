#pragma once

#include "Types/RuntimeHandle.h"
#include "Types/Uuid.h"

namespace LE
{

struct FReflectionTypeIdTag;
struct FReflectionPropertyIdTag;
struct FReflectionModuleHandleTag;

using FTypeId = TStableId<FReflectionTypeIdTag>;
using FPropertyId = TStableId<FReflectionPropertyIdTag>;
using FReflectionModuleHandle = TRuntimeHandle<FReflectionModuleHandleTag>;

enum class EReflectedTypeKind : uint8
{
    Struct = 0,
    Class,
};

enum class EEnumUnderlyingType : uint8
{
    Int8 = 0,
    UInt8,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Int64,
    UInt64,
};

constexpr bool IsValidEnumUnderlyingType(const EEnumUnderlyingType Type) noexcept
{
    return Type == EEnumUnderlyingType::Int8 || Type == EEnumUnderlyingType::UInt8 ||
        Type == EEnumUnderlyingType::Int16 || Type == EEnumUnderlyingType::UInt16 ||
        Type == EEnumUnderlyingType::Int32 || Type == EEnumUnderlyingType::UInt32 ||
        Type == EEnumUnderlyingType::Int64 || Type == EEnumUnderlyingType::UInt64;
}

constexpr uint8 GetEnumUnderlyingBitWidth(const EEnumUnderlyingType Type) noexcept
{
    switch (Type)
    {
    case EEnumUnderlyingType::Int8:
    case EEnumUnderlyingType::UInt8:
        return 8;
    case EEnumUnderlyingType::Int16:
    case EEnumUnderlyingType::UInt16:
        return 16;
    case EEnumUnderlyingType::Int32:
    case EEnumUnderlyingType::UInt32:
        return 32;
    case EEnumUnderlyingType::Int64:
    case EEnumUnderlyingType::UInt64:
        return 64;
    }
    return 0;
}

constexpr bool IsEnumUnderlyingSigned(const EEnumUnderlyingType Type) noexcept
{
    return Type == EEnumUnderlyingType::Int8 ||
        Type == EEnumUnderlyingType::Int16 ||
        Type == EEnumUnderlyingType::Int32 ||
        Type == EEnumUnderlyingType::Int64;
}

constexpr uint64 GetEnumValueMask(const EEnumUnderlyingType Type) noexcept
{
    const uint8 Width = GetEnumUnderlyingBitWidth(Type);
    return Width == 0 ? uint64{ 0 } :
        (Width == 64 ? ~uint64{ 0 } : ((uint64{ 1 } << Width) - 1));
}

constexpr bool IsCanonicalEnumValueBits(
    const EEnumUnderlyingType Type,
    const uint64 ValueBits) noexcept
{
    return IsValidEnumUnderlyingType(Type) &&
        (ValueBits & ~GetEnumValueMask(Type)) == 0;
}

inline bool TryInterpretEnumValueAsSigned(
    const EEnumUnderlyingType Type,
    const uint64 ValueBits,
    int64& OutValue) noexcept
{
    if (!IsValidEnumUnderlyingType(Type) || !IsEnumUnderlyingSigned(Type) ||
        !IsCanonicalEnumValueBits(Type, ValueBits))
    {
        return false;
    }

    const uint8 Width = GetEnumUnderlyingBitWidth(Type);
    if (Width == 64)
    {
        // Convert through the two's-complement magnitude instead of relying on
        // an implementation-defined uint64-to-int64 conversion.
        if ((ValueBits & (uint64{ 1 } << 63)) == 0)
        {
            OutValue = static_cast<int64>(ValueBits);
        }
        else
        {
            const uint64 MagnitudeMinusOne = ~ValueBits;
            OutValue = -static_cast<int64>(MagnitudeMinusOne) - 1;
        }
        return true;
    }

    const uint64 SignBit = uint64{ 1 } << (Width - 1);
    if ((ValueBits & SignBit) == 0)
    {
        OutValue = static_cast<int64>(ValueBits);
        return true;
    }

    const uint64 MagnitudeMinusOne = (~ValueBits) & GetEnumValueMask(Type);
    OutValue = -static_cast<int64>(MagnitudeMinusOne) - 1;
    return true;
}

inline bool TryInterpretEnumValueAsUnsigned(
    const EEnumUnderlyingType Type,
    const uint64 ValueBits,
    uint64& OutValue) noexcept
{
    if (!IsValidEnumUnderlyingType(Type) || IsEnumUnderlyingSigned(Type) ||
        !IsCanonicalEnumValueBits(Type, ValueBits))
    {
        return false;
    }
    OutValue = ValueBits;
    return true;
}

} // namespace LE
