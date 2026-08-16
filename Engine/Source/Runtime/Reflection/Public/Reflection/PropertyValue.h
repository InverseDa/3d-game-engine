#pragma once

#include "Containers/Span.h"
#include "Containers/StringView.h"
#include "Memory/Allocator.h"
#include "Reflection/ReflectionTypes.h"

namespace LE
{

class FPropertyBag;

// This is the complete canonical property builtin set. Bytes is manual-bag
// only; enums use their reflected type ID and are not represented here.
enum class EPropertyBuiltinType : uint8
{
    Bool = 0,
    Int8,
    UInt8,
    Int32,
    UInt32,
    Int64,
    UInt64,
    Float32,
    Float64,
    String,
    Bytes,
};

enum class EPropertyValueKind : uint8
{
    Invalid = 0,
    Bool,
    Int8,
    UInt8,
    Int32,
    UInt32,
    Int64,
    UInt64,
    Float32,
    Float64,
    String,
    Bytes,
    Enum,
    Object,
    Opaque,
};

enum class EPropertyWireTag : uint8
{
    Invalid = 0,
    Bool = 1,
    Int8 = 2,
    UInt8 = 3,
    Int32 = 4,
    UInt32 = 5,
    Int64 = 6,
    UInt64 = 7,
    Float32 = 8,
    Float64 = 9,
    String = 10,
    Bytes = 11,
    Enum = 12,
    Object = 13,
};

// UUIDs returned here are the single C++ source of truth used by generated
// descriptors, property values, validation, and serialization.
REFLECTION_API FTypeId GetBuiltinPropertyTypeId(EPropertyBuiltinType Type) noexcept;
REFLECTION_API bool TryGetBuiltinPropertyType(
    FTypeId TypeId,
    EPropertyBuiltinType& OutType) noexcept;

class REFLECTION_API FPropertyValue final
{
public:
    FPropertyValue() noexcept;
    explicit FPropertyValue(IAllocator& Allocator) noexcept;
    ~FPropertyValue() noexcept;

    FPropertyValue(const FPropertyValue&) = delete;
    FPropertyValue& operator=(const FPropertyValue&) = delete;
    FPropertyValue(FPropertyValue&& Other) noexcept;
    FPropertyValue& operator=(FPropertyValue&& Other) noexcept;

    bool IsValid() const noexcept;
    EPropertyValueKind GetKind() const noexcept;
    FTypeId GetTypeId() const noexcept;
    IAllocator& GetAllocator() const noexcept;
    void Reset() noexcept;

    bool TrySetBool(bool Value) noexcept;
    bool TrySetInt8(int8 Value) noexcept;
    bool TrySetUInt8(uint8 Value) noexcept;
    bool TrySetInt32(int32 Value) noexcept;
    bool TrySetUInt32(uint32 Value) noexcept;
    bool TrySetInt64(int64 Value) noexcept;
    bool TrySetUInt64(uint64 Value) noexcept;
    bool TrySetFloat32(float32 Value) noexcept;
    bool TrySetFloat64(float64 Value) noexcept;
    bool TrySetString(StringView Value) noexcept;
    bool TrySetBytes(Span<const uint8> Value) noexcept;
    bool TrySetEnum(FTypeId EnumTypeId, uint64 ValueBits) noexcept;
    bool TrySetObject(const FPropertyBag& Value) noexcept;
    bool TrySetObject(FPropertyBag&& Value) noexcept;
    bool TrySetOpaque(
        FTypeId TypeId,
        uint8 WireTag,
        uint8 WireFlags,
        Span<const uint8> Payload) noexcept;

    bool TryGetBool(bool& OutValue) const noexcept;
    bool TryGetInt8(int8& OutValue) const noexcept;
    bool TryGetUInt8(uint8& OutValue) const noexcept;
    bool TryGetInt32(int32& OutValue) const noexcept;
    bool TryGetUInt32(uint32& OutValue) const noexcept;
    bool TryGetInt64(int64& OutValue) const noexcept;
    bool TryGetUInt64(uint64& OutValue) const noexcept;
    bool TryGetFloat32(float32& OutValue) const noexcept;
    bool TryGetFloat64(float64& OutValue) const noexcept;
    bool TryGetString(StringView& OutValue) const noexcept;
    bool TryGetBytes(Span<const uint8>& OutValue) const noexcept;
    bool TryGetEnum(uint64& OutValueBits) const noexcept;
    const FPropertyBag* TryGetObject() const noexcept;
    bool TryGetOpaque(
        uint8& OutWireTag,
        uint8& OutWireFlags,
        Span<const uint8>& OutPayload) const noexcept;

    // Returned String/Bytes/Opaque views and Object pointers are borrowed and
    // invalidated by any successful setter, Reset, move, or destruction.

    // Clones into OutValue's allocator and commits only after every allocation
    // succeeds. OutValue is unchanged on failure.
    bool TryClone(FPropertyValue& OutValue) const noexcept;

private:
    struct FImpl;
    FImpl* Impl = nullptr;
    IAllocator* Allocator = nullptr;
};

} // namespace LE
