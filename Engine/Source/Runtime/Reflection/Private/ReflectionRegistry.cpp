#include "Reflection/ReflectionRegistry.h"

#include "Containers/Array.h"
#include "Containers/String.h"
#include "Memory/Allocator.h"
#include "Reflection/PropertyValue.h"

#include <new>
#include <utility>

namespace LE
{
namespace
{

struct FOwnedType
{
    explicit FOwnedType(IAllocator& Allocator) noexcept
        : Name(Allocator)
        , PropertyNames(Allocator)
        , Properties(Allocator)
    {
    }

    FTypeInfo& RefreshInfo(
        const StringView ModuleName,
        const FReflectionModuleHandle ModuleHandle) noexcept
    {
        // Public views are rebuilt from final owned storage on every lookup;
        // no view created while assembling the transaction escapes.
        for (std::size_t Index = 0; Index < Properties.Size(); ++Index)
        {
            Properties[Index].Name = PropertyNames[Index];
        }
        Info.Id = Id;
        Info.Name = Name;
        Info.ModuleName = ModuleName;
        Info.ModuleHandle = ModuleHandle;
        Info.Kind = Kind;
        Info.Size = Size;
        Info.Alignment = Alignment;
        Info.Properties = Properties.AsSpan();
        Info.Construct = Construct;
        Info.Destruct = Destruct;
        return Info;
    }

    FTypeId Id;
    String Name;
    EReflectedTypeKind Kind = EReflectedTypeKind::Struct;
    std::size_t Size = 0;
    std::size_t Alignment = 0;
    Array<String> PropertyNames;
    Array<FPropertyInfo> Properties;
    FReflectionConstructThunk Construct = nullptr;
    FReflectionDestructThunk Destruct = nullptr;
    FTypeInfo Info;
};

struct FOwnedEnum
{
    explicit FOwnedEnum(IAllocator& Allocator) noexcept
        : Name(Allocator)
        , ValueNames(Allocator)
        , Values(Allocator)
    {
    }

    FEnumInfo& RefreshInfo(
        const StringView ModuleName,
        const FReflectionModuleHandle ModuleHandle) noexcept
    {
        for (std::size_t Index = 0; Index < Values.Size(); ++Index)
        {
            Values[Index].Name = ValueNames[Index];
        }
        Info.Id = Id;
        Info.Name = Name;
        Info.ModuleName = ModuleName;
        Info.ModuleHandle = ModuleHandle;
        Info.UnderlyingType = UnderlyingType;
        Info.Values = Values.AsSpan();
        return Info;
    }

    FTypeId Id;
    String Name;
    EEnumUnderlyingType UnderlyingType = EEnumUnderlyingType::Int32;
    Array<String> ValueNames;
    Array<FEnumValueInfo> Values;
    FEnumInfo Info;
};

struct FOwnedModule
{
    explicit FOwnedModule(IAllocator& Allocator) noexcept
        : Name(Allocator)
        , Types(Allocator)
        , Enums(Allocator)
    {
    }

    String Name;
    FReflectionModuleHandle Handle;
    Array<FOwnedType> Types;
    Array<FOwnedEnum> Enums;
};

bool IsValidName(const StringView Name) noexcept
{
    if (Name.IsEmpty())
    {
        return false;
    }
    for (std::size_t Index = 0; Index < Name.Size(); ++Index)
    {
        if (Name[Index] == '\0')
        {
            return false;
        }
    }
    return true;
}

bool IsPowerOfTwo(const std::size_t Value) noexcept
{
    return Value != 0 && (Value & (Value - 1)) == 0;
}

bool IsValidTypeKind(const EReflectedTypeKind Kind) noexcept
{
    return Kind == EReflectedTypeKind::Struct || Kind == EReflectedTypeKind::Class;
}

bool IsValidUnderlyingType(const EEnumUnderlyingType Type) noexcept
{
    return IsValidEnumUnderlyingType(Type);
}

bool HasTypeIdentity(
    const FReflectionModuleDescriptor& Descriptor,
    const std::size_t BeforeType,
    const FTypeId Id,
    const StringView Name) noexcept
{
    for (std::size_t Index = 0; Index < BeforeType; ++Index)
    {
        if (Descriptor.Types[Index].Id == Id || Descriptor.Types[Index].Name == Name)
        {
            return true;
        }
    }
    for (const FEnumDescriptor& Enum : Descriptor.Enums)
    {
        if (Enum.Id == Id || Enum.Name == Name)
        {
            return true;
        }
    }
    return false;
}

bool HasEnumIdentity(
    const FReflectionModuleDescriptor& Descriptor,
    const std::size_t BeforeEnum,
    const FTypeId Id,
    const StringView Name) noexcept
{
    for (const FTypeDescriptor& Type : Descriptor.Types)
    {
        if (Type.Id == Id || Type.Name == Name)
        {
            return true;
        }
    }
    for (std::size_t Index = 0; Index < BeforeEnum; ++Index)
    {
        if (Descriptor.Enums[Index].Id == Id || Descriptor.Enums[Index].Name == Name)
        {
            return true;
        }
    }
    return false;
}

} // namespace

struct FReflectionRegistry::FImpl
{
    explicit FImpl(IAllocator& InAllocator) noexcept
        : Allocator(InAllocator)
        , Modules(InAllocator)
        , Handles(InAllocator)
    {
    }

    FOwnedModule* FindModule(const FReflectionModuleHandle Handle) noexcept
    {
        if (!Handles.IsAlive(Handle))
        {
            return nullptr;
        }
        for (FOwnedModule& Module : Modules)
        {
            if (Module.Handle == Handle)
            {
                return &Module;
            }
        }
        return nullptr;
    }

    const FOwnedModule* FindModuleByName(const StringView Name) const noexcept
    {
        for (const FOwnedModule& Module : Modules)
        {
            if (Module.Name == Name)
            {
                return &Module;
            }
        }
        return nullptr;
    }

    const FOwnedType* FindOwnedType(const FTypeId Id) const noexcept
    {
        for (const FOwnedModule& Module : Modules)
        {
            for (const FOwnedType& Type : Module.Types)
            {
                if (Type.Id == Id)
                {
                    return &Type;
                }
            }
        }
        return nullptr;
    }

    const FOwnedType* FindOwnedType(const StringView Name) const noexcept
    {
        for (const FOwnedModule& Module : Modules)
        {
            for (const FOwnedType& Type : Module.Types)
            {
                if (Type.Name == Name)
                {
                    return &Type;
                }
            }
        }
        return nullptr;
    }

    const FOwnedEnum* FindOwnedEnum(const FTypeId Id) const noexcept
    {
        for (const FOwnedModule& Module : Modules)
        {
            for (const FOwnedEnum& Enum : Module.Enums)
            {
                if (Enum.Id == Id)
                {
                    return &Enum;
                }
            }
        }
        return nullptr;
    }

    const FOwnedEnum* FindOwnedEnum(const StringView Name) const noexcept
    {
        for (const FOwnedModule& Module : Modules)
        {
            for (const FOwnedEnum& Enum : Module.Enums)
            {
                if (Enum.Name == Name)
                {
                    return &Enum;
                }
            }
        }
        return nullptr;
    }

    bool HasAnyIdentity(const FTypeId Id, const StringView Name) const noexcept
    {
        return FindOwnedType(Id) != nullptr || FindOwnedType(Name) != nullptr ||
            FindOwnedEnum(Id) != nullptr || FindOwnedEnum(Name) != nullptr;
    }

    EReflectionRegisterResult ValidateDescriptor(
        const FReflectionModuleDescriptor& Descriptor) const noexcept;

    IAllocator& Allocator;
    Array<FOwnedModule> Modules;
    TRuntimeHandlePool<FReflectionModuleHandleTag> Handles;
};

EReflectionRegisterResult FReflectionRegistry::FImpl::ValidateDescriptor(
    const FReflectionModuleDescriptor& Descriptor) const noexcept
{
    if (!IsValidName(Descriptor.Name))
    {
        return EReflectionRegisterResult::InvalidDescriptor;
    }
    if (FindModuleByName(Descriptor.Name) != nullptr)
    {
        return EReflectionRegisterResult::DuplicateModuleName;
    }

    for (std::size_t TypeIndex = 0; TypeIndex < Descriptor.Types.Size(); ++TypeIndex)
    {
        const FTypeDescriptor& Type = Descriptor.Types[TypeIndex];
        if (!Type.Id.IsValid() || !IsValidName(Type.Name) || !IsValidTypeKind(Type.Kind) ||
            Type.Size == 0 || !IsPowerOfTwo(Type.Alignment) ||
            (Type.Size % Type.Alignment) != 0 || Type.Construct == nullptr ||
            Type.Destruct == nullptr)
        {
            return EReflectionRegisterResult::InvalidDescriptor;
        }
        EPropertyBuiltinType ReservedBuiltinType;
        if (TryGetBuiltinPropertyType(Type.Id, ReservedBuiltinType))
        {
            return EReflectionRegisterResult::DuplicateTypeId;
        }
        if (HasAnyIdentity(Type.Id, Type.Name))
        {
            if (FindOwnedType(Type.Id) != nullptr || FindOwnedEnum(Type.Id) != nullptr)
            {
                return EReflectionRegisterResult::DuplicateTypeId;
            }
            return EReflectionRegisterResult::DuplicateTypeName;
        }
        if (HasTypeIdentity(Descriptor, TypeIndex, Type.Id, Type.Name))
        {
            for (std::size_t Previous = 0; Previous < TypeIndex; ++Previous)
            {
                if (Descriptor.Types[Previous].Id == Type.Id)
                {
                    return EReflectionRegisterResult::DuplicateTypeId;
                }
            }
            for (const FEnumDescriptor& Enum : Descriptor.Enums)
            {
                if (Enum.Id == Type.Id)
                {
                    return EReflectionRegisterResult::DuplicateTypeId;
                }
            }
            return EReflectionRegisterResult::DuplicateTypeName;
        }

        for (std::size_t PropertyIndex = 0; PropertyIndex < Type.Properties.Size(); ++PropertyIndex)
        {
            const FPropertyDescriptor& Property = Type.Properties[PropertyIndex];
            if (!Property.Id.IsValid() || !IsValidName(Property.Name) ||
                !Property.ValueTypeId.IsValid() || Property.Getter == nullptr)
            {
                return EReflectionRegisterResult::InvalidDescriptor;
            }
            for (std::size_t Previous = 0; Previous < PropertyIndex; ++Previous)
            {
                if (Type.Properties[Previous].Id == Property.Id)
                {
                    return EReflectionRegisterResult::DuplicatePropertyId;
                }
                if (Type.Properties[Previous].Name == Property.Name)
                {
                    return EReflectionRegisterResult::DuplicatePropertyName;
                }
            }
        }
    }

    for (std::size_t EnumIndex = 0; EnumIndex < Descriptor.Enums.Size(); ++EnumIndex)
    {
        const FEnumDescriptor& Enum = Descriptor.Enums[EnumIndex];
        if (!Enum.Id.IsValid() || !IsValidName(Enum.Name) ||
            !IsValidUnderlyingType(Enum.UnderlyingType))
        {
            return EReflectionRegisterResult::InvalidDescriptor;
        }
        EPropertyBuiltinType ReservedBuiltinType;
        if (TryGetBuiltinPropertyType(Enum.Id, ReservedBuiltinType))
        {
            return EReflectionRegisterResult::DuplicateTypeId;
        }
        if (HasAnyIdentity(Enum.Id, Enum.Name))
        {
            if (FindOwnedType(Enum.Id) != nullptr || FindOwnedEnum(Enum.Id) != nullptr)
            {
                return EReflectionRegisterResult::DuplicateTypeId;
            }
            return EReflectionRegisterResult::DuplicateTypeName;
        }
        if (HasEnumIdentity(Descriptor, EnumIndex, Enum.Id, Enum.Name))
        {
            for (const FTypeDescriptor& Type : Descriptor.Types)
            {
                if (Type.Id == Enum.Id)
                {
                    return EReflectionRegisterResult::DuplicateTypeId;
                }
            }
            for (std::size_t Previous = 0; Previous < EnumIndex; ++Previous)
            {
                if (Descriptor.Enums[Previous].Id == Enum.Id)
                {
                    return EReflectionRegisterResult::DuplicateTypeId;
                }
            }
            return EReflectionRegisterResult::DuplicateTypeName;
        }

        for (std::size_t ValueIndex = 0; ValueIndex < Enum.Values.Size(); ++ValueIndex)
        {
            const FEnumValueDescriptor& Value = Enum.Values[ValueIndex];
            if (!IsValidName(Value.Name) ||
                !IsCanonicalEnumValueBits(Enum.UnderlyingType, Value.ValueBits))
            {
                return EReflectionRegisterResult::InvalidDescriptor;
            }
            for (std::size_t Previous = 0; Previous < ValueIndex; ++Previous)
            {
                if (Enum.Values[Previous].Name == Value.Name)
                {
                    return EReflectionRegisterResult::InvalidDescriptor;
                }
            }
        }
    }
    return EReflectionRegisterResult::Success;
}

namespace
{

bool TryCopyType(
    IAllocator& Allocator,
    const FTypeDescriptor& Descriptor,
    FOwnedType& OutType) noexcept
{
    OutType.Id = Descriptor.Id;
    OutType.Kind = Descriptor.Kind;
    OutType.Size = Descriptor.Size;
    OutType.Alignment = Descriptor.Alignment;
    OutType.Construct = Descriptor.Construct;
    OutType.Destruct = Descriptor.Destruct;
    if (!OutType.Name.TryAssign(Descriptor.Name) ||
        !OutType.PropertyNames.TryReserve(Descriptor.Properties.Size()) ||
        !OutType.Properties.TryReserve(Descriptor.Properties.Size()))
    {
        return false;
    }

    for (const FPropertyDescriptor& DescriptorProperty : Descriptor.Properties)
    {
        String PropertyName(Allocator);
        if (!PropertyName.TryAssign(DescriptorProperty.Name) ||
            !OutType.PropertyNames.TryPushBack(std::move(PropertyName)))
        {
            return false;
        }

        FPropertyInfo Property;
        Property.Id = DescriptorProperty.Id;
        Property.Name = OutType.PropertyNames.Back();
        Property.ValueTypeId = DescriptorProperty.ValueTypeId;
        Property.Getter = DescriptorProperty.Getter;
        Property.Setter = DescriptorProperty.Setter;
        if (!OutType.Properties.TryPushBack(Property))
        {
            return false;
        }
    }
    return true;
}

bool TryCopyEnum(
    IAllocator& Allocator,
    const FEnumDescriptor& Descriptor,
    FOwnedEnum& OutEnum) noexcept
{
    OutEnum.Id = Descriptor.Id;
    OutEnum.UnderlyingType = Descriptor.UnderlyingType;
    if (!OutEnum.Name.TryAssign(Descriptor.Name) ||
        !OutEnum.ValueNames.TryReserve(Descriptor.Values.Size()) ||
        !OutEnum.Values.TryReserve(Descriptor.Values.Size()))
    {
        return false;
    }

    for (const FEnumValueDescriptor& DescriptorValue : Descriptor.Values)
    {
        String ValueName(Allocator);
        if (!ValueName.TryAssign(DescriptorValue.Name) ||
            !OutEnum.ValueNames.TryPushBack(std::move(ValueName)))
        {
            return false;
        }

        FEnumValueInfo Value;
        Value.Name = OutEnum.ValueNames.Back();
        Value.ValueBits = DescriptorValue.ValueBits;
        if (!OutEnum.Values.TryPushBack(Value))
        {
            return false;
        }
    }
    return true;
}

bool TryCopyModule(
    IAllocator& Allocator,
    const FReflectionModuleDescriptor& Descriptor,
    FOwnedModule& OutModule) noexcept
{
    if (!OutModule.Name.TryAssign(Descriptor.Name) ||
        !OutModule.Types.TryReserve(Descriptor.Types.Size()) ||
        !OutModule.Enums.TryReserve(Descriptor.Enums.Size()))
    {
        return false;
    }

    for (const FTypeDescriptor& DescriptorType : Descriptor.Types)
    {
        FOwnedType Type(Allocator);
        if (!TryCopyType(Allocator, DescriptorType, Type) ||
            !OutModule.Types.TryPushBack(std::move(Type)))
        {
            return false;
        }
    }
    for (const FEnumDescriptor& DescriptorEnum : Descriptor.Enums)
    {
        FOwnedEnum Enum(Allocator);
        if (!TryCopyEnum(Allocator, DescriptorEnum, Enum) ||
            !OutModule.Enums.TryPushBack(std::move(Enum)))
        {
            return false;
        }
    }
    return true;
}

} // namespace

FReflectionRegistry::FReflectionRegistry() noexcept
    : FReflectionRegistry(GetDefaultAllocator())
{
}

FReflectionRegistry::FReflectionRegistry(IAllocator& Allocator) noexcept
{
    Impl = TryAllocateArray<FImpl>(Allocator, 1);
    if (Impl == nullptr)
    {
        HandleOutOfMemory(sizeof(FImpl), alignof(FImpl));
    }
    new (Impl) FImpl(Allocator);
}

FReflectionRegistry::~FReflectionRegistry() noexcept
{
    if (Impl != nullptr)
    {
        // Provider thunks are borrowed code addresses. Destruction drops their
        // values only and deliberately never calls provider code.
        IAllocator& Allocator = Impl->Allocator;
        Impl->~FImpl();
        DeallocateArray(Allocator, Impl, 1);
        Impl = nullptr;
    }
}

EReflectionRegisterResult FReflectionRegistry::RegisterModule(
    const FReflectionModuleDescriptor& Descriptor,
    FReflectionModuleHandle& OutHandle) noexcept
{
    const EReflectionRegisterResult Validation = Impl->ValidateDescriptor(Descriptor);
    if (Validation != EReflectionRegisterResult::Success)
    {
        return Validation;
    }

    FOwnedModule Module(Impl->Allocator);
    if (!TryCopyModule(Impl->Allocator, Descriptor, Module))
    {
        return EReflectionRegisterResult::AllocationFailed;
    }

    // Reserve the sole commit container before issuing the externally visible
    // generation handle. Array relocation moves each module's nested arrays
    // without relocating their heap storage, and lookup pointers are in any
    // case invalidated by every successful registry mutation.
    if (!Impl->Modules.TryReserve(Impl->Modules.Size() + 1))
    {
        return EReflectionRegisterResult::AllocationFailed;
    }

    FReflectionModuleHandle Handle;
    if (!Impl->Handles.TryAllocate(Handle))
    {
        return EReflectionRegisterResult::AllocationFailed;
    }
    Module.Handle = Handle;
    Impl->Modules.PushBack(std::move(Module));

    OutHandle = Handle;
    return EReflectionRegisterResult::Success;
}

EReflectionUnregisterResult FReflectionRegistry::UnregisterModule(
    const FReflectionModuleHandle Handle) noexcept
{
    if (Impl->FindModule(Handle) == nullptr)
    {
        return EReflectionUnregisterResult::NotFound;
    }
    for (std::size_t Index = 0; Index < Impl->Modules.Size(); ++Index)
    {
        if (Impl->Modules[Index].Handle == Handle)
        {
            Impl->Modules.Erase(Index);
            static_cast<void>(Impl->Handles.Release(Handle));
            return EReflectionUnregisterResult::Success;
        }
    }
    return EReflectionUnregisterResult::NotFound;
}

const FTypeInfo* FReflectionRegistry::FindType(const FTypeId Id) const noexcept
{
    for (const FOwnedModule& Module : Impl->Modules)
    {
        for (const FOwnedType& Type : Module.Types)
        {
            if (Type.Id == Id)
            {
                return &const_cast<FOwnedType&>(Type).RefreshInfo(Module.Name, Module.Handle);
            }
        }
    }
    return nullptr;
}

const FTypeInfo* FReflectionRegistry::FindType(const StringView Name) const noexcept
{
    for (const FOwnedModule& Module : Impl->Modules)
    {
        for (const FOwnedType& Type : Module.Types)
        {
            if (Type.Name == Name)
            {
                return &const_cast<FOwnedType&>(Type).RefreshInfo(Module.Name, Module.Handle);
            }
        }
    }
    return nullptr;
}

const FEnumInfo* FReflectionRegistry::FindEnum(const FTypeId Id) const noexcept
{
    for (const FOwnedModule& Module : Impl->Modules)
    {
        for (const FOwnedEnum& Enum : Module.Enums)
        {
            if (Enum.Id == Id)
            {
                return &const_cast<FOwnedEnum&>(Enum).RefreshInfo(Module.Name, Module.Handle);
            }
        }
    }
    return nullptr;
}

const FEnumInfo* FReflectionRegistry::FindEnum(const StringView Name) const noexcept
{
    for (const FOwnedModule& Module : Impl->Modules)
    {
        for (const FOwnedEnum& Enum : Module.Enums)
        {
            if (Enum.Name == Name)
            {
                return &const_cast<FOwnedEnum&>(Enum).RefreshInfo(Module.Name, Module.Handle);
            }
        }
    }
    return nullptr;
}

std::size_t FReflectionRegistry::GetModuleCount() const noexcept
{
    return Impl->Modules.Size();
}

std::size_t FReflectionRegistry::GetTypeCount() const noexcept
{
    std::size_t Count = 0;
    for (const FOwnedModule& Module : Impl->Modules)
    {
        Count += Module.Types.Size();
    }
    return Count;
}

std::size_t FReflectionRegistry::GetEnumCount() const noexcept
{
    std::size_t Count = 0;
    for (const FOwnedModule& Module : Impl->Modules)
    {
        Count += Module.Enums.Size();
    }
    return Count;
}

} // namespace LE
