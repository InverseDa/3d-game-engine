#include "ReflectionGeneratedFixture.h"

#include "Reflection/PropertySerialization.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace LE
{
namespace
{

int Expect(const bool Condition, const char* const Message)
{
    if (!Condition)
    {
        std::printf("FAILED: %s\n", Message);
        return 1;
    }
    return 0;
}

class FToggleAllocator final : public IAllocator
{
public:
    void* Allocate(const std::size_t Size, const std::size_t Alignment) noexcept override
    {
        return bFail ? nullptr : GetDefaultAllocator().Allocate(Size, Alignment);
    }

    void Deallocate(void* Address, const std::size_t Size, const std::size_t Alignment) noexcept override
    {
        GetDefaultAllocator().Deallocate(Address, Size, Alignment);
    }

    bool bFail = false;
};

template <typename IdType>
IdType ParseId(const char* const Text)
{
    IdType Result;
    static_cast<void>(IdType::TryParse(Text, Result));
    return Result;
}

bool ByteArraysEqual(const Array<uint8>& Left, const Array<uint8>& Right)
{
    return Left.Size() == Right.Size() &&
        (Left.IsEmpty() || std::memcmp(Left.Data(), Right.Data(), Left.Size()) == 0);
}

} // namespace

int RunPropertySerializationTests()
{
    int Failures = 0;
    const FTypeId SchemaId = ParseId<FTypeId>("61000000-0000-4000-8000-000000000003");
    const FPropertyId LowId = ParseId<FPropertyId>("10000000-0000-4000-8000-000000000001");
    const FPropertyId HighId = ParseId<FPropertyId>("f0000000-0000-4000-8000-000000000001");

    FPropertyBag Bag;
    Failures += Expect(Bag.TryInitialize(SchemaId, 7), "property bag initializes with schema and version");
    FPropertyValue High;
    FPropertyValue Low;
    static_cast<void>(High.TrySetString("hello"));
    static_cast<void>(Low.TrySetUInt32(1280));
    Failures += Expect(
        Bag.TryInsert(HighId, std::move(High)) == EPropertyBagMutationResult::Success &&
            Bag.TryInsert(LowId, std::move(Low)) == EPropertyBagMutationResult::Success &&
            !High.IsValid() && !Low.IsValid() && Bag.GetPropertyIdAt(0) == LowId &&
            Bag.GetPropertyIdAt(1) == HighId,
        "bag insertion consumes only success and stores canonical UUID order");
    FPropertyValue Duplicate;
    static_cast<void>(Duplicate.TrySetUInt32(1));
    Failures += Expect(
        Bag.TryInsert(LowId, std::move(Duplicate)) == EPropertyBagMutationResult::DuplicatePropertyId &&
            Duplicate.IsValid(),
        "duplicate insertion leaves rvalue unchanged");

    Array<uint8> Encoded;
    Array<uint8> EncodedAgain;
    FPropertyBag Decoded;
    Failures += Expect(
        TryEncodePropertyBag(Bag, Encoded) == EPropertySerializationResult::Success &&
            TryDecodePropertyBag(Span<const uint8>(Encoded.Data(), Encoded.Size()), Decoded) ==
                EPropertySerializationResult::Success &&
            TryEncodePropertyBag(Decoded, EncodedAgain) == EPropertySerializationResult::Success &&
            ByteArraysEqual(Encoded, EncodedAgain) && Decoded.GetSchemaVersion() == 7,
        "binary property bag round-trip is deterministic and preserves schema version");
    const Span<const uint8> SchemaBytes = SchemaId.GetUuid().GetBytes();
    const Span<const uint8> LowPropertyBytes = LowId.GetUuid().GetBytes();
    const FTypeId UInt32TypeId = GetBuiltinPropertyTypeId(EPropertyBuiltinType::UInt32);
    const Span<const uint8> UInt32TypeBytes = UInt32TypeId.GetUuid().GetBytes();
    Failures += Expect(
        Encoded.Size() == 121 && Encoded[0] == 'L' && Encoded[1] == 'P' &&
            Encoded[2] == 'B' && Encoded[3] == 'G' && Encoded[4] == 1 &&
            Encoded[5] == 0 && Encoded[6] == 0 && Encoded[7] == 0 &&
            std::memcmp(Encoded.Data() + 8, SchemaBytes.Data(), SchemaBytes.Size()) == 0 &&
            Encoded[24] == 7 && Encoded[28] == 2 &&
            std::memcmp(Encoded.Data() + 32, LowPropertyBytes.Data(), LowPropertyBytes.Size()) == 0 &&
            std::memcmp(Encoded.Data() + 48, UInt32TypeBytes.Data(), UInt32TypeBytes.Size()) == 0 &&
            Encoded[64] == static_cast<uint8>(EPropertyWireTag::UInt32) &&
            Encoded[65] == 0 && Encoded[66] == 0 && Encoded[67] == 0 &&
            Encoded[68] == 4 && Encoded[72] == 0 && Encoded[73] == 5 &&
            Encoded[74] == 0 && Encoded[75] == 0 &&
            Encoded[108] == static_cast<uint8>(EPropertyWireTag::String) && Encoded[112] == 5,
        "binary v1 golden layout uses stable IDs exact tags sizes and little-endian payloads");

    FPropertyValue Future;
    const uint8 FutureBytes[] = { 9, 8, 7 };
    Failures += Expect(
        Future.TrySetOpaque(GetBuiltinPropertyTypeId(EPropertyBuiltinType::UInt32), 200, 0,
            Span<const uint8>(FutureBytes)) &&
            Decoded.TryInsert(ParseId<FPropertyId>("e0000000-0000-4000-8000-000000000002"),
                std::move(Future)) == EPropertyBagMutationResult::Success,
        "unknown wire tag is representable as opaque");
    FPropertyValue FutureFlags;
    Failures += Expect(
        FutureFlags.TrySetOpaque(GetBuiltinPropertyTypeId(EPropertyBuiltinType::Bool), 1, 1,
            Span<const uint8>(FutureBytes)) &&
            !FutureFlags.TrySetOpaque(GetBuiltinPropertyTypeId(EPropertyBuiltinType::Bool), 1, 0,
                Span<const uint8>(FutureBytes)),
        "known tag with unknown flags is opaque while known tag flags zero is rejected");
    const FPropertyId FutureFlagsId =
        ParseId<FPropertyId>("e0000000-0000-4000-8000-000000000003");
    static_cast<void>(Decoded.TryInsert(FutureFlagsId, std::move(FutureFlags)));
    Array<uint8> UnknownEncoded;
    Array<uint8> UnknownReencoded;
    FPropertyBag UnknownDecoded;
    Failures += Expect(
        TryEncodePropertyBag(Decoded, UnknownEncoded) == EPropertySerializationResult::Success &&
            TryDecodePropertyBag(Span<const uint8>(UnknownEncoded.Data(), UnknownEncoded.Size()),
                UnknownDecoded) == EPropertySerializationResult::Success &&
            UnknownDecoded.Find(FutureFlagsId) != nullptr &&
            UnknownDecoded.Find(FutureFlagsId)->GetKind() == EPropertyValueKind::Opaque &&
            TryEncodePropertyBag(UnknownDecoded, UnknownReencoded) ==
                EPropertySerializationResult::Success &&
            ByteArraysEqual(UnknownEncoded, UnknownReencoded),
        "unknown field payload and future flags survive canonical decode");

    float32 NaN = 0.0f;
    const uint32 NaNBits = 0x7fc12345u;
    std::memcpy(&NaN, &NaNBits, sizeof(NaN));
    FPropertyBag NaNBag;
    static_cast<void>(NaNBag.TryInitialize(SchemaId, 1));
    FPropertyValue NaNValue;
    static_cast<void>(NaNValue.TrySetFloat32(NaN));
    static_cast<void>(NaNBag.TryInsert(LowId, std::move(NaNValue)));
    Array<uint8> NaNFirst;
    Array<uint8> NaNSecond;
    FPropertyBag NaNDecoded;
    static_cast<void>(TryEncodePropertyBag(NaNBag, NaNFirst));
    static_cast<void>(TryDecodePropertyBag(Span<const uint8>(NaNFirst.Data(), NaNFirst.Size()), NaNDecoded));
    static_cast<void>(TryEncodePropertyBag(NaNDecoded, NaNSecond));
    float32 RoundTripNaN = 0.0f;
    uint32 RoundTripBits = 0;
    NaNDecoded.Find(LowId)->TryGetFloat32(RoundTripNaN);
    std::memcpy(&RoundTripBits, &RoundTripNaN, sizeof(RoundTripBits));
    Failures += Expect(
        ByteArraysEqual(NaNFirst, NaNSecond) && RoundTripBits == NaNBits,
        "floating wire encoding preserves deterministic NaN payload bits");

    Array<uint8> Sentinel;
    Sentinel.PushBack(42);
    FPropertySerializationLimits TinyLimits;
    TinyLimits.MaxDocumentBytes = 32;
    Failures += Expect(
        TryEncodePropertyBag(Bag, Sentinel, TinyLimits) ==
            EPropertySerializationResult::SizeLimitExceeded &&
            Sentinel.Size() == 1 && Sentinel[0] == 42,
        "failed encode leaves output bytes unchanged");

    FPropertyBag DecodeSentinel;
    static_cast<void>(DecodeSentinel.TryInitialize(SchemaId, 99));
    Array<uint8> Trailing(Encoded);
    Trailing.PushBack(0);
    Failures += Expect(
        TryDecodePropertyBag(Span<const uint8>(Trailing.Data(), Trailing.Size()), DecodeSentinel) ==
            EPropertySerializationResult::MalformedData &&
            DecodeSentinel.GetSchemaVersion() == 99,
        "trailing data fails transactionally");
    Array<uint8> Reserved(Encoded);
    Reserved[66] = 1;
    Failures += Expect(
        TryDecodePropertyBag(Span<const uint8>(Reserved.Data(), Reserved.Size()), DecodeSentinel) ==
            EPropertySerializationResult::MalformedData &&
            DecodeSentinel.GetSchemaVersion() == 99,
        "nonzero field reserved bits are rejected transactionally");

    Array<uint8> BadMagic(Encoded);
    BadMagic[0] = 'X';
    Array<uint8> BadVersion(Encoded);
    BadVersion[4] = 2;
    Array<uint8> BadDocumentFlags(Encoded);
    BadDocumentFlags[6] = 1;
    Failures += Expect(
        TryDecodePropertyBag(Span<const uint8>(BadMagic.Data(), BadMagic.Size()), DecodeSentinel) ==
                EPropertySerializationResult::MalformedData &&
            TryDecodePropertyBag(Span<const uint8>(BadVersion.Data(), BadVersion.Size()), DecodeSentinel) ==
                EPropertySerializationResult::UnsupportedFormatVersion &&
            TryDecodePropertyBag(
                Span<const uint8>(BadDocumentFlags.Data(), BadDocumentFlags.Size()), DecodeSentinel) ==
                EPropertySerializationResult::UnsupportedDocumentFlags,
        "magic format version and document flags have distinct diagnostics");

    Array<uint8> NilSchema(Encoded);
    for (std::size_t Index = 8; Index < 24; ++Index) NilSchema[Index] = 0;
    Array<uint8> NilProperty(Encoded);
    for (std::size_t Index = 32; Index < 48; ++Index) NilProperty[Index] = 0;
    Array<uint8> NilValueType(Encoded);
    for (std::size_t Index = 48; Index < 64; ++Index) NilValueType[Index] = 0;
    Failures += Expect(
        TryDecodePropertyBag(Span<const uint8>(NilSchema.Data(), NilSchema.Size()), DecodeSentinel) ==
                EPropertySerializationResult::InvalidSchema &&
            TryDecodePropertyBag(Span<const uint8>(NilProperty.Data(), NilProperty.Size()), DecodeSentinel) ==
                EPropertySerializationResult::InvalidPropertyId &&
            TryDecodePropertyBag(Span<const uint8>(NilValueType.Data(), NilValueType.Size()), DecodeSentinel) ==
                EPropertySerializationResult::TypeMismatch,
        "nil schema property and value type IDs are rejected");

    Array<uint8> WrongPayloadSize(Encoded);
    WrongPayloadSize[68] = 3;
    Array<uint8> WrongBuiltinType(Encoded);
    WrongBuiltinType[48] ^= 1;
    Array<uint8> ReservedTag(Encoded);
    ReservedTag[64] = 0;
    Array<uint8> InvalidUtf8Wire(Encoded);
    InvalidUtf8Wire[116] = 0xc0;
    InvalidUtf8Wire[117] = 0x80;
    Failures += Expect(
        TryDecodePropertyBag(
            Span<const uint8>(WrongPayloadSize.Data(), WrongPayloadSize.Size()), DecodeSentinel) ==
                EPropertySerializationResult::MalformedData &&
            TryDecodePropertyBag(
                Span<const uint8>(WrongBuiltinType.Data(), WrongBuiltinType.Size()), DecodeSentinel) ==
                EPropertySerializationResult::TypeMismatch &&
            TryDecodePropertyBag(Span<const uint8>(ReservedTag.Data(), ReservedTag.Size()), DecodeSentinel) ==
                EPropertySerializationResult::MalformedData &&
            TryDecodePropertyBag(
                Span<const uint8>(InvalidUtf8Wire.Data(), InvalidUtf8Wire.Size()), DecodeSentinel) ==
                EPropertySerializationResult::InvalidUtf8,
        "known payload size builtin type ID reserved tag and UTF-8 are checked independently");

    Failures += Expect(
        TryDecodePropertyBag(Span<const uint8>(Encoded.Data(), 10), DecodeSentinel) ==
                EPropertySerializationResult::MalformedData &&
            TryDecodePropertyBag(Span<const uint8>(Encoded.Data(), 50), DecodeSentinel) ==
                EPropertySerializationResult::MalformedData &&
            TryDecodePropertyBag(
                Span<const uint8>(Encoded.Data(), Encoded.Size() - 1), DecodeSentinel) ==
                EPropertySerializationResult::MalformedData,
        "truncated header field header and payload are malformed");

    FPropertySerializationLimits FieldLimit;
    FieldLimit.MaxFields = 1;
    FPropertySerializationLimits ValueLimit;
    ValueLimit.MaxValueBytes = 3;
    Failures += Expect(
        TryEncodePropertyBag(Bag, Sentinel, FieldLimit) == EPropertySerializationResult::SizeLimitExceeded &&
            TryDecodePropertyBag(Span<const uint8>(Encoded.Data(), Encoded.Size()), DecodeSentinel, FieldLimit) ==
                EPropertySerializationResult::SizeLimitExceeded &&
            TryEncodePropertyBag(Bag, Sentinel, ValueLimit) == EPropertySerializationResult::SizeLimitExceeded,
        "field and value byte limits fail before committing outputs");

    FPropertyBag NestedLeaf;
    static_cast<void>(NestedLeaf.TryInitialize(SchemaId, 4));
    FPropertyValue LeafValue;
    static_cast<void>(LeafValue.TrySetUInt32(1));
    static_cast<void>(NestedLeaf.TryInsert(LowId, std::move(LeafValue)));
    FPropertyBag NestedRoot;
    static_cast<void>(NestedRoot.TryInitialize(SchemaId, 3));
    FPropertyValue ObjectValue;
    static_cast<void>(ObjectValue.TrySetObject(std::move(NestedLeaf)));
    static_cast<void>(NestedRoot.TryInsert(HighId, std::move(ObjectValue)));
    Array<uint8> NestedEncoded;
    static_cast<void>(TryEncodePropertyBag(NestedRoot, NestedEncoded));
    FPropertyBag NestedDecoded;
    static_cast<void>(TryDecodePropertyBag(
        Span<const uint8>(NestedEncoded.Data(), NestedEncoded.Size()), NestedDecoded));
    const FPropertyBag* const DecodedLeaf = NestedDecoded.Find(HighId) == nullptr
        ? nullptr : NestedDecoded.Find(HighId)->TryGetObject();
    FPropertySerializationLimits DepthLimit;
    DepthLimit.MaxDepth = 1;
    Failures += Expect(
        DecodedLeaf != nullptr && DecodedLeaf->GetSchemaVersion() == 4 &&
            TryEncodePropertyBag(NestedRoot, Sentinel, DepthLimit) ==
                EPropertySerializationResult::SizeLimitExceeded &&
            TryDecodePropertyBag(
                Span<const uint8>(NestedEncoded.Data(), NestedEncoded.Size()), DecodeSentinel, DepthLimit) ==
                EPropertySerializationResult::SizeLimitExceeded,
        "manual nested version is preserved and object graph enforces depth limits");
    Array<uint8> DuplicateWire(Encoded);
    DuplicateWire[28] = 3;
    const std::size_t FirstRecordSize = 40 + 4;
    for (std::size_t Index = 0; Index < FirstRecordSize; ++Index)
        DuplicateWire.PushBack(Encoded[32 + Index]);
    Failures += Expect(
        TryDecodePropertyBag(Span<const uint8>(DuplicateWire.Data(), DuplicateWire.Size()),
            DecodeSentinel) == EPropertySerializationResult::DuplicatePropertyId &&
            DecodeSentinel.GetSchemaVersion() == 99,
        "duplicate property IDs are rejected transactionally");

    const char InvalidUtf8Bytes[] = { static_cast<char>(0xc0), static_cast<char>(0x80) };
    FPropertyValue Utf8Value;
    static_cast<void>(Utf8Value.TrySetString("preserved"));
    Failures += Expect(
        !Utf8Value.TrySetString(StringView(InvalidUtf8Bytes, sizeof(InvalidUtf8Bytes))) &&
            Utf8Value.GetKind() == EPropertyValueKind::String,
        "invalid UTF-8 is rejected without replacing the value");
    FPropertyValue TagZero;
    Failures += Expect(
        !TagZero.TrySetOpaque(GetBuiltinPropertyTypeId(EPropertyBuiltinType::Bytes), 0, 1,
            Span<const uint8>(FutureBytes)),
        "wire tag zero cannot be constructed as opaque");

    FToggleAllocator BagAllocator;
    FToggleAllocator ExternalAllocator;
    FPropertyBag MixedBag(BagAllocator);
    static_cast<void>(MixedBag.TryInitialize(SchemaId, 1));
    FPropertyValue External(ExternalAllocator);
    static_cast<void>(External.TrySetUInt32(77));
    BagAllocator.bFail = true;
    Failures += Expect(
        MixedBag.TryInsert(LowId, std::move(External)) ==
            EPropertyBagMutationResult::AllocationFailed && External.IsValid() &&
            MixedBag.GetFieldCount() == 0,
        "mixed-allocator insertion allocation failure consumes nothing");
    BagAllocator.bFail = false;
    Failures += Expect(
        MixedBag.TryInsert(LowId, std::move(External)) == EPropertyBagMutationResult::Success &&
            !External.IsValid() && &External.GetAllocator() == &ExternalAllocator &&
            &MixedBag.Find(LowId)->GetAllocator() == &BagAllocator,
        "mixed-allocator insertion normalizes recursively to bag allocator");
    FPropertyValue Replacement(ExternalAllocator);
    static_cast<void>(Replacement.TrySetUInt32(88));
    BagAllocator.bFail = true;
    uint32 Preserved = 0;
    Failures += Expect(
        MixedBag.TrySet(LowId, std::move(Replacement)) ==
            EPropertyBagMutationResult::AllocationFailed && Replacement.IsValid() &&
            MixedBag.Find(LowId)->TryGetUInt32(Preserved) && Preserved == 77,
        "mixed-allocator replacement failure preserves old and incoming values");
    BagAllocator.bFail = false;

    FPropertyBag ExternalNested(ExternalAllocator);
    static_cast<void>(ExternalNested.TryInitialize(SchemaId, 2));
    FPropertyValue ExternalNestedScalar(ExternalAllocator);
    static_cast<void>(ExternalNestedScalar.TrySetUInt32(9));
    static_cast<void>(ExternalNested.TryInsert(LowId, std::move(ExternalNestedScalar)));
    FPropertyValue ExternalObject(ExternalAllocator);
    static_cast<void>(ExternalObject.TrySetObject(std::move(ExternalNested)));
    static_cast<void>(MixedBag.TryInsert(HighId, std::move(ExternalObject)));
    const FPropertyBag* const NormalizedObject = MixedBag.Find(HighId)->TryGetObject();
    Failures += Expect(
        NormalizedObject != nullptr && &NormalizedObject->GetAllocator() == &BagAllocator &&
            NormalizedObject->Find(LowId) != nullptr &&
            &NormalizedObject->Find(LowId)->GetAllocator() == &BagAllocator,
        "mixed-allocator nested objects normalize every recursive owner to the bag allocator");

    FPropertyBag MoveSource(ExternalAllocator);
    static_cast<void>(MoveSource.TryInitialize(SchemaId, 3));
    FPropertyBag MoveDestination(std::move(MoveSource));
    Failures += Expect(
        MoveDestination.IsValid() && !MoveSource.IsValid() &&
            &MoveSource.GetAllocator() == &ExternalAllocator &&
            MoveSource.TryInitialize(SchemaId, 4),
        "moved-from bag stays bound to and reusable with its original allocator");

    FReflectionRegistry Registry;
    FReflectionModuleHandle ModuleHandle;
    const EReflectionRegisterResult RegisterResult =
        RegisterBuilderTestsReflection(Registry, ModuleHandle);
    const FTypeInfo* const OwnerType = Registry.FindType(SchemaId);
    alignas(FGeneratedPrivateOwner) uint8 OwnerStorage[sizeof(FGeneratedPrivateOwner)];
    const bool bConstructed = OwnerType != nullptr && OwnerType->Construct(OwnerStorage);
    FPropertyBag Captured;
    const EPropertySerializationResult CaptureResult = bConstructed
        ? TryCapturePropertyBag(Registry, SchemaId, 7, OwnerStorage, Captured)
        : EPropertySerializationResult::InvalidArgument;
    FPropertyBag MisalignedCapture;
    static_cast<void>(MisalignedCapture.TryInitialize(SchemaId, 93));
    Failures += Expect(
        TryCapturePropertyBag(Registry, SchemaId, 7, OwnerStorage + 1, MisalignedCapture) ==
                EPropertySerializationResult::GetterFailed &&
            MisalignedCapture.GetSchemaVersion() == 93,
        "generated getter rejection leaves capture output unchanged");

    FToggleAllocator TransactionAllocator;
    Array<uint8> FailedEncode(TransactionAllocator);
    FailedEncode.PushBack(91);
    FPropertyBag FailedDecode(TransactionAllocator);
    static_cast<void>(FailedDecode.TryInitialize(SchemaId, 91));
    FPropertyBag FailedCapture(TransactionAllocator);
    static_cast<void>(FailedCapture.TryInitialize(SchemaId, 92));
    TransactionAllocator.bFail = true;
    Failures += Expect(
        TryEncodePropertyBag(Bag, FailedEncode) == EPropertySerializationResult::AllocationFailed &&
            FailedEncode.Size() == 1 && FailedEncode[0] == 91 &&
            TryDecodePropertyBag(Span<const uint8>(Encoded.Data(), Encoded.Size()), FailedDecode) ==
                EPropertySerializationResult::AllocationFailed &&
            FailedDecode.GetSchemaVersion() == 91 &&
            (bConstructed ? TryCapturePropertyBag(
                Registry, SchemaId, 7, OwnerStorage, FailedCapture)
                : EPropertySerializationResult::AllocationFailed) ==
                EPropertySerializationResult::AllocationFailed &&
            FailedCapture.GetSchemaVersion() == 92,
        "allocation failure leaves encode decode and capture outputs unchanged");
    TransactionAllocator.bFail = false;
    FPropertySerializationLimits CaptureValueLimit;
    CaptureValueLimit.MaxValueBytes = 4;
    FPropertySerializationLimits CaptureDocumentLimit;
    CaptureDocumentLimit.MaxDocumentBytes = 32;
    Failures += Expect(
        (bConstructed ? TryCapturePropertyBag(
            Registry, SchemaId, 7, OwnerStorage, FailedCapture, CaptureValueLimit)
            : EPropertySerializationResult::SizeLimitExceeded) ==
                EPropertySerializationResult::SizeLimitExceeded &&
            FailedCapture.GetSchemaVersion() == 92 &&
            (bConstructed ? TryCapturePropertyBag(
                Registry, SchemaId, 7, OwnerStorage, FailedCapture, CaptureDocumentLimit)
                : EPropertySerializationResult::SizeLimitExceeded) ==
                EPropertySerializationResult::SizeLimitExceeded &&
            FailedCapture.GetSchemaVersion() == 92,
        "capture enforces value and total document limits transactionally");

    FPropertyBag OversizedManual;
    static_cast<void>(OversizedManual.TryInitialize(SchemaId, 1));
    FPropertyValue OversizedText;
    static_cast<void>(OversizedText.TrySetString("five!"));
    static_cast<void>(OversizedManual.TryInsert(LowId, std::move(OversizedText)));
    Failures += Expect(
        ValidatePropertyBag(Registry, OversizedManual, CaptureValueLimit) ==
                EPropertySerializationResult::SizeLimitExceeded &&
            ValidatePropertyBag(Registry, OversizedManual, CaptureDocumentLimit) ==
                EPropertySerializationResult::SizeLimitExceeded,
        "validation enforces value and document limits even for unknown manual fields");
    const FPropertyId ValueId = ParseId<FPropertyId>("61000000-0000-4000-8000-000000000004");
    const FPropertyId PayloadId = ParseId<FPropertyId>("61000000-0000-4000-8000-000000000007");
    const FPropertyId ModeId = ParseId<FPropertyId>("61000000-0000-4000-8000-000000000008");
    const FPropertyId TitleId = ParseId<FPropertyId>("61000000-0000-4000-8000-000000000009");
    const FPropertyBag* const Nested =
        Captured.Find(ValueId) == nullptr ? nullptr : Captured.Find(ValueId)->TryGetObject();
    int32 Payload = 0;
    uint64 ModeBits = 0;
    StringView Title;
    Failures += Expect(
        RegisterResult == EReflectionRegisterResult::Success && bConstructed &&
            CaptureResult == EPropertySerializationResult::Success &&
            Captured.GetSchemaVersion() == 7 && Nested != nullptr &&
            Nested->GetSchemaVersion() == 7 && Nested->Find(PayloadId) != nullptr &&
            Nested->Find(PayloadId)->TryGetInt32(Payload) && Payload == 17 &&
            Captured.Find(ModeId) != nullptr && Captured.Find(ModeId)->TryGetEnum(ModeBits) &&
            ModeBits == 0xff && Captured.Find(TitleId) != nullptr &&
            Captured.Find(TitleId)->TryGetString(Title) && Title == StringView("Generated owner") &&
            ValidatePropertyBag(Registry, Captured) == EPropertySerializationResult::Success,
        "generated private getters capture scalar, enum, string, and nested object metadata");

    FPropertyValue WrongType;
    static_cast<void>(WrongType.TrySetUInt32(5));
    static_cast<void>(Captured.TrySet(TitleId, std::move(WrongType)));
    Failures += Expect(
        ValidatePropertyBag(Registry, Captured) == EPropertySerializationResult::TypeMismatch,
        "schema validation rejects a known property type mismatch");

    if (bConstructed) static_cast<void>(OwnerType->Destruct(OwnerStorage));
    Failures += Expect(
        UnregisterBuilderTestsReflection(Registry, ModuleHandle) ==
            EReflectionUnregisterResult::Success,
        "property capture provider unregisters after object destruction");
    return Failures;
}

} // namespace LE
