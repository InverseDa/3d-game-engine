#pragma once

#include "Memory/Allocator.h"

#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace LE
{

template <typename T>
struct IsBitwiseRelocatable : std::is_trivially_copyable<T>
{
};

template <typename T>
constexpr bool IsBitwiseRelocatableV = IsBitwiseRelocatable<T>::value;

template <typename T>
void DestroyRange(T* const Data, const std::size_t Count) noexcept
{
    if constexpr (!std::is_trivially_destructible<T>::value)
    {
        for (std::size_t Index = Count; Index > 0; --Index)
        {
            Data[Index - 1].~T();
        }
    }
}

template <typename T>
void RelocateConstructRange(T* const Destination, T* const Source, const std::size_t Count)
{
    if (Count == 0 || Destination == Source)
    {
        return;
    }

    if constexpr (IsBitwiseRelocatableV<T>)
    {
        std::size_t Size = 0;
        if (!TryMultiplySize(sizeof(T), Count, Size))
        {
            HandleContractViolation("relocation byte size overflow", __FILE__, __LINE__);
        }
        std::memmove(Destination, Source, Size);
    }
    else
    {
        for (std::size_t Index = 0; Index < Count; ++Index)
        {
            new (Destination + Index) T(std::move(Source[Index]));
        }
        DestroyRange(Source, Count);
    }
}

} // namespace LE
