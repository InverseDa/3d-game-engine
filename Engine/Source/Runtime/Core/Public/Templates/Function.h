#pragma once

#include "Memory/Allocator.h"

#include <new>
#include <type_traits>
#include <utility>

namespace LE
{

template <typename Signature>
class Function;

template <typename Return, typename... Arguments>
class Function<Return(Arguments...)>
{
public:
    Function() noexcept = default;
    Function(std::nullptr_t) noexcept {}
    explicit Function(IAllocator& Allocator) noexcept : AllocatorValue(Allocator) {}

    template <
        typename Callable,
        typename Stored = typename std::decay<Callable>::type,
        typename std::enable_if<!std::is_same<Stored, Function>::value, int>::type = 0>
    Function(Callable&& Value)
    {
        AssignCallableOrFail(GetDefaultAllocator(), std::forward<Callable>(Value));
    }

    template <
        typename Callable,
        typename Stored = typename std::decay<Callable>::type,
        typename std::enable_if<!std::is_same<Stored, Function>::value, int>::type = 0>
    Function(IAllocator& Allocator, Callable&& Value)
        : AllocatorValue(Allocator)
    {
        AssignCallableOrFail(Allocator, std::forward<Callable>(Value));
    }

    Function(const Function& Other)
        : AllocatorValue(Other.AllocatorValue)
    {
        if (Other.CallableValue != nullptr)
        {
            CallableValue = Other.CopyValue(AllocatorValue.Get(), Other.CallableValue);
            if (CallableValue == nullptr)
            {
                HandleOutOfMemory(Other.SizeValue, Other.AlignmentValue);
            }
            InvokeValue = Other.InvokeValue;
            CopyValue = Other.CopyValue;
            DestroyValue = Other.DestroyValue;
            SizeValue = Other.SizeValue;
            AlignmentValue = Other.AlignmentValue;
        }
    }

    Function(Function&& Other) noexcept
        : CallableValue(Other.CallableValue)
        , InvokeValue(Other.InvokeValue)
        , CopyValue(Other.CopyValue)
        , DestroyValue(Other.DestroyValue)
        , SizeValue(Other.SizeValue)
        , AlignmentValue(Other.AlignmentValue)
        , AllocatorValue(Other.AllocatorValue)
    {
        Other.ClearWithoutDestroy();
    }

    ~Function() { Reset(); }

    Function& operator=(const Function& Other)
    {
        if (this != &Other)
        {
            Function Copy(Other);
            Swap(Copy);
        }
        return *this;
    }

    Function& operator=(Function&& Other) noexcept
    {
        if (this != &Other)
        {
            Function Moved(std::move(Other));
            Swap(Moved);
        }
        return *this;
    }

    Function& operator=(std::nullptr_t) noexcept
    {
        Reset();
        return *this;
    }

    template <
        typename Callable,
        typename Stored = typename std::decay<Callable>::type,
        typename std::enable_if<!std::is_same<Stored, Function>::value, int>::type = 0>
    Function& operator=(Callable&& Value)
    {
        Function Replacement(AllocatorValue.Get(), std::forward<Callable>(Value));
        Swap(Replacement);
        return *this;
    }

    template <
        typename Callable,
        typename Stored = typename std::decay<Callable>::type,
        typename std::enable_if<!std::is_same<Stored, Function>::value, int>::type = 0>
    bool TryAssign(Callable&& Value) noexcept
    {
        Function Replacement(AllocatorValue.Get());
        if (!Replacement.TryAssignCallable(AllocatorValue.Get(), std::forward<Callable>(Value)))
        {
            return false;
        }
        Swap(Replacement);
        return true;
    }

    explicit operator bool() const noexcept { return CallableValue != nullptr; }
    bool IsBound() const noexcept { return CallableValue != nullptr; }
    IAllocator& GetAllocator() const noexcept { return AllocatorValue.Get(); }

    Return operator()(Arguments... Values) const
    {
        if (CallableValue == nullptr)
        {
            HandleContractViolation("invoked empty Function", __FILE__, __LINE__);
        }
        return InvokeValue(CallableValue, std::forward<Arguments>(Values)...);
    }

    void Reset() noexcept
    {
        if (CallableValue != nullptr)
        {
            DestroyValue(AllocatorValue.Get(), CallableValue);
        }
        ClearWithoutDestroy();
    }

    void Swap(Function& Other) noexcept
    {
        using std::swap;
        swap(CallableValue, Other.CallableValue);
        swap(InvokeValue, Other.InvokeValue);
        swap(CopyValue, Other.CopyValue);
        swap(DestroyValue, Other.DestroyValue);
        swap(SizeValue, Other.SizeValue);
        swap(AlignmentValue, Other.AlignmentValue);
        swap(AllocatorValue, Other.AllocatorValue);
    }

private:
    using InvokeFunction = Return (*)(void*, Arguments&&...);
    using CopyFunction = void* (*)(IAllocator&, const void*) noexcept;
    using DestroyFunction = void (*)(IAllocator&, void*) noexcept;

    template <typename Callable>
    static Return InvokeCallable(void* const Storage, Arguments&&... Values)
    {
        return (*static_cast<Callable*>(Storage))(std::forward<Arguments>(Values)...);
    }

    template <typename Callable>
    static void* CopyCallable(IAllocator& Allocator, const void* const Storage) noexcept
    {
        Callable* const Copy = TryAllocateArray<Callable>(Allocator, 1);
        if (Copy == nullptr)
        {
            return nullptr;
        }
        new (Copy) Callable(*static_cast<const Callable*>(Storage));
        return Copy;
    }

    template <typename Callable>
    static void DestroyCallable(IAllocator& Allocator, void* const Storage) noexcept
    {
        Callable* const Value = static_cast<Callable*>(Storage);
        Value->~Callable();
        DeallocateArray(Allocator, Value, 1);
    }

    template <typename Callable>
    bool TryAssignCallable(IAllocator& Allocator, Callable&& Value) noexcept
    {
        using Stored = typename std::decay<Callable>::type;
        static_assert(std::is_copy_constructible<Stored>::value,
            "Function requires a copy-constructible callable");
        static_assert(std::is_nothrow_copy_constructible<Stored>::value,
            "Function requires non-throwing callable copies");
        static_assert(std::is_nothrow_constructible<Stored, Callable&&>::value,
            "Function requires non-throwing callable construction");
        static_assert(std::is_nothrow_destructible<Stored>::value,
            "Function requires a non-throwing callable destructor");
        static_assert(std::is_invocable_r<Return, Stored&, Arguments...>::value,
            "Function callable does not match its signature");

        Stored* const Storage = TryAllocateArray<Stored>(Allocator, 1);
        if (Storage == nullptr)
        {
            return false;
        }
        new (Storage) Stored(std::forward<Callable>(Value));
        CallableValue = Storage;
        InvokeValue = &InvokeCallable<Stored>;
        CopyValue = &CopyCallable<Stored>;
        DestroyValue = &DestroyCallable<Stored>;
        SizeValue = sizeof(Stored);
        AlignmentValue = alignof(Stored);
        return true;
    }

    template <typename Callable>
    void AssignCallableOrFail(IAllocator& Allocator, Callable&& Value)
    {
        using Stored = typename std::decay<Callable>::type;
        if (!TryAssignCallable(Allocator, std::forward<Callable>(Value)))
        {
            HandleOutOfMemory(sizeof(Stored), alignof(Stored));
        }
    }

    void ClearWithoutDestroy() noexcept
    {
        CallableValue = nullptr;
        InvokeValue = nullptr;
        CopyValue = nullptr;
        DestroyValue = nullptr;
        SizeValue = 0;
        AlignmentValue = 0;
    }

    void* CallableValue = nullptr;
    InvokeFunction InvokeValue = nullptr;
    CopyFunction CopyValue = nullptr;
    DestroyFunction DestroyValue = nullptr;
    std::size_t SizeValue = 0;
    std::size_t AlignmentValue = 0;
    AllocatorRef AllocatorValue;
};

} // namespace LE
