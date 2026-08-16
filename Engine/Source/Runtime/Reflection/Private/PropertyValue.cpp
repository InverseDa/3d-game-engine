#include "Reflection/PropertyValue.h"

#include "Reflection/PropertyBag.h"

#include <new>
#include <utility>

namespace LE
{
namespace
{

constexpr const char* BuiltinTypeIds[] = {
    "62c925ef-59ef-4b81-ac9a-956d443b91fe", // Bool
    "8f174d4e-59ca-4c58-9749-3ec98fa8c56c", // Int8
    "b7436e66-6482-4e1f-94d9-67b0323c426d", // UInt8
    "6ff6b20a-9345-460c-9bfd-9a4c55ee503a", // Int32
    "93497730-7ea5-412b-aee9-e10ce909a080", // UInt32
    "146ed02f-ea65-4278-a698-61906f630132", // Int64
    "cfe809b0-014d-4485-8eb6-2328ddce9c27", // UInt64
    "7378e461-09b0-4da9-be06-386cb67d70d4", // Float32
    "351e30dd-600b-4201-b1c0-0dd42263ef23", // Float64
    "71219161-14ba-4a9f-a4ed-9fd33a96a1d5", // String
    "68d345ab-1bad-41ab-acc3-add49d1280e9", // Bytes
};

constexpr std::size_t BuiltinTypeCount =
    sizeof(BuiltinTypeIds) / sizeof(BuiltinTypeIds[0]);

template <typename T, typename... Args>
T* TryNew(IAllocator& Allocator, Args&&... Arguments) noexcept
{
    void* const Storage = TryAllocateBytes(Allocator, sizeof(T), alignof(T));
    return Storage == nullptr
        ? nullptr
        : new (Storage) T(std::forward<Args>(Arguments)...);
}

template <typename T>
void Delete(IAllocator& Allocator, T*& Value) noexcept
{
    if (Value == nullptr)
    {
        return;
    }
    Value->~T();
    Allocator.Deallocate(Value, sizeof(T), alignof(T));
    Value = nullptr;
}

bool IsValidUtf8(const StringView Text) noexcept
{
    const uint8* const Data = reinterpret_cast<const uint8*>(Text.Data());
    std::size_t Index = 0;
    while (Index < Text.Size())
    {
        const uint8 First = Data[Index++];
        if (First <= 0x7fu)
        {
            continue;
        }

        uint32 CodePoint = 0;
        std::size_t Continuations = 0;
        uint32 Minimum = 0;
        if ((First & 0xe0u) == 0xc0u)
        {
            CodePoint = First & 0x1fu;
            Continuations = 1;
            Minimum = 0x80u;
        }
        else if ((First & 0xf0u) == 0xe0u)
        {
            CodePoint = First & 0x0fu;
            Continuations = 2;
            Minimum = 0x800u;
        }
        else if ((First & 0xf8u) == 0xf0u)
        {
            CodePoint = First & 0x07u;
            Continuations = 3;
            Minimum = 0x10000u;
        }
        else
        {
            return false;
        }
        if (Continuations > Text.Size() - Index)
        {
            return false;
        }
        for (std::size_t Count = 0; Count < Continuations; ++Count)
        {
            const uint8 Next = Data[Index++];
            if ((Next & 0xc0u) != 0x80u)
            {
                return false;
            }
            CodePoint = (CodePoint << 6) | (Next & 0x3fu);
        }
        if (CodePoint < Minimum || CodePoint > 0x10ffffu ||
            (CodePoint >= 0xd800u && CodePoint <= 0xdfffu))
        {
            return false;
        }
    }
    return true;
}

} // namespace

struct FPropertyValue::FImpl
{
    explicit FImpl(IAllocator& Allocator) noexcept
        : Text(Allocator)
        , Bytes(Allocator)
        , Object(Allocator)
    {
    }

    EPropertyValueKind Kind = EPropertyValueKind::Invalid;
    FTypeId TypeId;
    bool BoolValue = false;
    int8 Int8Value = 0;
    uint8 UInt8Value = 0;
    int32 Int32Value = 0;
    uint32 UInt32Value = 0;
    int64 Int64Value = 0;
    uint64 UInt64Value = 0;
    float32 Float32Value = 0.0f;
    float64 Float64Value = 0.0;
    String Text;
    Array<uint8> Bytes;
    FPropertyBag Object;
    uint8 WireTag = 0;
    uint8 WireFlags = 0;
};

FTypeId GetBuiltinPropertyTypeId(const EPropertyBuiltinType Type) noexcept
{
    const std::size_t Index = static_cast<std::size_t>(Type);
    FTypeId Result;
    if (Index < BuiltinTypeCount)
    {
        static_cast<void>(FTypeId::TryParse(BuiltinTypeIds[Index], Result));
    }
    return Result;
}

bool TryGetBuiltinPropertyType(
    const FTypeId TypeId,
    EPropertyBuiltinType& OutType) noexcept
{
    for (std::size_t Index = 0; Index < BuiltinTypeCount; ++Index)
    {
        if (TypeId == GetBuiltinPropertyTypeId(static_cast<EPropertyBuiltinType>(Index)))
        {
            OutType = static_cast<EPropertyBuiltinType>(Index);
            return true;
        }
    }
    return false;
}

FPropertyValue::FPropertyValue() noexcept
    : Allocator(&GetDefaultAllocator())
{
}

FPropertyValue::FPropertyValue(IAllocator& InAllocator) noexcept
    : Allocator(&InAllocator)
{
}

FPropertyValue::~FPropertyValue() noexcept
{
    Delete(*Allocator, Impl);
}

FPropertyValue::FPropertyValue(FPropertyValue&& Other) noexcept
    : Impl(Other.Impl)
    , Allocator(Other.Allocator)
{
    Other.Impl = nullptr;
}

FPropertyValue& FPropertyValue::operator=(FPropertyValue&& Other) noexcept
{
    if (this != &Other)
    {
        Delete(*Allocator, Impl);
        Impl = Other.Impl;
        Allocator = Other.Allocator;
        Other.Impl = nullptr;
    }
    return *this;
}

bool FPropertyValue::IsValid() const noexcept
{
    return Impl != nullptr && Impl->Kind != EPropertyValueKind::Invalid && Impl->TypeId.IsValid();
}

EPropertyValueKind FPropertyValue::GetKind() const noexcept
{
    return Impl == nullptr ? EPropertyValueKind::Invalid : Impl->Kind;
}

FTypeId FPropertyValue::GetTypeId() const noexcept
{
    return Impl == nullptr ? FTypeId{} : Impl->TypeId;
}

IAllocator& FPropertyValue::GetAllocator() const noexcept
{
    return *Allocator;
}

void FPropertyValue::Reset() noexcept
{
    Delete(*Allocator, Impl);
}

#define LE_DEFINE_PROPERTY_SCALAR(Name, CppType, Field, KindName, BuiltinName) \
    bool FPropertyValue::TrySet##Name(const CppType Value) noexcept \
    { \
        FImpl* Candidate = TryNew<FImpl>(*Allocator, *Allocator); \
        if (Candidate == nullptr) return false; \
        Candidate->Kind = EPropertyValueKind::KindName; \
        Candidate->TypeId = GetBuiltinPropertyTypeId(EPropertyBuiltinType::BuiltinName); \
        Candidate->Field = Value; \
        Delete(*Allocator, Impl); \
        Impl = Candidate; \
        return true; \
    } \
    bool FPropertyValue::TryGet##Name(CppType& OutValue) const noexcept \
    { \
        if (Impl == nullptr || Impl->Kind != EPropertyValueKind::KindName) return false; \
        OutValue = Impl->Field; \
        return true; \
    }

LE_DEFINE_PROPERTY_SCALAR(Bool, bool, BoolValue, Bool, Bool)
LE_DEFINE_PROPERTY_SCALAR(Int8, int8, Int8Value, Int8, Int8)
LE_DEFINE_PROPERTY_SCALAR(UInt8, uint8, UInt8Value, UInt8, UInt8)
LE_DEFINE_PROPERTY_SCALAR(Int32, int32, Int32Value, Int32, Int32)
LE_DEFINE_PROPERTY_SCALAR(UInt32, uint32, UInt32Value, UInt32, UInt32)
LE_DEFINE_PROPERTY_SCALAR(Int64, int64, Int64Value, Int64, Int64)
LE_DEFINE_PROPERTY_SCALAR(UInt64, uint64, UInt64Value, UInt64, UInt64)
LE_DEFINE_PROPERTY_SCALAR(Float32, float32, Float32Value, Float32, Float32)
LE_DEFINE_PROPERTY_SCALAR(Float64, float64, Float64Value, Float64, Float64)

#undef LE_DEFINE_PROPERTY_SCALAR

bool FPropertyValue::TrySetString(const StringView Value) noexcept
{
    if (!IsValidUtf8(Value))
    {
        return false;
    }
    FImpl* Candidate = TryNew<FImpl>(*Allocator, *Allocator);
    if (Candidate == nullptr || !Candidate->Text.TryAssign(Value))
    {
        Delete(*Allocator, Candidate);
        return false;
    }
    Candidate->Kind = EPropertyValueKind::String;
    Candidate->TypeId = GetBuiltinPropertyTypeId(EPropertyBuiltinType::String);
    Delete(*Allocator, Impl);
    Impl = Candidate;
    return true;
}

bool FPropertyValue::TrySetBytes(const Span<const uint8> Value) noexcept
{
    FImpl* Candidate = TryNew<FImpl>(*Allocator, *Allocator);
    if (Candidate == nullptr || !Candidate->Bytes.TryReserve(Value.Size()))
    {
        Delete(*Allocator, Candidate);
        return false;
    }
    for (const uint8 Byte : Value)
    {
        if (!Candidate->Bytes.TryPushBack(Byte))
        {
            Delete(*Allocator, Candidate);
            return false;
        }
    }
    Candidate->Kind = EPropertyValueKind::Bytes;
    Candidate->TypeId = GetBuiltinPropertyTypeId(EPropertyBuiltinType::Bytes);
    Delete(*Allocator, Impl);
    Impl = Candidate;
    return true;
}

bool FPropertyValue::TrySetEnum(const FTypeId EnumTypeId, const uint64 ValueBits) noexcept
{
    if (!EnumTypeId.IsValid())
    {
        return false;
    }
    FImpl* Candidate = TryNew<FImpl>(*Allocator, *Allocator);
    if (Candidate == nullptr)
    {
        return false;
    }
    Candidate->Kind = EPropertyValueKind::Enum;
    Candidate->TypeId = EnumTypeId;
    Candidate->UInt64Value = ValueBits;
    Delete(*Allocator, Impl);
    Impl = Candidate;
    return true;
}

bool FPropertyValue::TrySetObject(const FPropertyBag& Value) noexcept
{
    if (!Value.IsValid())
    {
        return false;
    }
    FImpl* Candidate = TryNew<FImpl>(*Allocator, *Allocator);
    if (Candidate == nullptr || !Value.TryClone(Candidate->Object))
    {
        Delete(*Allocator, Candidate);
        return false;
    }
    Candidate->Kind = EPropertyValueKind::Object;
    Candidate->TypeId = Value.GetSchemaTypeId();
    Delete(*Allocator, Impl);
    Impl = Candidate;
    return true;
}

bool FPropertyValue::TrySetObject(FPropertyBag&& Value) noexcept
{
    if (!Value.IsValid())
    {
        return false;
    }
    FImpl* Candidate = TryNew<FImpl>(*Allocator, *Allocator);
    if (Candidate == nullptr)
    {
        return false;
    }
    Candidate->Kind = EPropertyValueKind::Object;
    Candidate->TypeId = Value.GetSchemaTypeId();
    if (&Value.GetAllocator() == Allocator)
    {
        Candidate->Object = std::move(Value);
    }
    else if (!Value.TryClone(Candidate->Object))
    {
        Delete(*Allocator, Candidate);
        return false;
    }
    Delete(*Allocator, Impl);
    Impl = Candidate;
    Value.Reset();
    return true;
}

bool FPropertyValue::TrySetOpaque(
    const FTypeId TypeId,
    const uint8 WireTag,
    const uint8 WireFlags,
    const Span<const uint8> Payload) noexcept
{
    // Tags 1-13 with flags zero have a defined v1 interpretation and must be
    // represented by their typed setter. Opaque values are reserved for a
    // future tag or a known tag carrying future non-zero flags.
    const uint8 FirstKnownTag = static_cast<uint8>(EPropertyWireTag::Bool);
    const uint8 LastKnownTag = static_cast<uint8>(EPropertyWireTag::Object);
    if (!TypeId.IsValid() || WireTag == static_cast<uint8>(EPropertyWireTag::Invalid) ||
        (WireTag >= FirstKnownTag && WireTag <= LastKnownTag && WireFlags == 0))
    {
        return false;
    }
    FImpl* Candidate = TryNew<FImpl>(*Allocator, *Allocator);
    if (Candidate == nullptr || !Candidate->Bytes.TryReserve(Payload.Size()))
    {
        Delete(*Allocator, Candidate);
        return false;
    }
    for (const uint8 Byte : Payload)
    {
        if (!Candidate->Bytes.TryPushBack(Byte))
        {
            Delete(*Allocator, Candidate);
            return false;
        }
    }
    Candidate->Kind = EPropertyValueKind::Opaque;
    Candidate->TypeId = TypeId;
    Candidate->WireTag = WireTag;
    Candidate->WireFlags = WireFlags;
    Delete(*Allocator, Impl);
    Impl = Candidate;
    return true;
}

bool FPropertyValue::TryGetString(StringView& OutValue) const noexcept
{
    if (Impl == nullptr || Impl->Kind != EPropertyValueKind::String)
    {
        return false;
    }
    OutValue = Impl->Text;
    return true;
}

bool FPropertyValue::TryGetBytes(Span<const uint8>& OutValue) const noexcept
{
    if (Impl == nullptr || Impl->Kind != EPropertyValueKind::Bytes)
    {
        return false;
    }
    OutValue = Span<const uint8>(Impl->Bytes.Data(), Impl->Bytes.Size());
    return true;
}

bool FPropertyValue::TryGetEnum(uint64& OutValueBits) const noexcept
{
    if (Impl == nullptr || Impl->Kind != EPropertyValueKind::Enum)
    {
        return false;
    }
    OutValueBits = Impl->UInt64Value;
    return true;
}

const FPropertyBag* FPropertyValue::TryGetObject() const noexcept
{
    return Impl != nullptr && Impl->Kind == EPropertyValueKind::Object
        ? &Impl->Object
        : nullptr;
}

bool FPropertyValue::TryGetOpaque(
    uint8& OutWireTag,
    uint8& OutWireFlags,
    Span<const uint8>& OutPayload) const noexcept
{
    if (Impl == nullptr || Impl->Kind != EPropertyValueKind::Opaque)
    {
        return false;
    }
    OutWireTag = Impl->WireTag;
    OutWireFlags = Impl->WireFlags;
    OutPayload = Span<const uint8>(Impl->Bytes.Data(), Impl->Bytes.Size());
    return true;
}

bool FPropertyValue::TryClone(FPropertyValue& OutValue) const noexcept
{
    if (!IsValid())
    {
        return false;
    }
    FPropertyValue Candidate(OutValue.GetAllocator());
    bool bSuccess = false;
    switch (Impl->Kind)
    {
    case EPropertyValueKind::Bool: bSuccess = Candidate.TrySetBool(Impl->BoolValue); break;
    case EPropertyValueKind::Int8: bSuccess = Candidate.TrySetInt8(Impl->Int8Value); break;
    case EPropertyValueKind::UInt8: bSuccess = Candidate.TrySetUInt8(Impl->UInt8Value); break;
    case EPropertyValueKind::Int32: bSuccess = Candidate.TrySetInt32(Impl->Int32Value); break;
    case EPropertyValueKind::UInt32: bSuccess = Candidate.TrySetUInt32(Impl->UInt32Value); break;
    case EPropertyValueKind::Int64: bSuccess = Candidate.TrySetInt64(Impl->Int64Value); break;
    case EPropertyValueKind::UInt64: bSuccess = Candidate.TrySetUInt64(Impl->UInt64Value); break;
    case EPropertyValueKind::Float32: bSuccess = Candidate.TrySetFloat32(Impl->Float32Value); break;
    case EPropertyValueKind::Float64: bSuccess = Candidate.TrySetFloat64(Impl->Float64Value); break;
    case EPropertyValueKind::String: bSuccess = Candidate.TrySetString(Impl->Text); break;
    case EPropertyValueKind::Bytes:
        bSuccess = Candidate.TrySetBytes(Span<const uint8>(Impl->Bytes.Data(), Impl->Bytes.Size()));
        break;
    case EPropertyValueKind::Enum: bSuccess = Candidate.TrySetEnum(Impl->TypeId, Impl->UInt64Value); break;
    case EPropertyValueKind::Object: bSuccess = Candidate.TrySetObject(Impl->Object); break;
    case EPropertyValueKind::Opaque:
        bSuccess = Candidate.TrySetOpaque(
            Impl->TypeId, Impl->WireTag, Impl->WireFlags,
            Span<const uint8>(Impl->Bytes.Data(), Impl->Bytes.Size()));
        break;
    case EPropertyValueKind::Invalid: break;
    }
    if (bSuccess)
    {
        OutValue = std::move(Candidate);
    }
    return bSuccess;
}

} // namespace LE
