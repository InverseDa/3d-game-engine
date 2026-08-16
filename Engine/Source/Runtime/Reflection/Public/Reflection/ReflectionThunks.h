#pragma once

#include <cstdint>
#include <new>
#include <type_traits>

namespace LE
{

using FReflectionConstructThunk = bool (*)(void* Storage) noexcept;
using FReflectionDestructThunk = bool (*)(void* Instance) noexcept;
using FReflectionPropertyGetterThunk = const void* (*)(const void* Instance) noexcept;
using FReflectionPropertySetterThunk = bool (*)(void* Instance, const void* Value) noexcept;

namespace Detail
{
template <typename T>
bool IsReflectionPointerAligned(const void* const Pointer) noexcept
{
    return Pointer != nullptr &&
        (reinterpret_cast<std::uintptr_t>(Pointer) % alignof(T)) == 0;
}
} // namespace Detail

// These helpers are the hand-written P0 backend. Future generated code and a
// standard-reflection backend can emit the same plain function-pointer ABI.
template <typename T>
struct TDefaultReflectionThunks
{
    static bool Construct(void* const Storage) noexcept
    {
        static_assert(std::is_nothrow_default_constructible<T>::value,
            "reflected default construction must be noexcept");
        if (!Detail::IsReflectionPointerAligned<T>(Storage))
        {
            return false;
        }
        new (Storage) T();
        return true;
    }

    static bool Destruct(void* const Instance) noexcept
    {
        static_assert(std::is_nothrow_destructible<T>::value,
            "reflected destruction must be noexcept");
        if (!Detail::IsReflectionPointerAligned<T>(Instance))
        {
            return false;
        }
        static_cast<T*>(Instance)->~T();
        return true;
    }
};

template <auto Member>
struct TMemberReflectionThunks;

template <typename ObjectType, typename ValueType, ValueType ObjectType::* Member>
struct TMemberReflectionThunks<Member>
{
    static const void* Get(const void* const Instance) noexcept
    {
        if (!Detail::IsReflectionPointerAligned<ObjectType>(Instance))
        {
            return nullptr;
        }
        return &(static_cast<const ObjectType*>(Instance)->*Member);
    }

    static bool Set(void* const Instance, const void* const Value) noexcept
    {
        static_assert(std::is_nothrow_assignable<ValueType&, const ValueType&>::value,
            "reflected property assignment must be noexcept");
        if (!Detail::IsReflectionPointerAligned<ObjectType>(Instance) ||
            !Detail::IsReflectionPointerAligned<ValueType>(Value))
        {
            return false;
        }
        static_cast<ObjectType*>(Instance)->*Member = *static_cast<const ValueType*>(Value);
        return true;
    }
};

} // namespace LE
