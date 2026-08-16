#pragma once

#include "Memory/Allocator.h"
#include "Reflection/ReflectionInfo.h"

namespace LE
{

enum class EReflectionRegisterResult : uint8
{
    Success = 0,
    InvalidDescriptor,
    DuplicateModuleName,
    DuplicateTypeId,
    DuplicateTypeName,
    DuplicatePropertyId,
    DuplicatePropertyName,
    AllocationFailed,
};

enum class EReflectionUnregisterResult : uint8
{
    Success = 0,
    NotFound,
};

// Registry access is intentionally not thread-safe, including lookup cache
// refresh. The owner must externally synchronize all calls. Every pointer and
// view returned by lookup is invalidated by any successful registration,
// unregistration, or registry destruction.
class REFLECTION_API FReflectionRegistry final
{
public:
    FReflectionRegistry() noexcept;
    // The allocator identity must outlive this registry. Allocation and
    // deallocation are both initiated by the Reflection implementation DLL.
    explicit FReflectionRegistry(IAllocator& Allocator) noexcept;
    ~FReflectionRegistry() noexcept;

    FReflectionRegistry(const FReflectionRegistry&) = delete;
    FReflectionRegistry& operator=(const FReflectionRegistry&) = delete;
    FReflectionRegistry(FReflectionRegistry&&) = delete;
    FReflectionRegistry& operator=(FReflectionRegistry&&) = delete;

    EReflectionRegisterResult RegisterModule(
        const FReflectionModuleDescriptor& Descriptor,
        FReflectionModuleHandle& OutHandle) noexcept;
    EReflectionUnregisterResult UnregisterModule(FReflectionModuleHandle Handle) noexcept;

    const FTypeInfo* FindType(FTypeId Id) const noexcept;
    const FTypeInfo* FindType(StringView Name) const noexcept;
    const FEnumInfo* FindEnum(FTypeId Id) const noexcept;
    const FEnumInfo* FindEnum(StringView Name) const noexcept;

    std::size_t GetModuleCount() const noexcept;
    std::size_t GetTypeCount() const noexcept;
    std::size_t GetEnumCount() const noexcept;

private:
    struct FImpl;
    FImpl* Impl = nullptr;
};

} // namespace LE
