#pragma once

#include "Memory/Allocator.h"
#include "Reflection/PropertyValue.h"

#include <cstddef>

namespace LE
{

enum class EPropertyBagMutationResult : uint8
{
    Success = 0,
    InvalidBag,
    InvalidPropertyId,
    InvalidValue,
    DuplicatePropertyId,
    NotFound,
    AllocationFailed,
};

class REFLECTION_API FPropertyBag final
{
public:
    FPropertyBag() noexcept;
    explicit FPropertyBag(IAllocator& Allocator) noexcept;
    ~FPropertyBag() noexcept;

    FPropertyBag(const FPropertyBag&) = delete;
    FPropertyBag& operator=(const FPropertyBag&) = delete;
    FPropertyBag(FPropertyBag&& Other) noexcept;
    FPropertyBag& operator=(FPropertyBag&& Other) noexcept;

    bool IsValid() const noexcept;
    FTypeId GetSchemaTypeId() const noexcept;
    uint32 GetSchemaVersion() const noexcept;
    std::size_t GetFieldCount() const noexcept;
    IAllocator& GetAllocator() const noexcept;

    bool TryInitialize(FTypeId SchemaTypeId, uint32 SchemaVersion) noexcept;
    void Reset() noexcept;

    EPropertyBagMutationResult TryInsert(
        FPropertyId PropertyId,
        FPropertyValue&& Value) noexcept;
    EPropertyBagMutationResult TrySet(
        FPropertyId PropertyId,
        FPropertyValue&& Value) noexcept;
    EPropertyBagMutationResult Erase(FPropertyId PropertyId) noexcept;

    const FPropertyValue* Find(FPropertyId PropertyId) const noexcept;
    FPropertyValue* Find(FPropertyId PropertyId) noexcept;
    FPropertyId GetPropertyIdAt(std::size_t Index) const noexcept;
    const FPropertyValue* GetValueAt(std::size_t Index) const noexcept;

    // Find/GetValueAt pointers are borrowed. Conservatively treat every
    // successful mutation, initialization, reset, move, or destruction as
    // invalidating every pointer obtained from this bag.

    // Clones into OutBag's allocator and commits only on success.
    bool TryClone(FPropertyBag& OutBag) const noexcept;

private:
    friend class FPropertyValue;
    struct FImpl;
    FImpl* Impl = nullptr;
    IAllocator* Allocator = nullptr;
};

} // namespace LE
