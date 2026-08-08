#pragma once

#include "Containers/Span.h"

#include <cstddef>
#include <utility>

namespace LE
{

template <typename T, std::size_t N>
class StaticArray
{
public:
    using ValueType = T;
    using SizeType = std::size_t;
    using Iterator = T*;
    using ConstIterator = const T*;

    constexpr SizeType Size() const noexcept { return N; }
    constexpr bool IsEmpty() const noexcept { return false; }
    constexpr T* Data() noexcept { return Elements; }
    constexpr const T* Data() const noexcept { return Elements; }

    T& operator[](const SizeType Index)
    {
#if !defined(NDEBUG)
        if (Index >= N)
        {
            HandleContractViolation("StaticArray index out of bounds", __FILE__, __LINE__);
        }
#endif
        return Elements[Index];
    }

    const T& operator[](const SizeType Index) const
    {
#if !defined(NDEBUG)
        if (Index >= N)
        {
            HandleContractViolation("StaticArray index out of bounds", __FILE__, __LINE__);
        }
#endif
        return Elements[Index];
    }

    T* TryGet(const SizeType Index) noexcept { return Index < N ? Elements + Index : nullptr; }
    const T* TryGet(const SizeType Index) const noexcept { return Index < N ? Elements + Index : nullptr; }

    Span<T> AsSpan() & noexcept { return Span<T>(Elements, N); }
    Span<const T> AsSpan() const & noexcept { return Span<const T>(Elements, N); }
    Span<T> AsSpan() && = delete;
    Span<const T> AsSpan() const && = delete;

    Iterator begin() noexcept { return Elements; }
    Iterator end() noexcept { return Elements + N; }
    ConstIterator begin() const noexcept { return Elements; }
    ConstIterator end() const noexcept { return Elements + N; }

private:
    T Elements[N]{};
};

template <typename T>
class StaticArray<T, 0>
{
public:
    using ValueType = T;
    using SizeType = std::size_t;
    using Iterator = T*;
    using ConstIterator = const T*;

    constexpr SizeType Size() const noexcept { return 0; }
    constexpr bool IsEmpty() const noexcept { return true; }
    constexpr T* Data() noexcept { return nullptr; }
    constexpr const T* Data() const noexcept { return nullptr; }

    T& operator[](const SizeType)
    {
        HandleContractViolation("empty StaticArray cannot be indexed", __FILE__, __LINE__);
    }
    const T& operator[](const SizeType) const
    {
        HandleContractViolation("empty StaticArray cannot be indexed", __FILE__, __LINE__);
    }

    T* TryGet(const SizeType) noexcept { return nullptr; }
    const T* TryGet(const SizeType) const noexcept { return nullptr; }

    Span<T> AsSpan() & noexcept { return {}; }
    Span<const T> AsSpan() const & noexcept { return {}; }
    Span<T> AsSpan() && = delete;
    Span<const T> AsSpan() const && = delete;

    Iterator begin() noexcept { return nullptr; }
    Iterator end() noexcept { return nullptr; }
    ConstIterator begin() const noexcept { return nullptr; }
    ConstIterator end() const noexcept { return nullptr; }
};

} // namespace LE
