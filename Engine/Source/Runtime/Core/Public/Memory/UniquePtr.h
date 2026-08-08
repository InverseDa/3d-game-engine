#pragma once

#include "Memory/Allocator.h"

#include <new>
#include <type_traits>
#include <utility>

namespace LE
{

template <typename T>
class UniquePtr
{
public:
    using DestroyFunction = void (*)(void* Context, T* Object) noexcept;

    UniquePtr() noexcept = default;
    UniquePtr(std::nullptr_t) noexcept {}

    UniquePtr(const UniquePtr&) = delete;
    UniquePtr& operator=(const UniquePtr&) = delete;

    UniquePtr(UniquePtr&& Other) noexcept
        : ObjectValue(Other.ObjectValue)
        , ContextValue(Other.ContextValue)
        , DestroyValue(Other.DestroyValue)
    {
        Other.ObjectValue = nullptr;
        Other.ContextValue = nullptr;
        Other.DestroyValue = nullptr;
    }

    UniquePtr& operator=(UniquePtr&& Other) noexcept
    {
        if (this != &Other)
        {
            Reset();
            ObjectValue = Other.ObjectValue;
            ContextValue = Other.ContextValue;
            DestroyValue = Other.DestroyValue;
            Other.ObjectValue = nullptr;
            Other.ContextValue = nullptr;
            Other.DestroyValue = nullptr;
        }
        return *this;
    }

    ~UniquePtr() { Reset(); }

    static UniquePtr Adopt(
        T* const Object,
        void* const Context,
        const DestroyFunction Destroy) noexcept
    {
        if (Object != nullptr && Destroy == nullptr)
        {
            HandleContractViolation("UniquePtr adoption requires a destroy route", __FILE__, __LINE__);
        }
        return UniquePtr(Object, Context, Destroy);
    }

    T* Get() const noexcept { return ObjectValue; }
    explicit operator bool() const noexcept { return ObjectValue != nullptr; }
    T& operator*() const
    {
        Validate();
        return *ObjectValue;
    }
    T* operator->() const
    {
        Validate();
        return ObjectValue;
    }

    void Reset() noexcept
    {
        if (ObjectValue != nullptr)
        {
            DestroyValue(ContextValue, ObjectValue);
        }
        ObjectValue = nullptr;
        ContextValue = nullptr;
        DestroyValue = nullptr;
    }

private:
    template <typename U, typename... Arguments>
    friend UniquePtr<U> TryMakeUniqueWithAllocator(IAllocator&, Arguments&&...);

    UniquePtr(T* const Object, void* const Context, const DestroyFunction Destroy) noexcept
        : ObjectValue(Object)
        , ContextValue(Context)
        , DestroyValue(Destroy)
    {
    }

    void Validate() const
    {
        if (ObjectValue == nullptr)
        {
            HandleContractViolation("dereferenced empty UniquePtr", __FILE__, __LINE__);
        }
    }

    T* ObjectValue = nullptr;
    void* ContextValue = nullptr;
    DestroyFunction DestroyValue = nullptr;
};

namespace Detail
{
template <typename T>
void DestroyUniqueAllocated(void* const Context, T* const Object) noexcept
{
    IAllocator& Allocator = *static_cast<IAllocator*>(Context);
    Object->~T();
    DeallocateArray(Allocator, Object, 1);
}
} // namespace Detail

template <typename T, typename... Arguments>
UniquePtr<T> TryMakeUniqueWithAllocator(IAllocator& Allocator, Arguments&&... Values)
{
    static_assert(std::is_nothrow_constructible<T, Arguments...>::value,
        "TryMakeUnique requires non-throwing object construction");
    static_assert(std::is_nothrow_destructible<T>::value,
        "TryMakeUnique requires non-throwing object destruction");
    T* const Storage = TryAllocateArray<T>(Allocator, 1);
    if (Storage == nullptr)
    {
        return {};
    }
    new (Storage) T(std::forward<Arguments>(Values)...);
    return UniquePtr<T>(Storage, &Allocator, &Detail::DestroyUniqueAllocated<T>);
}

template <typename T, typename... Arguments>
UniquePtr<T> MakeUniqueWithAllocator(IAllocator& Allocator, Arguments&&... Values)
{
    UniquePtr<T> Result = TryMakeUniqueWithAllocator<T>(Allocator, std::forward<Arguments>(Values)...);
    if (!Result)
    {
        HandleOutOfMemory(sizeof(T), alignof(T));
    }
    return Result;
}

template <typename T, typename... Arguments>
UniquePtr<T> MakeUnique(Arguments&&... Values)
{
    return MakeUniqueWithAllocator<T>(GetDefaultAllocator(), std::forward<Arguments>(Values)...);
}

template <typename T, typename... Arguments>
UniquePtr<T> TryMakeUnique(Arguments&&... Values)
{
    return TryMakeUniqueWithAllocator<T>(GetDefaultAllocator(), std::forward<Arguments>(Values)...);
}

} // namespace LE
