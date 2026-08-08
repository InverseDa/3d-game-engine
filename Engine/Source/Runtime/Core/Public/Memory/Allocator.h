#pragma once

#include <cstddef>
#include <limits>

namespace LE
{

class CORE_API IAllocator
{
public:
    virtual ~IAllocator() = default;

    // Returns nullptr for zero bytes or allocation failure. The caller must
    // deallocate through this exact allocator instance.
    virtual void* Allocate(std::size_t Size, std::size_t Alignment) noexcept = 0;
    virtual void Deallocate(void* Address, std::size_t Size, std::size_t Alignment) noexcept = 0;
};

using OutOfMemoryHandler = void (*)(std::size_t Size, std::size_t Alignment);
using ContractViolationHandler = void (*)(const char* Message, const char* File, int Line);

CORE_API IAllocator& GetDefaultAllocator() noexcept;
CORE_API OutOfMemoryHandler SetOutOfMemoryHandler(OutOfMemoryHandler Handler) noexcept;
CORE_API ContractViolationHandler SetContractViolationHandler(ContractViolationHandler Handler) noexcept;

[[noreturn]] CORE_API void HandleOutOfMemory(std::size_t Size, std::size_t Alignment) noexcept;
[[noreturn]] CORE_API void HandleContractViolation(
    const char* Message,
    const char* File,
    int Line) noexcept;

constexpr bool IsValidAlignment(const std::size_t Alignment) noexcept
{
    return Alignment != 0 && (Alignment & (Alignment - 1)) == 0;
}

constexpr bool TryMultiplySize(
    const std::size_t Left,
    const std::size_t Right,
    std::size_t& Result) noexcept
{
    if (Left != 0 && Right > (std::numeric_limits<std::size_t>::max)() / Left)
    {
        Result = 0;
        return false;
    }

    Result = Left * Right;
    return true;
}

inline void ValidateAlignment(const std::size_t Alignment)
{
    if (!IsValidAlignment(Alignment))
    {
        HandleContractViolation("alignment must be a non-zero power of two", __FILE__, __LINE__);
    }
}

inline void* TryAllocateBytes(
    IAllocator& Allocator,
    const std::size_t Size,
    const std::size_t Alignment) noexcept
{
    if (!IsValidAlignment(Alignment))
    {
        HandleContractViolation("alignment must be a non-zero power of two", __FILE__, __LINE__);
    }
    return Size == 0 ? nullptr : Allocator.Allocate(Size, Alignment);
}

inline void* AllocateBytes(
    IAllocator& Allocator,
    const std::size_t Size,
    const std::size_t Alignment)
{
    void* const Address = TryAllocateBytes(Allocator, Size, Alignment);
    if (Address == nullptr && Size != 0)
    {
        HandleOutOfMemory(Size, Alignment);
    }
    return Address;
}

template <typename T>
T* TryAllocateArray(IAllocator& Allocator, const std::size_t Count) noexcept
{
    std::size_t Size = 0;
    if (!TryMultiplySize(sizeof(T), Count, Size))
    {
        return nullptr;
    }
    return static_cast<T*>(TryAllocateBytes(Allocator, Size, alignof(T)));
}

template <typename T>
T* AllocateArray(IAllocator& Allocator, const std::size_t Count)
{
    std::size_t Size = 0;
    if (!TryMultiplySize(sizeof(T), Count, Size))
    {
        HandleOutOfMemory((std::numeric_limits<std::size_t>::max)(), alignof(T));
    }
    return static_cast<T*>(AllocateBytes(Allocator, Size, alignof(T)));
}

template <typename T>
void DeallocateArray(IAllocator& Allocator, T* const Address, const std::size_t Count) noexcept
{
    if (Address == nullptr)
    {
        return;
    }

    std::size_t Size = 0;
    if (!TryMultiplySize(sizeof(T), Count, Size))
    {
        HandleContractViolation("deallocation size overflow", __FILE__, __LINE__);
    }
    Allocator.Deallocate(Address, Size, alignof(T));
}

class AllocatorRef
{
public:
    AllocatorRef() noexcept
        : Instance(&GetDefaultAllocator())
    {
    }

    explicit AllocatorRef(IAllocator& Allocator) noexcept
        : Instance(&Allocator)
    {
    }

    IAllocator& Get() const noexcept
    {
        return *Instance;
    }

    friend bool operator==(const AllocatorRef Left, const AllocatorRef Right) noexcept
    {
        return Left.Instance == Right.Instance;
    }

    friend bool operator!=(const AllocatorRef Left, const AllocatorRef Right) noexcept
    {
        return !(Left == Right);
    }

private:
    IAllocator* Instance;
};

} // namespace LE
