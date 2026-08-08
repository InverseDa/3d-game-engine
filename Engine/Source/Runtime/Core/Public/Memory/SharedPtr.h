#pragma once

#include "Memory/Allocator.h"

#include <atomic>
#include <cstddef>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace LE
{

namespace Detail
{
struct SharedControlBlock
{
    std::atomic<std::size_t> StrongCount{1};
    std::atomic<std::size_t> WeakCount{1}; // implicit weak reference while strong > 0
    void* Object = nullptr;
    void (*DestroyObject)(SharedControlBlock*) noexcept = nullptr;
    void (*DestroyControl)(SharedControlBlock*) noexcept = nullptr;
};

inline void AddStrongReference(SharedControlBlock* const Control) noexcept
{
    const std::size_t Previous = Control->StrongCount.fetch_add(1, std::memory_order_relaxed);
    if (Previous == (std::numeric_limits<std::size_t>::max)())
    {
        HandleContractViolation("SharedPtr strong reference count overflow", __FILE__, __LINE__);
    }
}

inline void AddWeakReference(SharedControlBlock* const Control) noexcept
{
    const std::size_t Previous = Control->WeakCount.fetch_add(1, std::memory_order_relaxed);
    if (Previous == (std::numeric_limits<std::size_t>::max)())
    {
        HandleContractViolation("WeakPtr reference count overflow", __FILE__, __LINE__);
    }
}

inline void ReleaseWeakReference(SharedControlBlock* const Control) noexcept
{
    if (Control->WeakCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        Control->DestroyControl(Control);
    }
}

inline void ReleaseStrongReference(SharedControlBlock* const Control) noexcept
{
    if (Control->StrongCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        Control->DestroyObject(Control);
        ReleaseWeakReference(Control);
    }
}

template <typename T>
struct InplaceSharedControl final : SharedControlBlock
{
    template <typename... Arguments>
    explicit InplaceSharedControl(IAllocator& InAllocator, Arguments&&... Values) noexcept
        : Allocator(&InAllocator)
    {
        Object = new (&Storage) T(std::forward<Arguments>(Values)...);
        DestroyObject = &DestroyStoredObject;
        DestroyControl = &DestroyStoredControl;
    }

    static void DestroyStoredObject(SharedControlBlock* const Base) noexcept
    {
        auto* const Control = static_cast<InplaceSharedControl*>(Base);
        static_cast<T*>(Control->Object)->~T();
        Control->Object = nullptr;
    }

    static void DestroyStoredControl(SharedControlBlock* const Base) noexcept
    {
        auto* const Control = static_cast<InplaceSharedControl*>(Base);
        IAllocator& Route = *Control->Allocator;
        Control->~InplaceSharedControl();
        DeallocateArray(Route, Control, 1);
    }

    IAllocator* Allocator;
    typename std::aligned_storage<sizeof(T), alignof(T)>::type Storage;
};

template <typename T, typename Deleter>
struct ExternalSharedControl final : SharedControlBlock
{
    template <typename DeleterArgument>
    ExternalSharedControl(
        IAllocator& InAllocator,
        T* const InObject,
        DeleterArgument&& InDeleter) noexcept
        : Allocator(&InAllocator)
        , Destroyer(std::forward<DeleterArgument>(InDeleter))
    {
        Object = InObject;
        DestroyObject = &DestroyExternalObject;
        DestroyControl = &DestroyExternalControl;
    }

    static void DestroyExternalObject(SharedControlBlock* const Base) noexcept
    {
        auto* const Control = static_cast<ExternalSharedControl*>(Base);
        Control->Destroyer(static_cast<T*>(Control->Object));
        Control->Object = nullptr;
    }

    static void DestroyExternalControl(SharedControlBlock* const Base) noexcept
    {
        auto* const Control = static_cast<ExternalSharedControl*>(Base);
        IAllocator& Route = *Control->Allocator;
        Control->~ExternalSharedControl();
        DeallocateArray(Route, Control, 1);
    }

    IAllocator* Allocator;
    Deleter Destroyer;
};
} // namespace Detail

template <typename T> class WeakPtr;

template <typename T>
class SharedPtr
{
    template <typename> friend class SharedPtr;
    template <typename> friend class WeakPtr;
    template <typename U, typename... Arguments>
    friend SharedPtr<U> TryMakeSharedWithAllocator(IAllocator&, Arguments&&...);
    template <typename U, typename Deleter>
    friend SharedPtr<U> TryAdoptShared(U*, IAllocator&, Deleter&&);

public:
    SharedPtr() noexcept = default;
    SharedPtr(std::nullptr_t) noexcept {}

    SharedPtr(const SharedPtr& Other) noexcept
        : ObjectValue(Other.ObjectValue)
        , ControlValue(Other.ControlValue)
    {
        AddReference();
    }

    template <typename U, typename std::enable_if<std::is_convertible<U*, T*>::value, int>::type = 0>
    SharedPtr(const SharedPtr<U>& Other) noexcept
        : ObjectValue(Other.ObjectValue)
        , ControlValue(Other.ControlValue)
    {
        AddReference();
    }

    SharedPtr(SharedPtr&& Other) noexcept
        : ObjectValue(Other.ObjectValue)
        , ControlValue(Other.ControlValue)
    {
        Other.ObjectValue = nullptr;
        Other.ControlValue = nullptr;
    }

    template <typename U, typename std::enable_if<std::is_convertible<U*, T*>::value, int>::type = 0>
    SharedPtr(SharedPtr<U>&& Other) noexcept
        : ObjectValue(Other.ObjectValue)
        , ControlValue(Other.ControlValue)
    {
        Other.ObjectValue = nullptr;
        Other.ControlValue = nullptr;
    }

    ~SharedPtr() { Reset(); }

    SharedPtr& operator=(const SharedPtr& Other) noexcept
    {
        if (this != &Other)
        {
            SharedPtr Copy(Other);
            Swap(Copy);
        }
        return *this;
    }

    SharedPtr& operator=(SharedPtr&& Other) noexcept
    {
        if (this != &Other)
        {
            SharedPtr Moved(std::move(Other));
            Swap(Moved);
        }
        return *this;
    }

    T* Get() const noexcept { return ObjectValue; }
    explicit operator bool() const noexcept { return ObjectValue != nullptr; }
    T& operator*() const { Validate(); return *ObjectValue; }
    T* operator->() const { Validate(); return ObjectValue; }
    std::size_t UseCount() const noexcept
    {
        return ControlValue == nullptr ? 0 : ControlValue->StrongCount.load(std::memory_order_acquire);
    }

    void Reset() noexcept
    {
        if (ControlValue != nullptr)
        {
            Detail::ReleaseStrongReference(ControlValue);
        }
        ObjectValue = nullptr;
        ControlValue = nullptr;
    }

    void Swap(SharedPtr& Other) noexcept
    {
        using std::swap;
        swap(ObjectValue, Other.ObjectValue);
        swap(ControlValue, Other.ControlValue);
    }

private:
    SharedPtr(T* const Object, Detail::SharedControlBlock* const Control, const bool bAddReference) noexcept
        : ObjectValue(Object)
        , ControlValue(Control)
    {
        if (bAddReference)
        {
            AddReference();
        }
    }

    void AddReference() noexcept
    {
        if (ControlValue != nullptr)
        {
            Detail::AddStrongReference(ControlValue);
        }
    }

    void Validate() const
    {
        if (ObjectValue == nullptr)
        {
            HandleContractViolation("dereferenced empty SharedPtr", __FILE__, __LINE__);
        }
    }

    T* ObjectValue = nullptr;
    Detail::SharedControlBlock* ControlValue = nullptr;
};

template <typename T>
class WeakPtr
{
    template <typename> friend class WeakPtr;

public:
    WeakPtr() noexcept = default;

    template <typename U, typename std::enable_if<std::is_convertible<U*, T*>::value, int>::type = 0>
    WeakPtr(const SharedPtr<U>& Owner) noexcept
        : ObjectValue(Owner.ObjectValue)
        , ControlValue(Owner.ControlValue)
    {
        AddReference();
    }

    WeakPtr(const WeakPtr& Other) noexcept
        : ObjectValue(Other.ObjectValue)
        , ControlValue(Other.ControlValue)
    {
        AddReference();
    }

    template <typename U, typename std::enable_if<std::is_convertible<U*, T*>::value, int>::type = 0>
    WeakPtr(const WeakPtr<U>& Other) noexcept
        : ObjectValue(Other.ObjectValue)
        , ControlValue(Other.ControlValue)
    {
        AddReference();
    }

    WeakPtr(WeakPtr&& Other) noexcept
        : ObjectValue(Other.ObjectValue)
        , ControlValue(Other.ControlValue)
    {
        Other.ObjectValue = nullptr;
        Other.ControlValue = nullptr;
    }

    ~WeakPtr() { Reset(); }

    WeakPtr& operator=(const WeakPtr& Other) noexcept
    {
        if (this != &Other)
        {
            WeakPtr Copy(Other);
            Swap(Copy);
        }
        return *this;
    }

    WeakPtr& operator=(WeakPtr&& Other) noexcept
    {
        if (this != &Other)
        {
            WeakPtr Moved(std::move(Other));
            Swap(Moved);
        }
        return *this;
    }

    bool IsExpired() const noexcept
    {
        return ControlValue == nullptr
            || ControlValue->StrongCount.load(std::memory_order_acquire) == 0;
    }

    SharedPtr<T> Lock() const noexcept
    {
        if (ControlValue == nullptr)
        {
            return {};
        }
        std::size_t Count = ControlValue->StrongCount.load(std::memory_order_acquire);
        while (Count != 0)
        {
            if (Count == (std::numeric_limits<std::size_t>::max)())
            {
                HandleContractViolation("SharedPtr strong reference count overflow", __FILE__, __LINE__);
            }
            if (ControlValue->StrongCount.compare_exchange_weak(
                    Count, Count + 1, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                return SharedPtr<T>(ObjectValue, ControlValue, false);
            }
        }
        return {};
    }

    void Reset() noexcept
    {
        if (ControlValue != nullptr)
        {
            Detail::ReleaseWeakReference(ControlValue);
        }
        ObjectValue = nullptr;
        ControlValue = nullptr;
    }

    void Swap(WeakPtr& Other) noexcept
    {
        using std::swap;
        swap(ObjectValue, Other.ObjectValue);
        swap(ControlValue, Other.ControlValue);
    }

private:
    void AddReference() noexcept
    {
        if (ControlValue != nullptr)
        {
            Detail::AddWeakReference(ControlValue);
        }
    }

    T* ObjectValue = nullptr;
    Detail::SharedControlBlock* ControlValue = nullptr;
};

template <typename T, typename... Arguments>
SharedPtr<T> TryMakeSharedWithAllocator(IAllocator& Allocator, Arguments&&... Values)
{
    static_assert(std::is_nothrow_constructible<T, Arguments...>::value,
        "TryMakeShared requires non-throwing object construction");
    static_assert(std::is_nothrow_destructible<T>::value,
        "TryMakeShared requires non-throwing object destruction");
    using Control = Detail::InplaceSharedControl<T>;
    Control* const Storage = TryAllocateArray<Control>(Allocator, 1);
    if (Storage == nullptr)
    {
        return {};
    }
    Control* const Created = new (Storage) Control(Allocator, std::forward<Arguments>(Values)...);
    return SharedPtr<T>(static_cast<T*>(Created->Object), Created, false);
}

template <typename T, typename... Arguments>
SharedPtr<T> MakeSharedWithAllocator(IAllocator& Allocator, Arguments&&... Values)
{
    SharedPtr<T> Result = TryMakeSharedWithAllocator<T>(Allocator, std::forward<Arguments>(Values)...);
    if (!Result)
    {
        HandleOutOfMemory(sizeof(Detail::InplaceSharedControl<T>), alignof(Detail::InplaceSharedControl<T>));
    }
    return Result;
}

template <typename T, typename... Arguments>
SharedPtr<T> MakeShared(Arguments&&... Values)
{
    return MakeSharedWithAllocator<T>(GetDefaultAllocator(), std::forward<Arguments>(Values)...);
}

template <typename T, typename... Arguments>
SharedPtr<T> TryMakeShared(Arguments&&... Values)
{
    return TryMakeSharedWithAllocator<T>(GetDefaultAllocator(), std::forward<Arguments>(Values)...);
}

template <typename T, typename Deleter>
SharedPtr<T> TryAdoptShared(T* const Object, IAllocator& Allocator, Deleter&& Destroyer)
{
    if (Object == nullptr)
    {
        return {};
    }
    using StoredDeleter = typename std::decay<Deleter>::type;
    static_assert(std::is_nothrow_constructible<StoredDeleter, Deleter&&>::value,
        "TryAdoptShared requires non-throwing deleter construction");
    static_assert(std::is_nothrow_invocable<StoredDeleter&, T*>::value,
        "TryAdoptShared requires a non-throwing deleter invocation");
    static_assert(std::is_nothrow_destructible<StoredDeleter>::value,
        "TryAdoptShared requires a non-throwing deleter destructor");
    using Control = Detail::ExternalSharedControl<T, StoredDeleter>;
    Control* const Storage = TryAllocateArray<Control>(Allocator, 1);
    if (Storage == nullptr)
    {
        return {};
    }
    Control* const Created = new (Storage) Control(
        Allocator, Object, std::forward<Deleter>(Destroyer));
    return SharedPtr<T>(Object, Created, false);
}

template <typename T, typename Deleter>
SharedPtr<T> AdoptShared(T* const Object, IAllocator& Allocator, Deleter&& Destroyer)
{
    SharedPtr<T> Result = TryAdoptShared(Object, Allocator, std::forward<Deleter>(Destroyer));
    if (Object != nullptr && !Result)
    {
        using Control = Detail::ExternalSharedControl<T, typename std::decay<Deleter>::type>;
        HandleOutOfMemory(sizeof(Control), alignof(Control));
    }
    return Result;
}

} // namespace LE
