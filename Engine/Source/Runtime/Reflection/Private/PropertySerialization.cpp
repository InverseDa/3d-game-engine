#include "Reflection/PropertySerialization.h"

#include <cstring>
#include <cstdint>
#include <limits>
#include <utility>

namespace LE
{
namespace
{

constexpr uint8 Magic[] = { 'L', 'P', 'B', 'G' };
constexpr std::uint16_t FormatVersion = 1;
constexpr std::size_t HeaderSize = 32;
constexpr std::size_t FieldHeaderSize = 40;

static_assert(sizeof(float32) == 4 && sizeof(float64) == 8,
    "property wire format requires 32-bit and 64-bit floating storage");
static_assert(std::numeric_limits<float32>::is_iec559 &&
    std::numeric_limits<float64>::is_iec559,
    "property wire format requires IEC 60559 floating representation");

bool AreLimitsValid(const FPropertySerializationLimits& Limits) noexcept
{
    return Limits.MaxDocumentBytes >= HeaderSize && Limits.MaxValueBytes != 0 &&
        Limits.MaxFields != 0 && Limits.MaxDepth != 0;
}

bool IsValidUtf8(const StringView Text) noexcept
{
    const uint8* const Data = reinterpret_cast<const uint8*>(Text.Data());
    std::size_t Index = 0;
    while (Index < Text.Size())
    {
        const uint8 First = Data[Index++];
        if (First <= 0x7fu) continue;
        uint32 CodePoint = 0;
        std::size_t Continuations = 0;
        uint32 Minimum = 0;
        if ((First & 0xe0u) == 0xc0u)
        {
            CodePoint = First & 0x1fu; Continuations = 1; Minimum = 0x80u;
        }
        else if ((First & 0xf0u) == 0xe0u)
        {
            CodePoint = First & 0x0fu; Continuations = 2; Minimum = 0x800u;
        }
        else if ((First & 0xf8u) == 0xf0u)
        {
            CodePoint = First & 0x07u; Continuations = 3; Minimum = 0x10000u;
        }
        else return false;
        if (Continuations > Text.Size() - Index) return false;
        for (std::size_t Count = 0; Count < Continuations; ++Count)
        {
            const uint8 Next = Data[Index++];
            if ((Next & 0xc0u) != 0x80u) return false;
            CodePoint = (CodePoint << 6) | (Next & 0x3fu);
        }
        if (CodePoint < Minimum || CodePoint > 0x10ffffu ||
            (CodePoint >= 0xd800u && CodePoint <= 0xdfffu)) return false;
    }
    return true;
}

bool TryAppend(Array<uint8>& Output, const Span<const uint8> Bytes) noexcept
{
    if (Bytes.Size() > Array<uint8>::MaxSize() - Output.Size() ||
        !Output.TryReserve(Output.Size() + Bytes.Size()))
    {
        return false;
    }
    for (const uint8 Byte : Bytes)
    {
        if (!Output.TryPushBack(Byte)) return false;
    }
    return true;
}

bool TryWriteU8(Array<uint8>& Output, const uint8 Value) noexcept
{
    return Output.TryPushBack(Value);
}

bool TryWriteU16(Array<uint8>& Output, const std::uint16_t Value) noexcept
{
    const uint8 Bytes[] = {
        static_cast<uint8>(Value), static_cast<uint8>(Value >> 8),
    };
    return TryAppend(Output, Span<const uint8>(Bytes));
}

bool TryWriteU32(Array<uint8>& Output, const uint32 Value) noexcept
{
    const uint8 Bytes[] = {
        static_cast<uint8>(Value), static_cast<uint8>(Value >> 8),
        static_cast<uint8>(Value >> 16), static_cast<uint8>(Value >> 24),
    };
    return TryAppend(Output, Span<const uint8>(Bytes));
}

bool TryWriteU64(Array<uint8>& Output, const uint64 Value) noexcept
{
    uint8 Bytes[8];
    for (uint8 Index = 0; Index < 8; ++Index)
    {
        Bytes[Index] = static_cast<uint8>(Value >> (Index * 8));
    }
    return TryAppend(Output, Span<const uint8>(Bytes));
}

template <typename StableId>
bool TryWriteStableId(Array<uint8>& Output, const StableId Id) noexcept
{
    if (!Id.IsValid()) return false;
    return TryAppend(Output, Id.GetUuid().GetBytes());
}

class FReader
{
public:
    explicit FReader(const Span<const uint8> InBytes) noexcept : Bytes(InBytes) {}

    bool TryReadU8(uint8& Out) noexcept
    {
        if (Remaining() < 1) return false;
        Out = Bytes[Position++];
        return true;
    }
    bool TryReadU16(std::uint16_t& Out) noexcept
    {
        if (Remaining() < 2) return false;
        Out = static_cast<std::uint16_t>(Bytes[Position]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(Bytes[Position + 1]) << 8);
        Position += 2;
        return true;
    }
    bool TryReadU32(uint32& Out) noexcept
    {
        if (Remaining() < 4) return false;
        Out = static_cast<uint32>(Bytes[Position]) |
            (static_cast<uint32>(Bytes[Position + 1]) << 8) |
            (static_cast<uint32>(Bytes[Position + 2]) << 16) |
            (static_cast<uint32>(Bytes[Position + 3]) << 24);
        Position += 4;
        return true;
    }
    bool TryReadU64(uint64& Out) noexcept
    {
        if (Remaining() < 8) return false;
        Out = 0;
        for (uint8 Index = 0; Index < 8; ++Index)
        {
            Out |= static_cast<uint64>(Bytes[Position + Index]) << (Index * 8);
        }
        Position += 8;
        return true;
    }
    bool TryReadBytes(const std::size_t Count, Span<const uint8>& Out) noexcept
    {
        if (Count > Remaining()) return false;
        Out = Span<const uint8>(Bytes.Data() == nullptr ? nullptr : Bytes.Data() + Position, Count);
        Position += Count;
        return true;
    }
    std::size_t Remaining() const noexcept { return Bytes.Size() - Position; }

private:
    Span<const uint8> Bytes;
    std::size_t Position = 0;
};

char HexDigit(const uint8 Value) noexcept
{
    return static_cast<char>(Value < 10 ? '0' + Value : 'a' + Value - 10);
}

template <typename StableId>
bool TryReadStableId(FReader& Reader, StableId& Out) noexcept
{
    Span<const uint8> Bytes;
    if (!Reader.TryReadBytes(FUuid::ByteCount, Bytes)) return false;
    char Text[FUuid::CanonicalStringLength];
    std::size_t ByteIndex = 0;
    std::size_t TextIndex = 0;
    for (; ByteIndex < FUuid::ByteCount; ++ByteIndex)
    {
        if (TextIndex == 8 || TextIndex == 13 || TextIndex == 18 || TextIndex == 23)
        {
            Text[TextIndex++] = '-';
        }
        Text[TextIndex++] = HexDigit(static_cast<uint8>(Bytes[ByteIndex] >> 4));
        Text[TextIndex++] = HexDigit(static_cast<uint8>(Bytes[ByteIndex] & 0x0fu));
    }
    StableId Candidate;
    if (!StableId::TryParse(StringView(Text, sizeof(Text)), Candidate)) return false;
    Out = Candidate;
    return true;
}

EPropertyValueKind KindForBuiltin(const EPropertyBuiltinType Type) noexcept
{
    switch (Type)
    {
    case EPropertyBuiltinType::Bool: return EPropertyValueKind::Bool;
    case EPropertyBuiltinType::Int8: return EPropertyValueKind::Int8;
    case EPropertyBuiltinType::UInt8: return EPropertyValueKind::UInt8;
    case EPropertyBuiltinType::Int32: return EPropertyValueKind::Int32;
    case EPropertyBuiltinType::UInt32: return EPropertyValueKind::UInt32;
    case EPropertyBuiltinType::Int64: return EPropertyValueKind::Int64;
    case EPropertyBuiltinType::UInt64: return EPropertyValueKind::UInt64;
    case EPropertyBuiltinType::Float32: return EPropertyValueKind::Float32;
    case EPropertyBuiltinType::Float64: return EPropertyValueKind::Float64;
    case EPropertyBuiltinType::String: return EPropertyValueKind::String;
    case EPropertyBuiltinType::Bytes: return EPropertyValueKind::Bytes;
    }
    return EPropertyValueKind::Invalid;
}

EPropertySerializationResult MeasureDocument(
    const FPropertyBag& Bag,
    const FPropertySerializationLimits& Limits,
    const uint32 Depth,
    std::size_t& OutSize) noexcept
{
    if (!Bag.IsValid()) return EPropertySerializationResult::InvalidSchema;
    if (Depth >= Limits.MaxDepth || Bag.GetFieldCount() > Limits.MaxFields)
        return EPropertySerializationResult::SizeLimitExceeded;
    std::size_t Total = HeaderSize;
    for (std::size_t Index = 0; Index < Bag.GetFieldCount(); ++Index)
    {
        const FPropertyId PropertyId = Bag.GetPropertyIdAt(Index);
        const FPropertyValue* const Value = Bag.GetValueAt(Index);
        if (!PropertyId.IsValid()) return EPropertySerializationResult::InvalidPropertyId;
        if (Value == nullptr || !Value->IsValid()) return EPropertySerializationResult::InvalidArgument;
        std::size_t PayloadSize = 0;
        switch (Value->GetKind())
        {
        case EPropertyValueKind::Bool:
        case EPropertyValueKind::Int8:
        case EPropertyValueKind::UInt8: PayloadSize = 1; break;
        case EPropertyValueKind::Int32:
        case EPropertyValueKind::UInt32:
        case EPropertyValueKind::Float32: PayloadSize = 4; break;
        case EPropertyValueKind::Int64:
        case EPropertyValueKind::UInt64:
        case EPropertyValueKind::Float64:
        case EPropertyValueKind::Enum: PayloadSize = 8; break;
        case EPropertyValueKind::String:
        {
            StringView Text;
            if (!Value->TryGetString(Text)) return EPropertySerializationResult::InvalidArgument;
            if (!IsValidUtf8(Text)) return EPropertySerializationResult::InvalidUtf8;
            PayloadSize = Text.Size();
            break;
        }
        case EPropertyValueKind::Bytes:
        {
            Span<const uint8> Bytes;
            if (!Value->TryGetBytes(Bytes)) return EPropertySerializationResult::InvalidArgument;
            PayloadSize = Bytes.Size();
            break;
        }
        case EPropertyValueKind::Opaque:
        {
            uint8 Tag = 0;
            uint8 Flags = 0;
            Span<const uint8> Payload;
            if (!Value->TryGetOpaque(Tag, Flags, Payload))
                return EPropertySerializationResult::InvalidArgument;
            PayloadSize = Payload.Size();
            break;
        }
        case EPropertyValueKind::Object:
        {
            const FPropertyBag* const Object = Value->TryGetObject();
            if (Object == nullptr) return EPropertySerializationResult::InvalidArgument;
            const EPropertySerializationResult Result =
                MeasureDocument(*Object, Limits, Depth + 1, PayloadSize);
            if (Result != EPropertySerializationResult::Success) return Result;
            break;
        }
        case EPropertyValueKind::Invalid:
            return EPropertySerializationResult::InvalidArgument;
        }
        if (PayloadSize > Limits.MaxValueBytes || Total > Limits.MaxDocumentBytes ||
            FieldHeaderSize > Limits.MaxDocumentBytes - Total ||
            PayloadSize > Limits.MaxDocumentBytes - Total - FieldHeaderSize)
            return EPropertySerializationResult::SizeLimitExceeded;
        Total += FieldHeaderSize + PayloadSize;
    }
    OutSize = Total;
    return EPropertySerializationResult::Success;
}

EPropertySerializationResult TryEncodeDocument(
    const FPropertyBag& Bag,
    Array<uint8>& Output,
    const FPropertySerializationLimits& Limits,
    const uint32 Depth) noexcept;

EPropertySerializationResult TryEncodeValue(
    const FPropertyValue& Value,
    uint8& OutTag,
    uint8& OutFlags,
    Array<uint8>& Payload,
    const FPropertySerializationLimits& Limits,
    const uint32 Depth) noexcept
{
    OutFlags = 0;
    switch (Value.GetKind())
    {
    case EPropertyValueKind::Bool:
    {
        bool Item = false; Value.TryGetBool(Item); OutTag = static_cast<uint8>(EPropertyWireTag::Bool);
        if (!TryWriteU8(Payload, Item ? 1 : 0)) return EPropertySerializationResult::AllocationFailed;
        break;
    }
    case EPropertyValueKind::Int8:
    {
        int8 Item = 0; uint8 Bits = 0; Value.TryGetInt8(Item); std::memcpy(&Bits, &Item, sizeof(Bits));
        OutTag = static_cast<uint8>(EPropertyWireTag::Int8);
        if (!TryWriteU8(Payload, Bits)) return EPropertySerializationResult::AllocationFailed;
        break;
    }
    case EPropertyValueKind::UInt8:
    {
        uint8 Item = 0; Value.TryGetUInt8(Item); OutTag = static_cast<uint8>(EPropertyWireTag::UInt8);
        if (!TryWriteU8(Payload, Item)) return EPropertySerializationResult::AllocationFailed;
        break;
    }
    case EPropertyValueKind::Int32:
    {
        int32 Item = 0; Value.TryGetInt32(Item); uint32 Bits = 0; std::memcpy(&Bits, &Item, sizeof(Bits));
        OutTag = static_cast<uint8>(EPropertyWireTag::Int32);
        if (!TryWriteU32(Payload, Bits)) return EPropertySerializationResult::AllocationFailed;
        break;
    }
    case EPropertyValueKind::UInt32:
    {
        uint32 Item = 0; Value.TryGetUInt32(Item); OutTag = static_cast<uint8>(EPropertyWireTag::UInt32);
        if (!TryWriteU32(Payload, Item)) return EPropertySerializationResult::AllocationFailed;
        break;
    }
    case EPropertyValueKind::Int64:
    {
        int64 Item = 0; Value.TryGetInt64(Item); uint64 Bits = 0; std::memcpy(&Bits, &Item, sizeof(Bits));
        OutTag = static_cast<uint8>(EPropertyWireTag::Int64);
        if (!TryWriteU64(Payload, Bits)) return EPropertySerializationResult::AllocationFailed;
        break;
    }
    case EPropertyValueKind::UInt64:
    {
        uint64 Item = 0; Value.TryGetUInt64(Item); OutTag = static_cast<uint8>(EPropertyWireTag::UInt64);
        if (!TryWriteU64(Payload, Item)) return EPropertySerializationResult::AllocationFailed;
        break;
    }
    case EPropertyValueKind::Float32:
    {
        float32 Item = 0; Value.TryGetFloat32(Item); uint32 Bits = 0; std::memcpy(&Bits, &Item, sizeof(Bits));
        OutTag = static_cast<uint8>(EPropertyWireTag::Float32);
        if (!TryWriteU32(Payload, Bits)) return EPropertySerializationResult::AllocationFailed;
        break;
    }
    case EPropertyValueKind::Float64:
    {
        float64 Item = 0; Value.TryGetFloat64(Item); uint64 Bits = 0; std::memcpy(&Bits, &Item, sizeof(Bits));
        OutTag = static_cast<uint8>(EPropertyWireTag::Float64);
        if (!TryWriteU64(Payload, Bits)) return EPropertySerializationResult::AllocationFailed;
        break;
    }
    case EPropertyValueKind::String:
    {
        StringView Item; Value.TryGetString(Item); OutTag = static_cast<uint8>(EPropertyWireTag::String);
        if (!IsValidUtf8(Item)) return EPropertySerializationResult::InvalidUtf8;
        if (Item.Size() > Limits.MaxValueBytes) return EPropertySerializationResult::SizeLimitExceeded;
        if (!TryAppend(Payload, Span<const uint8>(reinterpret_cast<const uint8*>(Item.Data()), Item.Size())))
            return EPropertySerializationResult::AllocationFailed;
        break;
    }
    case EPropertyValueKind::Bytes:
    {
        Span<const uint8> Item; Value.TryGetBytes(Item); OutTag = static_cast<uint8>(EPropertyWireTag::Bytes);
        if (Item.Size() > Limits.MaxValueBytes) return EPropertySerializationResult::SizeLimitExceeded;
        if (!TryAppend(Payload, Item)) return EPropertySerializationResult::AllocationFailed;
        break;
    }
    case EPropertyValueKind::Enum:
    {
        uint64 Bits = 0; Value.TryGetEnum(Bits); OutTag = static_cast<uint8>(EPropertyWireTag::Enum);
        if (!TryWriteU64(Payload, Bits)) return EPropertySerializationResult::AllocationFailed;
        break;
    }
    case EPropertyValueKind::Object:
    {
        const FPropertyBag* const Object = Value.TryGetObject();
        if (Object == nullptr || Depth >= Limits.MaxDepth) return EPropertySerializationResult::SizeLimitExceeded;
        if (Limits.MaxValueBytes < HeaderSize) return EPropertySerializationResult::SizeLimitExceeded;
        OutTag = static_cast<uint8>(EPropertyWireTag::Object);
        FPropertySerializationLimits NestedLimits = Limits;
        if (NestedLimits.MaxDocumentBytes > NestedLimits.MaxValueBytes)
            NestedLimits.MaxDocumentBytes = NestedLimits.MaxValueBytes;
        const EPropertySerializationResult Result =
            TryEncodeDocument(*Object, Payload, NestedLimits, Depth + 1);
        if (Result != EPropertySerializationResult::Success) return Result;
        break;
    }
    case EPropertyValueKind::Opaque:
    {
        Span<const uint8> Item;
        if (!Value.TryGetOpaque(OutTag, OutFlags, Item)) return EPropertySerializationResult::InvalidArgument;
        if (Item.Size() > Limits.MaxValueBytes) return EPropertySerializationResult::SizeLimitExceeded;
        if (!TryAppend(Payload, Item)) return EPropertySerializationResult::AllocationFailed;
        break;
    }
    case EPropertyValueKind::Invalid:
        return EPropertySerializationResult::InvalidArgument;
    }
    return Payload.Size() > Limits.MaxValueBytes
        ? EPropertySerializationResult::SizeLimitExceeded
        : EPropertySerializationResult::Success;
}

EPropertySerializationResult TryEncodeDocument(
    const FPropertyBag& Bag,
    Array<uint8>& Output,
    const FPropertySerializationLimits& Limits,
    const uint32 Depth) noexcept
{
    if (!Bag.IsValid()) return EPropertySerializationResult::InvalidSchema;
    if (Depth >= Limits.MaxDepth || Bag.GetFieldCount() > Limits.MaxFields ||
        Bag.GetFieldCount() > (std::numeric_limits<uint32>::max)())
        return EPropertySerializationResult::SizeLimitExceeded;

    if (!TryAppend(Output, Span<const uint8>(Magic)) ||
        !TryWriteU16(Output, FormatVersion) || !TryWriteU16(Output, 0) ||
        !TryWriteStableId(Output, Bag.GetSchemaTypeId()) ||
        !TryWriteU32(Output, Bag.GetSchemaVersion()) ||
        !TryWriteU32(Output, static_cast<uint32>(Bag.GetFieldCount())))
        return EPropertySerializationResult::AllocationFailed;

    for (std::size_t Index = 0; Index < Bag.GetFieldCount(); ++Index)
    {
        const FPropertyId PropertyId = Bag.GetPropertyIdAt(Index);
        const FPropertyValue* const Value = Bag.GetValueAt(Index);
        if (!PropertyId.IsValid()) return EPropertySerializationResult::InvalidPropertyId;
        if (Value == nullptr || !Value->IsValid()) return EPropertySerializationResult::InvalidArgument;
        Array<uint8> Payload(Output.GetAllocator());
        uint8 Tag = 0;
        uint8 Flags = 0;
        const EPropertySerializationResult ValueResult =
            TryEncodeValue(*Value, Tag, Flags, Payload, Limits, Depth);
        if (ValueResult != EPropertySerializationResult::Success) return ValueResult;
        if (Payload.Size() > (std::numeric_limits<uint32>::max)())
            return EPropertySerializationResult::SizeLimitExceeded;
        if (Output.Size() > Limits.MaxDocumentBytes ||
            FieldHeaderSize > Limits.MaxDocumentBytes - Output.Size() ||
            Payload.Size() > Limits.MaxDocumentBytes - Output.Size() - FieldHeaderSize)
            return EPropertySerializationResult::SizeLimitExceeded;
        if (!TryWriteStableId(Output, PropertyId) || !TryWriteStableId(Output, Value->GetTypeId()) ||
            !TryWriteU8(Output, Tag) || !TryWriteU8(Output, Flags) || !TryWriteU16(Output, 0) ||
            !TryWriteU32(Output, static_cast<uint32>(Payload.Size())) ||
            !TryAppend(Output, Span<const uint8>(Payload.Data(), Payload.Size())))
            return EPropertySerializationResult::AllocationFailed;
        if (Output.Size() > Limits.MaxDocumentBytes)
            return EPropertySerializationResult::SizeLimitExceeded;
    }
    return Output.Size() <= Limits.MaxDocumentBytes
        ? EPropertySerializationResult::Success
        : EPropertySerializationResult::SizeLimitExceeded;
}

bool HasExactType(const FPropertyValue& Value, const EPropertyBuiltinType Type) noexcept
{
    return Value.GetTypeId() == GetBuiltinPropertyTypeId(Type) &&
        Value.GetKind() == KindForBuiltin(Type);
}

EPropertySerializationResult TryDecodeDocument(
    const Span<const uint8> Bytes,
    FPropertyBag& OutBag,
    const FPropertySerializationLimits& Limits,
    const uint32 Depth) noexcept
{
    if (Depth >= Limits.MaxDepth || Bytes.Size() > Limits.MaxDocumentBytes)
        return EPropertySerializationResult::SizeLimitExceeded;
    if (Bytes.Size() < HeaderSize) return EPropertySerializationResult::MalformedData;
    FReader Reader(Bytes);
    Span<const uint8> ReadMagic;
    if (!Reader.TryReadBytes(sizeof(Magic), ReadMagic) ||
        std::memcmp(ReadMagic.Data(), Magic, sizeof(Magic)) != 0)
        return EPropertySerializationResult::MalformedData;
    std::uint16_t Version = 0;
    std::uint16_t DocumentFlags = 0;
    if (!Reader.TryReadU16(Version) || !Reader.TryReadU16(DocumentFlags))
        return EPropertySerializationResult::MalformedData;
    if (Version != FormatVersion) return EPropertySerializationResult::UnsupportedFormatVersion;
    if (DocumentFlags != 0) return EPropertySerializationResult::UnsupportedDocumentFlags;
    FTypeId SchemaTypeId;
    uint32 SchemaVersion = 0;
    uint32 FieldCount = 0;
    if (!TryReadStableId(Reader, SchemaTypeId) || !Reader.TryReadU32(SchemaVersion) ||
        !Reader.TryReadU32(FieldCount) || SchemaVersion == 0)
        return EPropertySerializationResult::InvalidSchema;
    if (FieldCount > Limits.MaxFields) return EPropertySerializationResult::SizeLimitExceeded;

    FPropertyBag Candidate(OutBag.GetAllocator());
    if (!Candidate.TryInitialize(SchemaTypeId, SchemaVersion))
        return EPropertySerializationResult::AllocationFailed;
    for (uint32 Index = 0; Index < FieldCount; ++Index)
    {
        FPropertyId PropertyId;
        FTypeId ValueTypeId;
        uint8 Tag = 0;
        uint8 Flags = 0;
        std::uint16_t Reserved = 0;
        uint32 PayloadSize = 0;
        if (Reader.Remaining() < FUuid::ByteCount)
            return EPropertySerializationResult::MalformedData;
        if (!TryReadStableId(Reader, PropertyId))
            return EPropertySerializationResult::InvalidPropertyId;
        if (Reader.Remaining() < FUuid::ByteCount)
            return EPropertySerializationResult::MalformedData;
        if (!TryReadStableId(Reader, ValueTypeId))
            return EPropertySerializationResult::TypeMismatch;
        if (!Reader.TryReadU8(Tag) || !Reader.TryReadU8(Flags) ||
            !Reader.TryReadU16(Reserved) || !Reader.TryReadU32(PayloadSize))
            return EPropertySerializationResult::MalformedData;
        if (Reserved != 0) return EPropertySerializationResult::MalformedData;
        if (PayloadSize > Limits.MaxValueBytes) return EPropertySerializationResult::SizeLimitExceeded;
        Span<const uint8> Payload;
        if (!Reader.TryReadBytes(PayloadSize, Payload)) return EPropertySerializationResult::MalformedData;

        FPropertyValue Value(Candidate.GetAllocator());
        if (Tag == static_cast<uint8>(EPropertyWireTag::Invalid))
            return EPropertySerializationResult::MalformedData;
        if (Flags != 0 || Tag < static_cast<uint8>(EPropertyWireTag::Bool) ||
            Tag > static_cast<uint8>(EPropertyWireTag::Object))
        {
            if (!Value.TrySetOpaque(ValueTypeId, Tag, Flags, Payload))
                return EPropertySerializationResult::AllocationFailed;
        }
        else
        {
            FReader PayloadReader(Payload);
            bool bSet = false;
            switch (static_cast<EPropertyWireTag>(Tag))
            {
            case EPropertyWireTag::Bool:
            {
                uint8 Item = 0;
                if (Payload.Size() != 1 || !PayloadReader.TryReadU8(Item) || Item > 1)
                    return EPropertySerializationResult::MalformedData;
                if (ValueTypeId != GetBuiltinPropertyTypeId(EPropertyBuiltinType::Bool))
                    return EPropertySerializationResult::TypeMismatch;
                bSet = Value.TrySetBool(Item != 0); break;
            }
            case EPropertyWireTag::Int8:
            {
                uint8 Item = 0;
                if (Payload.Size() != 1 || !PayloadReader.TryReadU8(Item))
                    return EPropertySerializationResult::MalformedData;
                if (ValueTypeId != GetBuiltinPropertyTypeId(EPropertyBuiltinType::Int8))
                    return EPropertySerializationResult::TypeMismatch;
                int8 SignedItem = 0; std::memcpy(&SignedItem, &Item, sizeof(SignedItem));
                bSet = Value.TrySetInt8(SignedItem); break;
            }
            case EPropertyWireTag::UInt8:
            {
                uint8 Item = 0;
                if (Payload.Size() != 1 || !PayloadReader.TryReadU8(Item))
                    return EPropertySerializationResult::MalformedData;
                if (ValueTypeId != GetBuiltinPropertyTypeId(EPropertyBuiltinType::UInt8))
                    return EPropertySerializationResult::TypeMismatch;
                bSet = Value.TrySetUInt8(Item); break;
            }
            case EPropertyWireTag::Int32:
            {
                uint32 Bits = 0; int32 Item = 0;
                if (Payload.Size() != 4 || !PayloadReader.TryReadU32(Bits))
                    return EPropertySerializationResult::MalformedData;
                if (ValueTypeId != GetBuiltinPropertyTypeId(EPropertyBuiltinType::Int32))
                    return EPropertySerializationResult::TypeMismatch;
                std::memcpy(&Item, &Bits, sizeof(Item)); bSet = Value.TrySetInt32(Item); break;
            }
            case EPropertyWireTag::UInt32:
            {
                uint32 Item = 0;
                if (Payload.Size() != 4 || !PayloadReader.TryReadU32(Item))
                    return EPropertySerializationResult::MalformedData;
                if (ValueTypeId != GetBuiltinPropertyTypeId(EPropertyBuiltinType::UInt32))
                    return EPropertySerializationResult::TypeMismatch;
                bSet = Value.TrySetUInt32(Item); break;
            }
            case EPropertyWireTag::Int64:
            {
                uint64 Bits = 0; int64 Item = 0;
                if (Payload.Size() != 8 || !PayloadReader.TryReadU64(Bits))
                    return EPropertySerializationResult::MalformedData;
                if (ValueTypeId != GetBuiltinPropertyTypeId(EPropertyBuiltinType::Int64))
                    return EPropertySerializationResult::TypeMismatch;
                std::memcpy(&Item, &Bits, sizeof(Item)); bSet = Value.TrySetInt64(Item); break;
            }
            case EPropertyWireTag::UInt64:
            {
                uint64 Item = 0;
                if (Payload.Size() != 8 || !PayloadReader.TryReadU64(Item))
                    return EPropertySerializationResult::MalformedData;
                if (ValueTypeId != GetBuiltinPropertyTypeId(EPropertyBuiltinType::UInt64))
                    return EPropertySerializationResult::TypeMismatch;
                bSet = Value.TrySetUInt64(Item); break;
            }
            case EPropertyWireTag::Float32:
            {
                uint32 Bits = 0; float32 Item = 0;
                if (Payload.Size() != 4 || !PayloadReader.TryReadU32(Bits))
                    return EPropertySerializationResult::MalformedData;
                if (ValueTypeId != GetBuiltinPropertyTypeId(EPropertyBuiltinType::Float32))
                    return EPropertySerializationResult::TypeMismatch;
                std::memcpy(&Item, &Bits, sizeof(Item)); bSet = Value.TrySetFloat32(Item); break;
            }
            case EPropertyWireTag::Float64:
            {
                uint64 Bits = 0; float64 Item = 0;
                if (Payload.Size() != 8 || !PayloadReader.TryReadU64(Bits))
                    return EPropertySerializationResult::MalformedData;
                if (ValueTypeId != GetBuiltinPropertyTypeId(EPropertyBuiltinType::Float64))
                    return EPropertySerializationResult::TypeMismatch;
                std::memcpy(&Item, &Bits, sizeof(Item)); bSet = Value.TrySetFloat64(Item); break;
            }
            case EPropertyWireTag::String:
            {
                if (ValueTypeId != GetBuiltinPropertyTypeId(EPropertyBuiltinType::String))
                    return EPropertySerializationResult::TypeMismatch;
                const StringView Item(reinterpret_cast<const char*>(Payload.Data()), Payload.Size());
                if (!IsValidUtf8(Item)) return EPropertySerializationResult::InvalidUtf8;
                bSet = Value.TrySetString(Item); break;
            }
            case EPropertyWireTag::Bytes:
                if (ValueTypeId != GetBuiltinPropertyTypeId(EPropertyBuiltinType::Bytes))
                    return EPropertySerializationResult::TypeMismatch;
                bSet = Value.TrySetBytes(Payload); break;
            case EPropertyWireTag::Enum:
            {
                uint64 Bits = 0;
                if (Payload.Size() != 8 || !PayloadReader.TryReadU64(Bits))
                    return EPropertySerializationResult::MalformedData;
                bSet = Value.TrySetEnum(ValueTypeId, Bits); break;
            }
            case EPropertyWireTag::Object:
            {
                FPropertyBag Object(Candidate.GetAllocator());
                const EPropertySerializationResult Result =
                    TryDecodeDocument(Payload, Object, Limits, Depth + 1);
                if (Result != EPropertySerializationResult::Success) return Result;
                if (Object.GetSchemaTypeId() != ValueTypeId)
                    return EPropertySerializationResult::TypeMismatch;
                bSet = Value.TrySetObject(std::move(Object)); break;
            }
            case EPropertyWireTag::Invalid: return EPropertySerializationResult::MalformedData;
            }
            if (!bSet) return EPropertySerializationResult::AllocationFailed;
        }
        const EPropertyBagMutationResult InsertResult =
            Candidate.TryInsert(PropertyId, std::move(Value));
        if (InsertResult == EPropertyBagMutationResult::DuplicatePropertyId)
            return EPropertySerializationResult::DuplicatePropertyId;
        if (InsertResult != EPropertyBagMutationResult::Success)
            return EPropertySerializationResult::AllocationFailed;
    }
    if (Reader.Remaining() != 0) return EPropertySerializationResult::MalformedData;
    OutBag = std::move(Candidate);
    return EPropertySerializationResult::Success;
}

EPropertySerializationResult ValidateDocument(
    const FReflectionRegistry& Registry,
    const FPropertyBag& Bag,
    const FPropertySerializationLimits& Limits,
    const uint32 Depth,
    const bool bRequireSchema) noexcept
{
    if (!Bag.IsValid()) return EPropertySerializationResult::InvalidSchema;
    if (Depth >= Limits.MaxDepth || Bag.GetFieldCount() > Limits.MaxFields)
        return EPropertySerializationResult::SizeLimitExceeded;
    const FTypeInfo* const Type = Registry.FindType(Bag.GetSchemaTypeId());
    if (Type == nullptr && bRequireSchema) return EPropertySerializationResult::UnresolvedType;
    for (std::size_t Index = 0; Index < Bag.GetFieldCount(); ++Index)
    {
        const FPropertyId PropertyId = Bag.GetPropertyIdAt(Index);
        const FPropertyValue* const Value = Bag.GetValueAt(Index);
        if (!PropertyId.IsValid()) return EPropertySerializationResult::InvalidPropertyId;
        if (Value == nullptr || !Value->IsValid()) return EPropertySerializationResult::InvalidArgument;
        const FPropertyInfo* const Property = Type == nullptr ? nullptr : Type->FindProperty(PropertyId);
        if (Property != nullptr)
        {
            if (Property->ValueTypeId != Value->GetTypeId())
                return EPropertySerializationResult::TypeMismatch;
            EPropertyBuiltinType Builtin;
            if (TryGetBuiltinPropertyType(Property->ValueTypeId, Builtin))
            {
                if (!HasExactType(*Value, Builtin)) return EPropertySerializationResult::TypeMismatch;
            }
            else if (const FEnumInfo* const Enum = Registry.FindEnum(Property->ValueTypeId))
            {
                uint64 Bits = 0;
                if (!Value->TryGetEnum(Bits) || !IsCanonicalEnumValueBits(Enum->UnderlyingType, Bits))
                    return EPropertySerializationResult::TypeMismatch;
            }
            else if (Registry.FindType(Property->ValueTypeId) != nullptr)
            {
                const FPropertyBag* const Object = Value->TryGetObject();
                if (Object == nullptr || Object->GetSchemaTypeId() != Property->ValueTypeId)
                    return EPropertySerializationResult::TypeMismatch;
                const EPropertySerializationResult Result =
                    ValidateDocument(Registry, *Object, Limits, Depth + 1, true);
                if (Result != EPropertySerializationResult::Success) return Result;
            }
            else return EPropertySerializationResult::UnresolvedType;
        }
        else if (const FPropertyBag* const Object = Value->TryGetObject())
        {
            // Unknown fields are preserved. Recurse only to enforce graph
            // limits; their schema need not be loaded.
            const EPropertySerializationResult Result =
                ValidateDocument(Registry, *Object, Limits, Depth + 1, false);
            if (Result != EPropertySerializationResult::Success) return Result;
        }
    }
    return EPropertySerializationResult::Success;
}

EPropertySerializationResult CaptureDocument(
    const FReflectionRegistry& Registry,
    const FTypeInfo& Type,
    const uint32 SchemaVersion,
    const void* Instance,
    FPropertyBag& OutBag,
    const FPropertySerializationLimits& Limits,
    const uint32 Depth) noexcept
{
    if (Instance == nullptr) return EPropertySerializationResult::InvalidArgument;
    if (Depth >= Limits.MaxDepth || Type.Properties.Size() > Limits.MaxFields)
        return EPropertySerializationResult::SizeLimitExceeded;
    FPropertyBag Candidate(OutBag.GetAllocator());
    if (!Candidate.TryInitialize(Type.Id, SchemaVersion))
        return EPropertySerializationResult::AllocationFailed;
    for (const FPropertyInfo& Property : Type.Properties)
    {
        const void* const Address = Property.Getter(Instance);
        if (Address == nullptr) return EPropertySerializationResult::GetterFailed;
        FPropertyValue Value(Candidate.GetAllocator());
        bool bSet = false;
        EPropertyBuiltinType Builtin;
        if (TryGetBuiltinPropertyType(Property.ValueTypeId, Builtin))
        {
            switch (Builtin)
            {
            case EPropertyBuiltinType::Bool: bSet = Value.TrySetBool(*static_cast<const bool*>(Address)); break;
            case EPropertyBuiltinType::Int8: bSet = Value.TrySetInt8(*static_cast<const int8*>(Address)); break;
            case EPropertyBuiltinType::UInt8: bSet = Value.TrySetUInt8(*static_cast<const uint8*>(Address)); break;
            case EPropertyBuiltinType::Int32: bSet = Value.TrySetInt32(*static_cast<const int32*>(Address)); break;
            case EPropertyBuiltinType::UInt32: bSet = Value.TrySetUInt32(*static_cast<const uint32*>(Address)); break;
            case EPropertyBuiltinType::Int64: bSet = Value.TrySetInt64(*static_cast<const int64*>(Address)); break;
            case EPropertyBuiltinType::UInt64: bSet = Value.TrySetUInt64(*static_cast<const uint64*>(Address)); break;
            case EPropertyBuiltinType::Float32: bSet = Value.TrySetFloat32(*static_cast<const float32*>(Address)); break;
            case EPropertyBuiltinType::Float64: bSet = Value.TrySetFloat64(*static_cast<const float64*>(Address)); break;
            case EPropertyBuiltinType::String:
            {
                const StringView Text = static_cast<const String*>(Address)->View();
                if (!IsValidUtf8(Text)) return EPropertySerializationResult::InvalidUtf8;
                bSet = Value.TrySetString(Text); break;
            }
            case EPropertyBuiltinType::Bytes:
                // Bytes has no C++ property spelling in Task 14.
                return EPropertySerializationResult::TypeMismatch;
            }
        }
        else if (const FEnumInfo* const Enum = Registry.FindEnum(Property.ValueTypeId))
        {
            uint64 Bits = 0;
            const uint8 Width = GetEnumUnderlyingBitWidth(Enum->UnderlyingType);
            if (Width == 8) { uint8 Raw = 0; std::memcpy(&Raw, Address, 1); Bits = Raw; }
            else if (Width == 16) { std::uint16_t Raw = 0; std::memcpy(&Raw, Address, 2); Bits = Raw; }
            else if (Width == 32) { uint32 Raw = 0; std::memcpy(&Raw, Address, 4); Bits = Raw; }
            else if (Width == 64) { std::memcpy(&Bits, Address, 8); }
            else return EPropertySerializationResult::TypeMismatch;
            if (!IsCanonicalEnumValueBits(Enum->UnderlyingType, Bits))
                return EPropertySerializationResult::TypeMismatch;
            bSet = Value.TrySetEnum(Property.ValueTypeId, Bits);
        }
        else if (const FTypeInfo* const NestedType = Registry.FindType(Property.ValueTypeId))
        {
            FPropertyBag Nested(Candidate.GetAllocator());
            const EPropertySerializationResult Result = CaptureDocument(
                Registry, *NestedType, SchemaVersion, Address, Nested, Limits, Depth + 1);
            if (Result != EPropertySerializationResult::Success) return Result;
            bSet = Value.TrySetObject(std::move(Nested));
        }
        else return EPropertySerializationResult::UnresolvedType;
        if (!bSet) return EPropertySerializationResult::AllocationFailed;
        const EPropertyBagMutationResult InsertResult =
            Candidate.TryInsert(Property.Id, std::move(Value));
        if (InsertResult == EPropertyBagMutationResult::DuplicatePropertyId)
            return EPropertySerializationResult::DuplicatePropertyId;
        if (InsertResult != EPropertyBagMutationResult::Success)
            return EPropertySerializationResult::AllocationFailed;
    }
    OutBag = std::move(Candidate);
    return EPropertySerializationResult::Success;
}

} // namespace

EPropertySerializationResult TryEncodePropertyBag(
    const FPropertyBag& Bag,
    Array<uint8>& OutBytes,
    const FPropertySerializationLimits& Limits) noexcept
{
    if (!AreLimitsValid(Limits)) return EPropertySerializationResult::InvalidArgument;
    std::size_t MeasuredSize = 0;
    const EPropertySerializationResult MeasureResult =
        MeasureDocument(Bag, Limits, 0, MeasuredSize);
    if (MeasureResult != EPropertySerializationResult::Success) return MeasureResult;
    Array<uint8> Candidate(OutBytes.GetAllocator());
    if (!Candidate.TryReserve(MeasuredSize)) return EPropertySerializationResult::AllocationFailed;
    const EPropertySerializationResult Result = TryEncodeDocument(Bag, Candidate, Limits, 0);
    if (Result == EPropertySerializationResult::Success) OutBytes = std::move(Candidate);
    return Result;
}

EPropertySerializationResult TryDecodePropertyBag(
    const Span<const uint8> Bytes,
    FPropertyBag& OutBag,
    const FPropertySerializationLimits& Limits) noexcept
{
    if (!AreLimitsValid(Limits)) return EPropertySerializationResult::InvalidArgument;
    FPropertyBag Candidate(OutBag.GetAllocator());
    const EPropertySerializationResult Result = TryDecodeDocument(Bytes, Candidate, Limits, 0);
    if (Result == EPropertySerializationResult::Success) OutBag = std::move(Candidate);
    return Result;
}

EPropertySerializationResult ValidatePropertyBag(
    const FReflectionRegistry& Registry,
    const FPropertyBag& Bag,
    const FPropertySerializationLimits& Limits) noexcept
{
    if (!AreLimitsValid(Limits)) return EPropertySerializationResult::InvalidArgument;
    std::size_t MeasuredSize = 0;
    const EPropertySerializationResult MeasureResult =
        MeasureDocument(Bag, Limits, 0, MeasuredSize);
    if (MeasureResult != EPropertySerializationResult::Success) return MeasureResult;
    return ValidateDocument(Registry, Bag, Limits, 0, true);
}

EPropertySerializationResult TryCapturePropertyBag(
    const FReflectionRegistry& Registry,
    const FTypeId SchemaTypeId,
    const uint32 SchemaVersion,
    const void* Instance,
    FPropertyBag& OutBag,
    const FPropertySerializationLimits& Limits) noexcept
{
    if (!AreLimitsValid(Limits) || !SchemaTypeId.IsValid() || SchemaVersion == 0 || Instance == nullptr)
        return EPropertySerializationResult::InvalidArgument;
    const FTypeInfo* const Type = Registry.FindType(SchemaTypeId);
    if (Type == nullptr) return EPropertySerializationResult::UnresolvedType;
    FPropertyBag Candidate(OutBag.GetAllocator());
    const EPropertySerializationResult Result =
        CaptureDocument(Registry, *Type, SchemaVersion, Instance, Candidate, Limits, 0);
    if (Result != EPropertySerializationResult::Success) return Result;
    std::size_t MeasuredSize = 0;
    const EPropertySerializationResult MeasureResult =
        MeasureDocument(Candidate, Limits, 0, MeasuredSize);
    if (MeasureResult != EPropertySerializationResult::Success) return MeasureResult;
    OutBag = std::move(Candidate);
    return EPropertySerializationResult::Success;
}

} // namespace LE
