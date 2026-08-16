#include "Reflection/ReflectionMinimal.h"
#include "ReflectionGeneratedFixture.h"
#include "World/World.h"
#include "World/WorldReflection.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>

namespace
{

int ExpectReflection(const bool bCondition, const char* const Name)
{
    if (bCondition)
    {
        return 0;
    }
    std::fprintf(stderr, "FAILED: %s\n", Name);
    return 1;
}

template <typename StableId>
StableId ParseStableId(const char* const Text)
{
    StableId Result;
    if (!StableId::TryParse(Text, Result))
    {
        std::abort();
    }
    return Result;
}

struct alignas(16) FManualReflectedRecord
{
    FManualReflectedRecord() noexcept
    {
        ++ConstructionCount;
        ++LiveCount;
    }

    ~FManualReflectedRecord() noexcept
    {
        ++DestructionCount;
        --LiveCount;
    }

    int Value = 17;
    LE::uint64 ReadOnlyValue = 41;

    static int ConstructionCount;
    static int DestructionCount;
    static int LiveCount;
};

int FManualReflectedRecord::ConstructionCount = 0;
int FManualReflectedRecord::DestructionCount = 0;
int FManualReflectedRecord::LiveCount = 0;

class FReflectionFailingAllocator final : public LE::IAllocator
{
public:
    void* Allocate(const std::size_t Size, const std::size_t Alignment) noexcept override
    {
        ++AllocationAttempts;
        if (FailureAttempt != 0 && AllocationAttempts == FailureAttempt)
        {
            return nullptr;
        }
        void* const Result = LE::GetDefaultAllocator().Allocate(Size, Alignment);
        if (Result != nullptr)
        {
            ++LiveAllocations;
        }
        return Result;
    }

    void Deallocate(
        void* const Address,
        const std::size_t Size,
        const std::size_t Alignment) noexcept override
    {
        if (Address != nullptr)
        {
            --LiveAllocations;
        }
        LE::GetDefaultAllocator().Deallocate(Address, Size, Alignment);
    }

    std::size_t AllocationAttempts = 0;
    std::size_t FailureAttempt = 0;
    std::size_t LiveAllocations = 0;
};

struct FManualDescriptors
{
    FManualDescriptors()
    {
        TypeId = ParseStableId<LE::FTypeId>("10000000-0000-4000-8000-000000000001");
        IntTypeId = ParseStableId<LE::FTypeId>("10000000-0000-4000-8000-000000000002");
        UInt64TypeId = ParseStableId<LE::FTypeId>("10000000-0000-4000-8000-000000000003");
        SignedEnumId = ParseStableId<LE::FTypeId>("30000000-0000-4000-8000-000000000001");
        UnsignedEnumId = ParseStableId<LE::FTypeId>("30000000-0000-4000-8000-000000000002");
        ValuePropertyId = ParseStableId<LE::FPropertyId>("20000000-0000-4000-8000-000000000001");
        ReadOnlyPropertyId = ParseStableId<LE::FPropertyId>("20000000-0000-4000-8000-000000000002");

        Properties[0].Id = ValuePropertyId;
        Properties[0].Name = ValuePropertyName;
        Properties[0].ValueTypeId = IntTypeId;
        Properties[0].Getter = &LE::TMemberReflectionThunks<&FManualReflectedRecord::Value>::Get;
        Properties[0].Setter = &LE::TMemberReflectionThunks<&FManualReflectedRecord::Value>::Set;
        Properties[1].Id = ReadOnlyPropertyId;
        Properties[1].Name = ReadOnlyPropertyName;
        Properties[1].ValueTypeId = UInt64TypeId;
        Properties[1].Getter =
            &LE::TMemberReflectionThunks<&FManualReflectedRecord::ReadOnlyValue>::Get;
        Properties[1].Setter = nullptr;

        Type.Id = TypeId;
        Type.Name = TypeName;
        Type.Kind = LE::EReflectedTypeKind::Struct;
        Type.Size = sizeof(FManualReflectedRecord);
        Type.Alignment = alignof(FManualReflectedRecord);
        Type.Properties = LE::Span<const LE::FPropertyDescriptor>(Properties);
        Type.Construct = &LE::TDefaultReflectionThunks<FManualReflectedRecord>::Construct;
        Type.Destruct = &LE::TDefaultReflectionThunks<FManualReflectedRecord>::Destruct;

        SignedValues[0] = { "Minimum", 0x80u };
        SignedValues[1] = { "NegativeOne", 0xffu };
        SignedValues[2] = { "NegativeOneAlias", 0xffu };
        SignedValues[3] = { "Maximum", 0x7fu };
        Enums[0].Id = SignedEnumId;
        Enums[0].Name = SignedEnumName;
        Enums[0].UnderlyingType = LE::EEnumUnderlyingType::Int8;
        Enums[0].Values = LE::Span<const LE::FEnumValueDescriptor>(SignedValues);

        UnsignedValues[0] = { "Zero", 0u };
        UnsignedValues[1] = { "Maximum", (std::numeric_limits<LE::uint64>::max)() };
        Enums[1].Id = UnsignedEnumId;
        Enums[1].Name = UnsignedEnumName;
        Enums[1].UnderlyingType = LE::EEnumUnderlyingType::UInt64;
        Enums[1].Values = LE::Span<const LE::FEnumValueDescriptor>(UnsignedValues);

        Module.Name = ModuleName;
        Module.Types = LE::Span<const LE::FTypeDescriptor>(&Type, 1);
        Module.Enums = LE::Span<const LE::FEnumDescriptor>(Enums);
    }

    char ModuleName[24] = "BuilderTests.Reflection";
    char TypeName[24] = "Tests.ManualRecord";
    char ValuePropertyName[16] = "Value";
    char ReadOnlyPropertyName[16] = "ReadOnlyValue";
    char SignedEnumName[24] = "Tests.SignedByte";
    char UnsignedEnumName[24] = "Tests.UnsignedWide";

    LE::FTypeId TypeId;
    LE::FTypeId IntTypeId;
    LE::FTypeId UInt64TypeId;
    LE::FTypeId SignedEnumId;
    LE::FTypeId UnsignedEnumId;
    LE::FPropertyId ValuePropertyId;
    LE::FPropertyId ReadOnlyPropertyId;
    LE::FPropertyDescriptor Properties[2];
    LE::FTypeDescriptor Type;
    LE::FEnumValueDescriptor SignedValues[4];
    LE::FEnumValueDescriptor UnsignedValues[2];
    LE::FEnumDescriptor Enums[2];
    LE::FReflectionModuleDescriptor Module;
};

bool RegisterManualTestsReflection(
    LE::FReflectionRegistry& Registry,
    const LE::FReflectionModuleDescriptor& Descriptor,
    LE::FReflectionModuleHandle& OutHandle) noexcept
{
    return Registry.RegisterModule(Descriptor, OutHandle) == LE::EReflectionRegisterResult::Success;
}

bool UnregisterManualTestsReflection(
    LE::FReflectionRegistry& Registry,
    const LE::FReflectionModuleHandle Handle) noexcept
{
    return Registry.UnregisterModule(Handle) == LE::EReflectionUnregisterResult::Success;
}

} // namespace

namespace LE
{

int RunReflectionTests()
{
    int Failures = 0;
    static_assert(!std::is_copy_constructible<LE::FReflectionRegistry>::value &&
        !std::is_move_constructible<LE::FReflectionRegistry>::value,
        "the reflection registry must retain one PImpl and lifetime domain");
    static_assert(std::is_same<decltype(LE::FTypeInfo::Size), std::size_t>::value,
        "reflected native size is represented without narrowing");

    FManualDescriptors Descriptors;
    LE::FReflectionRegistry Registry;
    LE::FReflectionModuleHandle ModuleHandle{ 77u, 77u };
    Failures += ExpectReflection(
        RegisterManualTestsReflection(Registry, Descriptors.Module, ModuleHandle) &&
            ModuleHandle.IsValid() && Registry.GetModuleCount() == 1 &&
            Registry.GetTypeCount() == 1 && Registry.GetEnumCount() == 2,
        "manual provider registration commits one complete reflection module");

    const LE::FTypeInfo* StableLookup = Registry.FindType(Descriptors.TypeId);
    const LE::FEnumInfo* SignedEnum = Registry.FindEnum(Descriptors.SignedEnumId);
    Failures += ExpectReflection(
        StableLookup != nullptr && Registry.FindType("Tests.ManualRecord") == StableLookup &&
            StableLookup->Name == LE::StringView("Tests.ManualRecord") &&
            StableLookup->ModuleName == LE::StringView("BuilderTests.Reflection") &&
            StableLookup->ModuleHandle == ModuleHandle &&
            StableLookup->Properties.Size() == 2 && SignedEnum != nullptr,
        "lookup exposes registry-owned type enum module and property metadata");

    const auto ExpectRejected = [&](
        const LE::FReflectionModuleDescriptor& Descriptor,
        const LE::EReflectionRegisterResult Expected,
        const char* const Name)
    {
        LE::FReflectionModuleHandle Preserved{ 91u, 92u };
        const LE::FReflectionModuleHandle Before = Preserved;
        const std::size_t ModuleCount = Registry.GetModuleCount();
        const std::size_t TypeCount = Registry.GetTypeCount();
        const std::size_t EnumCount = Registry.GetEnumCount();
        const LE::EReflectionRegisterResult Result = Registry.RegisterModule(Descriptor, Preserved);
        return ExpectReflection(Result == Expected && Preserved == Before &&
                Registry.GetModuleCount() == ModuleCount &&
                Registry.GetTypeCount() == TypeCount && Registry.GetEnumCount() == EnumCount &&
                Registry.FindType(Descriptors.TypeId) == StableLookup &&
                StableLookup->Name == LE::StringView("Tests.ManualRecord") &&
                StableLookup->Properties.Size() == 2 &&
                StableLookup->Properties[0].Name == LE::StringView("Value"),
            Name);
    };

    LE::FReflectionModuleDescriptor DuplicateModule;
    DuplicateModule.Name = "BuilderTests.Reflection";
    Failures += ExpectRejected(DuplicateModule, LE::EReflectionRegisterResult::DuplicateModuleName,
        "duplicate module name is rejected atomically");
    LE::FReflectionModuleDescriptor EmptyNameModule;
    Failures += ExpectRejected(EmptyNameModule, LE::EReflectionRegisterResult::InvalidDescriptor,
        "empty module name is invalid");

    LE::FTypeDescriptor DuplicateTypeId = Descriptors.Type;
    DuplicateTypeId.Name = "Tests.OtherName";
    LE::FReflectionModuleDescriptor DuplicateTypeIdModule{
        "Tests.DuplicateTypeId", LE::Span<const LE::FTypeDescriptor>(&DuplicateTypeId, 1), {} };
    Failures += ExpectRejected(DuplicateTypeIdModule, LE::EReflectionRegisterResult::DuplicateTypeId,
        "duplicate type ID is rejected across modules");

    LE::FTypeDescriptor DuplicateTypeName = Descriptors.Type;
    DuplicateTypeName.Id = ParseStableId<LE::FTypeId>(
        "10000000-0000-4000-8000-000000000099");
    LE::FReflectionModuleDescriptor DuplicateTypeNameModule{
        "Tests.DuplicateTypeName", LE::Span<const LE::FTypeDescriptor>(&DuplicateTypeName, 1), {} };
    Failures += ExpectRejected(DuplicateTypeNameModule,
        LE::EReflectionRegisterResult::DuplicateTypeName,
        "duplicate qualified type name is rejected across modules");

    LE::FEnumDescriptor CrossKindDuplicate = Descriptors.Enums[0];
    CrossKindDuplicate.Id = Descriptors.TypeId;
    CrossKindDuplicate.Name = "Tests.CrossKindId";
    LE::FReflectionModuleDescriptor CrossKindDuplicateModule{
        "Tests.CrossKindId", {}, LE::Span<const LE::FEnumDescriptor>(&CrossKindDuplicate, 1) };
    Failures += ExpectRejected(CrossKindDuplicateModule,
        LE::EReflectionRegisterResult::DuplicateTypeId,
        "struct and enum descriptors share one global type ID namespace");
    CrossKindDuplicate.Id = ParseStableId<LE::FTypeId>(
        "40000000-0000-4000-8000-000000000090");
    CrossKindDuplicate.Name = "Tests.ManualRecord";
    Failures += ExpectRejected(CrossKindDuplicateModule,
        LE::EReflectionRegisterResult::DuplicateTypeName,
        "struct and enum descriptors share one qualified-name namespace");

    LE::FTypeDescriptor BatchTypes[2] = { Descriptors.Type, Descriptors.Type };
    BatchTypes[0].Id = ParseStableId<LE::FTypeId>("40000000-0000-4000-8000-000000000001");
    BatchTypes[0].Name = "Tests.BatchA";
    BatchTypes[1].Id = BatchTypes[0].Id;
    BatchTypes[1].Name = "Tests.BatchB";
    LE::FReflectionModuleDescriptor DuplicateBatch{
        "Tests.DuplicateBatch", LE::Span<const LE::FTypeDescriptor>(BatchTypes), {} };
    Failures += ExpectRejected(DuplicateBatch, LE::EReflectionRegisterResult::DuplicateTypeId,
        "duplicate type identity inside one registration batch rolls back the batch");
    BatchTypes[1].Id = ParseStableId<LE::FTypeId>(
        "40000000-0000-4000-8000-000000000091");
    BatchTypes[1].Name = BatchTypes[0].Name;
    Failures += ExpectRejected(DuplicateBatch, LE::EReflectionRegisterResult::DuplicateTypeName,
        "duplicate qualified name inside one registration batch rolls back the batch");

    LE::FPropertyDescriptor DuplicateProperties[2] = {
        Descriptors.Properties[0], Descriptors.Properties[1]
    };
    DuplicateProperties[1].Id = DuplicateProperties[0].Id;
    LE::FTypeDescriptor DuplicatePropertyType = Descriptors.Type;
    DuplicatePropertyType.Id = ParseStableId<LE::FTypeId>(
        "40000000-0000-4000-8000-000000000002");
    DuplicatePropertyType.Name = "Tests.DuplicateProperty";
    DuplicatePropertyType.Properties = LE::Span<const LE::FPropertyDescriptor>(DuplicateProperties);
    LE::FReflectionModuleDescriptor DuplicatePropertyModule{
        "Tests.DuplicateProperty", LE::Span<const LE::FTypeDescriptor>(&DuplicatePropertyType, 1), {} };
    Failures += ExpectRejected(DuplicatePropertyModule,
        LE::EReflectionRegisterResult::DuplicatePropertyId,
        "property IDs are unique within their owner type");
    DuplicateProperties[1].Id = Descriptors.ReadOnlyPropertyId;
    DuplicateProperties[1].Name = DuplicateProperties[0].Name;
    Failures += ExpectRejected(DuplicatePropertyModule,
        LE::EReflectionRegisterResult::DuplicatePropertyName,
        "property names are unique within their owner type");

    LE::FTypeDescriptor InvalidType = Descriptors.Type;
    InvalidType.Alignment = 3;
    InvalidType.Id = ParseStableId<LE::FTypeId>("40000000-0000-4000-8000-000000000003");
    InvalidType.Name = "Tests.InvalidAlignment";
    LE::FReflectionModuleDescriptor InvalidModule{
        "Tests.Invalid", LE::Span<const LE::FTypeDescriptor>(&InvalidType, 1), {} };
    Failures += ExpectRejected(InvalidModule, LE::EReflectionRegisterResult::InvalidDescriptor,
        "non-power-of-two reflected alignment is invalid");
    InvalidType = Descriptors.Type;
    InvalidType.Id = ParseStableId<LE::FTypeId>("40000000-0000-4000-8000-000000000092");
    InvalidType.Name = "Tests.ZeroSize";
    InvalidType.Size = 0;
    Failures += ExpectRejected(InvalidModule, LE::EReflectionRegisterResult::InvalidDescriptor,
        "zero reflected native size is invalid");
    InvalidType = Descriptors.Type;
    InvalidType.Id = ParseStableId<LE::FTypeId>("40000000-0000-4000-8000-000000000093");
    InvalidType.Name = "Tests.InvalidKind";
    InvalidType.Kind = static_cast<LE::EReflectedTypeKind>(0xffu);
    Failures += ExpectRejected(InvalidModule, LE::EReflectionRegisterResult::InvalidDescriptor,
        "unknown reflected type kind is invalid");
    InvalidType = Descriptors.Type;
    InvalidType.Id = {};
    InvalidType.Name = "Tests.NilType";
    Failures += ExpectRejected(InvalidModule, LE::EReflectionRegisterResult::InvalidDescriptor,
        "nil reflected type ID is invalid");
    InvalidType = Descriptors.Type;
    InvalidType.Id = LE::GetBuiltinPropertyTypeId(LE::EPropertyBuiltinType::Bool);
    InvalidType.Name = "Tests.ReservedBuiltinType";
    Failures += ExpectRejected(InvalidModule, LE::EReflectionRegisterResult::DuplicateTypeId,
        "builtin property type IDs are reserved from user reflected types");
    InvalidType = Descriptors.Type;
    InvalidType.Id = ParseStableId<LE::FTypeId>("40000000-0000-4000-8000-000000000004");
    InvalidType.Name = LE::StringView("Bad\0Name", 8);
    Failures += ExpectRejected(InvalidModule, LE::EReflectionRegisterResult::InvalidDescriptor,
        "embedded NUL metadata names are invalid");
    InvalidType = Descriptors.Type;
    InvalidType.Id = ParseStableId<LE::FTypeId>("40000000-0000-4000-8000-000000000005");
    InvalidType.Name = "Tests.NullConstruct";
    InvalidType.Construct = nullptr;
    Failures += ExpectRejected(InvalidModule, LE::EReflectionRegisterResult::InvalidDescriptor,
        "missing reflected construction thunk is invalid");
    InvalidType = Descriptors.Type;
    InvalidType.Id = ParseStableId<LE::FTypeId>("40000000-0000-4000-8000-000000000006");
    InvalidType.Name = "Tests.NullDestruct";
    InvalidType.Destruct = nullptr;
    Failures += ExpectRejected(InvalidModule, LE::EReflectionRegisterResult::InvalidDescriptor,
        "missing reflected destruction thunk is invalid");

    LE::FPropertyDescriptor InvalidProperty = Descriptors.Properties[0];
    InvalidProperty.Getter = nullptr;
    InvalidType = Descriptors.Type;
    InvalidType.Id = ParseStableId<LE::FTypeId>("40000000-0000-4000-8000-000000000007");
    InvalidType.Name = "Tests.InvalidProperty";
    InvalidType.Properties = LE::Span<const LE::FPropertyDescriptor>(&InvalidProperty, 1);
    Failures += ExpectRejected(InvalidModule, LE::EReflectionRegisterResult::InvalidDescriptor,
        "missing property getter thunk is invalid while a null setter is read-only");
    InvalidProperty = Descriptors.Properties[0];
    InvalidProperty.Id = {};
    Failures += ExpectRejected(InvalidModule, LE::EReflectionRegisterResult::InvalidDescriptor,
        "nil property ID is invalid");
    InvalidProperty = Descriptors.Properties[0];
    InvalidProperty.ValueTypeId = {};
    Failures += ExpectRejected(InvalidModule, LE::EReflectionRegisterResult::InvalidDescriptor,
        "nil property value type ID is invalid");
    InvalidProperty = Descriptors.Properties[0];
    InvalidProperty.Name = {};
    Failures += ExpectRejected(InvalidModule, LE::EReflectionRegisterResult::InvalidDescriptor,
        "empty property name is invalid");

    LE::FEnumValueDescriptor InvalidValue{ "TooWide", 0x100u };
    LE::FEnumDescriptor InvalidEnum;
    InvalidEnum.Id = ParseStableId<LE::FTypeId>("40000000-0000-4000-8000-000000000008");
    InvalidEnum.Name = "Tests.InvalidEnum";
    InvalidEnum.UnderlyingType = LE::EEnumUnderlyingType::UInt8;
    InvalidEnum.Values = LE::Span<const LE::FEnumValueDescriptor>(&InvalidValue, 1);
    LE::FReflectionModuleDescriptor InvalidEnumModule{
        "Tests.InvalidEnum", {}, LE::Span<const LE::FEnumDescriptor>(&InvalidEnum, 1) };
    Failures += ExpectRejected(InvalidEnumModule, LE::EReflectionRegisterResult::InvalidDescriptor,
        "enum raw bits outside the underlying width are invalid");
    InvalidValue.ValueBits = 0;
    InvalidEnum.UnderlyingType = static_cast<LE::EEnumUnderlyingType>(0xffu);
    Failures += ExpectRejected(InvalidEnumModule, LE::EReflectionRegisterResult::InvalidDescriptor,
        "unknown enum underlying kind is invalid even for zero bits");
    LE::FEnumValueDescriptor DuplicateValueNames[2] = {
        { "Repeated", 0u }, { "Repeated", 1u }
    };
    InvalidEnum.UnderlyingType = LE::EEnumUnderlyingType::UInt8;
    InvalidEnum.Values = LE::Span<const LE::FEnumValueDescriptor>(DuplicateValueNames);
    Failures += ExpectRejected(InvalidEnumModule, LE::EReflectionRegisterResult::InvalidDescriptor,
        "duplicate enum value names are invalid while duplicate numeric aliases remain valid");

    LE::int64 PreservedSigned = 123;
    LE::uint64 PreservedUnsigned = 456;
    Failures += ExpectReflection(
        !LE::IsCanonicalEnumValueBits(static_cast<LE::EEnumUnderlyingType>(0xffu), 0) &&
            !LE::TryInterpretEnumValueAsSigned(
                static_cast<LE::EEnumUnderlyingType>(0xffu), 0, PreservedSigned) &&
            !LE::TryInterpretEnumValueAsUnsigned(
                static_cast<LE::EEnumUnderlyingType>(0xffu), 0, PreservedUnsigned) &&
            PreservedSigned == 123 && PreservedUnsigned == 456,
        "invalid enum kind is rejected by every public numeric helper");

    std::memset(Descriptors.ModuleName, 'm', std::strlen(Descriptors.ModuleName));
    std::memset(Descriptors.TypeName, 't', std::strlen(Descriptors.TypeName));
    std::memset(Descriptors.ValuePropertyName, 'p',
        std::strlen(Descriptors.ValuePropertyName));
    Descriptors.Properties[0].Getter = nullptr;
    Descriptors.SignedValues[0].ValueBits = 0;
    const LE::FTypeInfo* CopiedType = Registry.FindType(Descriptors.TypeId);
    const LE::FPropertyInfo* CopiedProperty =
        CopiedType == nullptr ? nullptr : CopiedType->FindProperty(Descriptors.ValuePropertyId);
    SignedEnum = Registry.FindEnum(Descriptors.SignedEnumId);
    Failures += ExpectReflection(CopiedType != nullptr && CopiedProperty != nullptr &&
            CopiedType->Name == LE::StringView("Tests.ManualRecord") &&
            CopiedType->ModuleName == LE::StringView("BuilderTests.Reflection") &&
            CopiedProperty->Name == LE::StringView("Value") &&
            CopiedProperty->Getter != nullptr && SignedEnum != nullptr &&
            SignedEnum->FindValue("Minimum") != nullptr &&
            SignedEnum->FindValue("Minimum")->ValueBits == 0x80u,
        "successful registration deep-copies every borrowed string span and descriptor value");

    alignas(FManualReflectedRecord) unsigned char Storage[sizeof(FManualReflectedRecord)];
    const int ConstructionsBefore = FManualReflectedRecord::ConstructionCount;
    const int DestructionsBefore = FManualReflectedRecord::DestructionCount;
    Failures += ExpectReflection(CopiedType->Construct(Storage) &&
            FManualReflectedRecord::ConstructionCount == ConstructionsBefore + 1 &&
            FManualReflectedRecord::LiveCount == 1,
        "manual construction thunk placement-constructs in aligned caller storage");
    auto* const Instance = reinterpret_cast<FManualReflectedRecord*>(Storage);
    const int NewValue = 73;
    const LE::FPropertyInfo* ValueProperty = CopiedType->FindProperty("Value");
    const LE::FPropertyInfo* ReadOnlyProperty =
        CopiedType->FindProperty(Descriptors.ReadOnlyPropertyId);
    Failures += ExpectReflection(ValueProperty != nullptr && ReadOnlyProperty != nullptr &&
            *static_cast<const int*>(ValueProperty->Getter(Instance)) == 17 &&
            ValueProperty->Setter(Instance, &NewValue) && Instance->Value == 73 &&
            ReadOnlyProperty->IsReadOnly() && ReadOnlyProperty->Setter == nullptr,
        "getter setter thunks access a writable member and preserve explicit read-only metadata");
    alignas(FManualReflectedRecord) unsigned char MisalignedObject[sizeof(FManualReflectedRecord) + 1];
    alignas(int) unsigned char MisalignedValue[sizeof(int) + 1];
    Failures += ExpectReflection(!CopiedType->Construct(nullptr) &&
            !CopiedType->Construct(MisalignedObject + 1) &&
            ValueProperty->Getter(nullptr) == nullptr &&
            ValueProperty->Getter(MisalignedObject + 1) == nullptr &&
            !ValueProperty->Setter(nullptr, &NewValue) &&
            !ValueProperty->Setter(Instance, nullptr) &&
            !ValueProperty->Setter(Instance, MisalignedValue + 1) &&
            !CopiedType->Destruct(nullptr) && !CopiedType->Destruct(MisalignedObject + 1),
        "manual thunk helpers reject null and misaligned object value and storage pointers");
    Failures += ExpectReflection(CopiedType->Destruct(Instance) &&
            FManualReflectedRecord::DestructionCount == DestructionsBefore + 1 &&
            FManualReflectedRecord::LiveCount == 0,
        "manual destruction thunk destroys exactly one live reflected instance");

    LE::int64 SignedValue = 0;
    LE::uint64 UnsignedValue = 0;
    const LE::FEnumValueInfo* NegativeOne = SignedEnum->FindValueByBits(0xffu);
    const LE::FEnumInfo* UnsignedEnum = Registry.FindEnum(Descriptors.UnsignedEnumId);
    Failures += ExpectReflection(NegativeOne != nullptr &&
            NegativeOne->Name == LE::StringView("NegativeOne") &&
            LE::TryInterpretEnumValueAsSigned(
                LE::EEnumUnderlyingType::Int8, 0x80u, SignedValue) && SignedValue == -128 &&
            LE::TryInterpretEnumValueAsSigned(
                LE::EEnumUnderlyingType::Int8, 0xffu, SignedValue) && SignedValue == -1 &&
            UnsignedEnum != nullptr && UnsignedEnum->FindValueByBits(
                (std::numeric_limits<LE::uint64>::max)()) != nullptr &&
            LE::TryInterpretEnumValueAsUnsigned(LE::EEnumUnderlyingType::UInt64,
                (std::numeric_limits<LE::uint64>::max)(), UnsignedValue) &&
            UnsignedValue == (std::numeric_limits<LE::uint64>::max)(),
        "enum helpers preserve signed boundaries unsigned width and declaration-first aliases");
    LE::int64 SignedWide = 0;
    Failures += ExpectReflection(
        LE::TryInterpretEnumValueAsSigned(LE::EEnumUnderlyingType::Int64,
            uint64{ 1 } << 63, SignedWide) &&
            SignedWide == (std::numeric_limits<LE::int64>::min)(),
        "signed enum interpretation preserves the int64 minimum boundary");

    FManualDescriptors OwnerLocalDescriptors;
    OwnerLocalDescriptors.Module.Name = "Tests.OwnerLocalProperty";
    OwnerLocalDescriptors.Type.Id = ParseStableId<LE::FTypeId>(
        "40000000-0000-4000-8000-000000000094");
    OwnerLocalDescriptors.Type.Name = "Tests.OwnerLocalRecord";
    OwnerLocalDescriptors.Module.Enums = {};
    LE::FReflectionModuleHandle OwnerLocalHandle;
    Failures += ExpectReflection(
        Registry.RegisterModule(OwnerLocalDescriptors.Module, OwnerLocalHandle) ==
                LE::EReflectionRegisterResult::Success &&
            Registry.FindType(OwnerLocalDescriptors.Type.Id)->FindProperty(
                Descriptors.ValuePropertyId) != nullptr &&
            Registry.UnregisterModule(OwnerLocalHandle) ==
                LE::EReflectionUnregisterResult::Success,
        "the same property ID is valid in a different owner type context");

    LE::FReflectionModuleDescriptor EmptySecond;
    EmptySecond.Name = "Tests.Second";
    LE::FReflectionModuleHandle SecondHandle;
    Failures += ExpectReflection(
        Registry.RegisterModule(EmptySecond, SecondHandle) ==
                LE::EReflectionRegisterResult::Success &&
            Registry.GetModuleCount() == 2 &&
            Registry.UnregisterModule(SecondHandle) == LE::EReflectionUnregisterResult::Success &&
            Registry.GetModuleCount() == 1 && Registry.FindType(Descriptors.TypeId) != nullptr,
        "unregister removes only the exact module instance and preserves other modules");
    Failures += ExpectReflection(
        Registry.UnregisterModule(SecondHandle) == LE::EReflectionUnregisterResult::NotFound,
        "a stale module handle cannot unregister another registration");

    Failures += ExpectReflection(UnregisterManualTestsReflection(Registry, ModuleHandle) &&
            Registry.GetModuleCount() == 0 && Registry.FindType(Descriptors.TypeId) == nullptr &&
            Registry.FindEnum(Descriptors.SignedEnumId) == nullptr,
        "explicit provider unregistration removes all metadata before provider unload");

    FReflectionFailingAllocator Allocator;
    {
        LE::FReflectionRegistry FailureRegistry(Allocator);
        LE::FReflectionModuleDescriptor EmptyModule;
        EmptyModule.Name = "Tests.AtomicAllocation";
        LE::FReflectionModuleHandle Preserved{ 51u, 52u };
        const LE::FReflectionModuleHandle Before = Preserved;
        // The next allocation copies the module name; the following allocation
        // reserves the single outer commit container. Fail at that late point.
        Allocator.FailureAttempt = Allocator.AllocationAttempts + 2;
        Failures += ExpectReflection(
            FailureRegistry.RegisterModule(EmptyModule, Preserved) ==
                    LE::EReflectionRegisterResult::AllocationFailed &&
                Preserved == Before && FailureRegistry.GetModuleCount() == 0,
            "late allocation failure leaves the single registry commit unit untouched");
        Allocator.FailureAttempt = 0;
        LE::FReflectionModuleHandle FirstSuccessful;
        Failures += ExpectReflection(
            FailureRegistry.RegisterModule(EmptyModule, FirstSuccessful) ==
                    LE::EReflectionRegisterResult::Success &&
                FirstSuccessful.Index == 0 && FirstSuccessful.Generation == 1,
            "failed registration consumes no observable module handle generation");
    }
    Failures += ExpectReflection(Allocator.LiveAllocations == 0,
        "registry PImpl metadata and handle storage free through the originating allocator");

    const int ProviderDestructionsBefore = FManualReflectedRecord::DestructionCount;
    {
        FManualDescriptors FreshDescriptors;
        LE::FReflectionRegistry DropOnlyRegistry;
        LE::FReflectionModuleHandle DropOnlyHandle;
        static_cast<void>(DropOnlyRegistry.RegisterModule(FreshDescriptors.Module, DropOnlyHandle));
    }
    Failures += ExpectReflection(
        FManualReflectedRecord::DestructionCount == ProviderDestructionsBefore,
        "registry destruction drops borrowed thunk addresses without invoking provider code");

    {
        LE::FReflectionRegistry WorldRegistry;
        LE::FReflectionModuleHandle WorldHandle;
        const LE::EReflectionRegisterResult WorldResult =
            LE::RegisterWorldReflection(WorldRegistry, WorldHandle);
        const LE::FTypeInfo* WorldType = WorldRegistry.FindType("LE.FWorld");
        alignas(LE::FWorld) unsigned char WorldStorage[sizeof(LE::FWorld)];
        Failures += ExpectReflection(
            WorldResult == LE::EReflectionRegisterResult::Success &&
                WorldHandle.IsValid() && WorldType != nullptr &&
                WorldType->Properties.IsEmpty() && WorldType->Construct(WorldStorage) &&
                WorldType->Destruct(WorldStorage) &&
                LE::UnregisterWorldReflection(WorldRegistry, WorldHandle) ==
                    LE::EReflectionUnregisterResult::Success,
            "World provider explicitly registers invokes and unregisters its DLL-owned thunks");
    }

    {
        LE::FReflectionRegistry GeneratedRegistry;
        LE::FReflectionModuleHandle GeneratedHandle;
        const LE::EReflectionRegisterResult GeneratedResult =
            LE::RegisterBuilderTestsReflection(GeneratedRegistry, GeneratedHandle);
        const LE::FTypeInfo* Owner = GeneratedRegistry.FindType("LE.FGeneratedPrivateOwner");
        const LE::FEnumInfo* Mode = GeneratedRegistry.FindEnum("LE.EGeneratedMode");
        void* Storage = Owner != nullptr
            ? LE::GetDefaultAllocator().Allocate(Owner->Size, Owner->Alignment)
            : nullptr;
        const LE::FPropertyInfo* Value = Owner != nullptr
            ? Owner->FindProperty(ParseStableId<LE::FPropertyId>("61000000-0000-4000-8000-000000000004"))
            : nullptr;
        const LE::FPropertyInfo* ReadOnly = Owner != nullptr
            ? Owner->FindProperty(ParseStableId<LE::FPropertyId>("61000000-0000-4000-8000-000000000005"))
            : nullptr;
        LE::FGeneratedValue Replacement;
        Replacement.Payload = 42;
        void* const MisalignedStorage = Storage != nullptr
            ? static_cast<void*>(static_cast<unsigned char*>(Storage) + 1)
            : nullptr;
        const void* const MisalignedValue = static_cast<const void*>(
            reinterpret_cast<const unsigned char*>(&Replacement) + 1);
        const bool bInvalidThunkInputsRejected = Owner != nullptr && Value != nullptr &&
            !Owner->Construct(nullptr) && !Owner->Construct(MisalignedStorage) &&
            !Owner->Destruct(nullptr) && !Owner->Destruct(MisalignedStorage) &&
            Value->Getter(nullptr) == nullptr && Value->Getter(MisalignedStorage) == nullptr &&
            Value->Setter != nullptr && !Value->Setter(nullptr, &Replacement) &&
            !Value->Setter(MisalignedStorage, &Replacement) &&
            !Value->Setter(Storage, nullptr) && !Value->Setter(Storage, MisalignedValue);
        const bool bConstructed = Owner != nullptr && Storage != nullptr && Owner->Construct(Storage);
        const bool bSet = bConstructed && Value != nullptr && Value->Setter != nullptr &&
            Value->Setter(Storage, &Replacement);
        const auto* const ReadBack = bSet
            ? static_cast<const LE::FGeneratedValue*>(Value->Getter(Storage))
            : nullptr;
        const bool bMetadataValid = GeneratedResult == LE::EReflectionRegisterResult::Success &&
            GeneratedHandle.IsValid() && Owner != nullptr && Owner->Properties.Size() == 4;
        const bool bValueValid = bConstructed && bSet && ReadBack != nullptr && ReadBack->Payload == 42;
        const bool bReadOnlyValid = ReadOnly != nullptr && ReadOnly->Getter(Storage) != nullptr &&
            ReadOnly->Setter == nullptr;
        const bool bEnumValid = Mode != nullptr && Mode->UnderlyingType == LE::EEnumUnderlyingType::UInt8 &&
            Mode->FindValueByBits(255) != nullptr;
        const bool bDestructed = bConstructed && Owner->Destruct(Storage);
        const std::size_t OwnerSize = Owner != nullptr ? Owner->Size : 0;
        const std::size_t OwnerAlignment = Owner != nullptr ? Owner->Alignment : 0;
        if (Storage != nullptr && Owner != nullptr)
        {
            LE::GetDefaultAllocator().Deallocate(Storage, OwnerSize, OwnerAlignment);
        }
        const LE::EReflectionUnregisterResult GeneratedUnregisterResult =
            LE::UnregisterBuilderTestsReflection(GeneratedRegistry, GeneratedHandle);
        Failures += ExpectReflection(
            bMetadataValid && bInvalidThunkInputsRejected && bValueValid && bReadOnlyValid &&
                bEnumValid && bDestructed &&
                GeneratedUnregisterResult == LE::EReflectionUnregisterResult::Success,
            "generated provider accesses private construction properties readonly policy and enum metadata");
    }

    return Failures;
}

} // namespace LE
