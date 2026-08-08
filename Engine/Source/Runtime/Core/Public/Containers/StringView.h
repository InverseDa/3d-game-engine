#pragma once

#include "Containers/HashMap.h"

#include <cstddef>
#include <cstring>

namespace LE
{

// A borrowed sequence of UTF-8 bytes. StringView never owns or extends the
// lifetime of its data and does not promise NUL termination.
class StringView
{
public:
    using SizeType = std::size_t;

    constexpr StringView() noexcept = default;

    StringView(const char* const NullTerminated) noexcept
        : DataValue(NullTerminated)
        , SizeValue(NullTerminated == nullptr ? 0 : std::strlen(NullTerminated))
    {
    }

    constexpr StringView(const char* const Data, const SizeType Size) noexcept
        : DataValue(Data)
        , SizeValue(Size)
    {
        if (Data == nullptr && Size != 0)
        {
            HandleContractViolation("non-empty StringView requires non-null data", __FILE__, __LINE__);
        }
    }

    constexpr const char* Data() const noexcept { return DataValue; }
    constexpr SizeType Size() const noexcept { return SizeValue; }
    constexpr SizeType Length() const noexcept { return SizeValue; }
    constexpr bool IsEmpty() const noexcept { return SizeValue == 0; }

    const char& operator[](const SizeType Index) const
    {
        if (Index >= SizeValue)
        {
            HandleContractViolation("StringView index out of bounds", __FILE__, __LINE__);
        }
        return DataValue[Index];
    }

    StringView Substr(const SizeType Offset, const SizeType Count) const
    {
        if (Offset > SizeValue)
        {
            HandleContractViolation("StringView substring offset out of bounds", __FILE__, __LINE__);
        }
        const SizeType Available = SizeValue - Offset;
        const SizeType ResultSize = Count < Available ? Count : Available;
        return StringView(ResultSize == 0 ? nullptr : DataValue + Offset, ResultSize);
    }

    int Compare(const StringView Other) const noexcept
    {
        const SizeType CommonSize = SizeValue < Other.SizeValue ? SizeValue : Other.SizeValue;
        if (CommonSize != 0)
        {
            const int Result = std::memcmp(DataValue, Other.DataValue, CommonSize);
            if (Result != 0) { return Result; }
        }
        if (SizeValue < Other.SizeValue) { return -1; }
        if (SizeValue > Other.SizeValue) { return 1; }
        return 0;
    }

private:
    const char* DataValue = nullptr;
    SizeType SizeValue = 0;
};

inline bool operator==(const StringView Left, const StringView Right) noexcept
{
    if (Left.Size() != Right.Size())
    {
        return false;
    }
    return Left.Size() == 0 || std::memcmp(Left.Data(), Right.Data(), Left.Size()) == 0;
}

inline bool operator!=(const StringView Left, const StringView Right) noexcept
{
    return !(Left == Right);
}

inline bool operator<(const StringView Left, const StringView Right) noexcept
{
    return Left.Compare(Right) < 0;
}

inline std::size_t HashStringBytes(const StringView Value) noexcept
{
    std::size_t Hash = sizeof(std::size_t) > 4
        ? static_cast<std::size_t>(14695981039346656037ULL)
        : static_cast<std::size_t>(2166136261U);
    const std::size_t Prime = sizeof(std::size_t) > 4
        ? static_cast<std::size_t>(1099511628211ULL)
        : static_cast<std::size_t>(16777619U);
    for (std::size_t Index = 0; Index < Value.Size(); ++Index)
    {
        Hash ^= static_cast<unsigned char>(Value.Data()[Index]);
        Hash *= Prime;
    }
    return Hash;
}

struct StringViewHash
{
    using is_transparent = void;
    std::size_t operator()(const StringView Value) const noexcept { return HashStringBytes(Value); }
    std::size_t operator()(const char* const Value) const noexcept { return HashStringBytes(StringView(Value)); }
};

struct StringViewEqual
{
    using is_transparent = void;
    bool operator()(const StringView Left, const StringView Right) const noexcept { return Left == Right; }
    bool operator()(const StringView Left, const char* const Right) const noexcept
    {
        return Left == StringView(Right);
    }
    bool operator()(const char* const Left, const StringView Right) const noexcept
    {
        return StringView(Left) == Right;
    }
};

template <>
struct DefaultHash<StringView> : StringViewHash
{
};

template <>
struct DefaultEqual<StringView> : StringViewEqual
{
};

} // namespace LE
