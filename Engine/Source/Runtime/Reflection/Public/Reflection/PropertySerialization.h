#pragma once

#include "Containers/Array.h"
#include "Containers/Span.h"
#include "Reflection/PropertyBag.h"
#include "Reflection/ReflectionRegistry.h"

#include <cstddef>

namespace LE
{

struct FPropertySerializationLimits
{
    std::size_t MaxDocumentBytes = 16u * 1024u * 1024u;
    std::size_t MaxValueBytes = 4u * 1024u * 1024u;
    uint32 MaxFields = 4096;
    uint32 MaxDepth = 32;
};

enum class EPropertySerializationResult : uint8
{
    Success = 0,
    InvalidArgument,
    InvalidSchema,
    InvalidPropertyId,
    DuplicatePropertyId,
    TypeMismatch,
    UnresolvedType,
    GetterFailed,
    InvalidUtf8,
    MalformedData,
    UnsupportedFormatVersion,
    UnsupportedDocumentFlags,
    SizeLimitExceeded,
    AllocationFailed,
};

// Both functions are transactional: OutBytes/OutBag are unchanged on any
// failure. The binary representation is canonical and little-endian.
REFLECTION_API EPropertySerializationResult TryEncodePropertyBag(
    const FPropertyBag& Bag,
    Array<uint8>& OutBytes,
    const FPropertySerializationLimits& Limits = {}) noexcept;
REFLECTION_API EPropertySerializationResult TryDecodePropertyBag(
    Span<const uint8> Bytes,
    FPropertyBag& OutBag,
    const FPropertySerializationLimits& Limits = {}) noexcept;

// Unknown property IDs are retained and accepted. Known properties must carry
// the exact reflected ValueTypeId and a compatible value kind.
REFLECTION_API EPropertySerializationResult ValidatePropertyBag(
    const FReflectionRegistry& Registry,
    const FPropertyBag& Bag,
    const FPropertySerializationLimits& Limits = {}) noexcept;

// Captures a native object through generated getter metadata. SchemaVersion is
// the version of the entire captured object graph and is applied recursively.
REFLECTION_API EPropertySerializationResult TryCapturePropertyBag(
    const FReflectionRegistry& Registry,
    FTypeId SchemaTypeId,
    uint32 SchemaVersion,
    const void* Instance,
    FPropertyBag& OutBag,
    const FPropertySerializationLimits& Limits = {}) noexcept;

} // namespace LE
