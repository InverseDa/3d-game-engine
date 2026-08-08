#include "Memory/Allocator.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>

#if defined(_MSC_VER)
    #include <malloc.h>
#endif

namespace LE
{
namespace
{

class DefaultAllocator final : public IAllocator
{
public:
    void* Allocate(const std::size_t Size, const std::size_t Alignment) noexcept override
    {
        if (Size == 0)
        {
            return nullptr;
        }
        if (!IsValidAlignment(Alignment))
        {
            return nullptr;
        }

        const std::size_t PlatformAlignment = (std::max)(Alignment, alignof(void*));
#if defined(_MSC_VER)
        return _aligned_malloc(Size, PlatformAlignment);
#else
        void* Address = nullptr;
        return posix_memalign(&Address, PlatformAlignment, Size) == 0 ? Address : nullptr;
#endif
    }

    void Deallocate(
        void* const Address,
        const std::size_t,
        const std::size_t) noexcept override
    {
#if defined(_MSC_VER)
        _aligned_free(Address);
#else
        std::free(Address);
#endif
    }
};

void DefaultOutOfMemoryHandler(const std::size_t Size, const std::size_t Alignment)
{
    std::fprintf(stderr, "Limitless Core out of memory: size=%zu alignment=%zu\n", Size, Alignment);
    std::abort();
}

void DefaultContractViolationHandler(const char* Message, const char* File, const int Line)
{
    std::fprintf(stderr, "Limitless Core contract violation: %s (%s:%d)\n", Message, File, Line);
    std::abort();
}

DefaultAllocator GDefaultAllocator;
std::atomic<OutOfMemoryHandler> GOutOfMemoryHandler{&DefaultOutOfMemoryHandler};
std::atomic<ContractViolationHandler> GContractViolationHandler{&DefaultContractViolationHandler};

} // namespace

IAllocator& GetDefaultAllocator() noexcept
{
    return GDefaultAllocator;
}

OutOfMemoryHandler SetOutOfMemoryHandler(const OutOfMemoryHandler Handler) noexcept
{
    return GOutOfMemoryHandler.exchange(Handler ? Handler : &DefaultOutOfMemoryHandler);
}

ContractViolationHandler SetContractViolationHandler(const ContractViolationHandler Handler) noexcept
{
    return GContractViolationHandler.exchange(Handler ? Handler : &DefaultContractViolationHandler);
}

[[noreturn]] void HandleOutOfMemory(const std::size_t Size, const std::size_t Alignment) noexcept
{
    GOutOfMemoryHandler.load()(Size, Alignment);
    std::abort();
}

[[noreturn]] void HandleContractViolation(
    const char* const Message,
    const char* const File,
    const int Line) noexcept
{
    GContractViolationHandler.load()(Message, File, Line);
    std::abort();
}

} // namespace LE
