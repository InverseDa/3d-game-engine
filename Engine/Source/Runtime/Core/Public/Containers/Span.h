#pragma once

#include "Memory/Allocator.h"

#include <cstddef>
#include <type_traits>

namespace LE
{

template <typename T>
class Span
{
public:
    using ElementType = T;
    using ValueType = typename std::remove_cv<T>::type;
    using SizeType = std::size_t;
    using Iterator = T*;

    constexpr Span() noexcept = default;

    constexpr Span(T* const Data, const SizeType Size) noexcept
        : DataValue(Size == 0 ? nullptr : Data)
        , SizeValue(Size)
    {
        if (Data == nullptr && Size != 0)
        {
            HandleContractViolation("non-empty Span requires non-null data", __FILE__, __LINE__);
        }
    }

    template <std::size_t N>
    constexpr Span(T (&Items)[N]) noexcept
        : DataValue(Items)
        , SizeValue(N)
    {
    }

    template <
        typename U,
        typename std::enable_if<std::is_convertible<U (*)[], T (*)[]>::value, int>::type = 0>
    constexpr Span(const Span<U> Other) noexcept
        : DataValue(Other.Data())
        , SizeValue(Other.Size())
    {
    }

    constexpr T* Data() const noexcept { return DataValue; }
    constexpr SizeType Size() const noexcept { return SizeValue; }
    bool TrySizeBytes(SizeType& Result) const noexcept
    {
        return TryMultiplySize(SizeValue, sizeof(T), Result);
    }

    SizeType SizeBytes() const
    {
        SizeType Result = 0;
        if (!TrySizeBytes(Result))
        {
            HandleContractViolation("Span byte size overflow", __FILE__, __LINE__);
        }
        return Result;
    }
    constexpr bool IsEmpty() const noexcept { return SizeValue == 0; }

    T& operator[](const SizeType Index) const
    {
#if !defined(NDEBUG)
        if (Index >= SizeValue)
        {
            HandleContractViolation("Span index out of bounds", __FILE__, __LINE__);
        }
#endif
        return DataValue[Index];
    }

    T* TryGet(const SizeType Index) const noexcept
    {
        return Index < SizeValue ? DataValue + Index : nullptr;
    }

    Span Subspan(const SizeType Offset, const SizeType Count) const
    {
        if (Offset > SizeValue || Count > SizeValue - Offset)
        {
            HandleContractViolation("Span subspan out of bounds", __FILE__, __LINE__);
        }
        return Span(DataValue == nullptr ? nullptr : DataValue + Offset, Count);
    }

    Span First(const SizeType Count) const { return Subspan(0, Count); }
    Span Last(const SizeType Count) const
    {
        if (Count > SizeValue)
        {
            HandleContractViolation("Span last out of bounds", __FILE__, __LINE__);
        }
        return Subspan(SizeValue - Count, Count);
    }

    Iterator begin() const noexcept { return DataValue; }
    Iterator end() const noexcept { return DataValue == nullptr ? nullptr : DataValue + SizeValue; }

private:
    T* DataValue = nullptr;
    SizeType SizeValue = 0;
};

} // namespace LE
