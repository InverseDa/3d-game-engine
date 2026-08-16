#pragma once

#include "Containers/Span.h"
#include "Containers/StringView.h"
#include "Reflection/ReflectionThunks.h"
#include "Reflection/ReflectionTypes.h"

#include <cstddef>

namespace LE
{

struct FPropertyInfo
{
    FPropertyId Id;
    StringView Name;
    FTypeId ValueTypeId;
    FReflectionPropertyGetterThunk Getter = nullptr;
    FReflectionPropertySetterThunk Setter = nullptr;

    bool IsReadOnly() const noexcept { return Setter == nullptr; }
};

struct FTypeInfo
{
    FTypeId Id;
    StringView Name;
    StringView ModuleName;
    FReflectionModuleHandle ModuleHandle;
    EReflectedTypeKind Kind = EReflectedTypeKind::Struct;
    std::size_t Size = 0;
    std::size_t Alignment = 0;
    Span<const FPropertyInfo> Properties;
    FReflectionConstructThunk Construct = nullptr;
    FReflectionDestructThunk Destruct = nullptr;

    const FPropertyInfo* FindProperty(FPropertyId PropertyId) const noexcept;
    const FPropertyInfo* FindProperty(StringView PropertyName) const noexcept;
};

struct FEnumValueInfo
{
    StringView Name;
    uint64 ValueBits = 0;
};

struct FEnumInfo
{
    FTypeId Id;
    StringView Name;
    StringView ModuleName;
    FReflectionModuleHandle ModuleHandle;
    EEnumUnderlyingType UnderlyingType = EEnumUnderlyingType::Int32;
    Span<const FEnumValueInfo> Values;

    const FEnumValueInfo* FindValue(StringView ValueName) const noexcept;
    const FEnumValueInfo* FindValueByBits(uint64 ValueBits) const noexcept;
};

struct FPropertyDescriptor
{
    FPropertyId Id;
    StringView Name;
    FTypeId ValueTypeId;
    FReflectionPropertyGetterThunk Getter = nullptr;
    FReflectionPropertySetterThunk Setter = nullptr;
};

struct FTypeDescriptor
{
    FTypeId Id;
    StringView Name;
    EReflectedTypeKind Kind = EReflectedTypeKind::Struct;
    std::size_t Size = 0;
    std::size_t Alignment = 0;
    Span<const FPropertyDescriptor> Properties;
    FReflectionConstructThunk Construct = nullptr;
    FReflectionDestructThunk Destruct = nullptr;
};

struct FEnumValueDescriptor
{
    StringView Name;
    uint64 ValueBits = 0;
};

struct FEnumDescriptor
{
    FTypeId Id;
    StringView Name;
    EEnumUnderlyingType UnderlyingType = EEnumUnderlyingType::Int32;
    Span<const FEnumValueDescriptor> Values;
};

struct FReflectionModuleDescriptor
{
    StringView Name;
    Span<const FTypeDescriptor> Types;
    Span<const FEnumDescriptor> Enums;
};

inline const FPropertyInfo* FTypeInfo::FindProperty(const FPropertyId PropertyId) const noexcept
{
    for (const FPropertyInfo& Property : Properties)
    {
        if (Property.Id == PropertyId)
        {
            return &Property;
        }
    }
    return nullptr;
}

inline const FPropertyInfo* FTypeInfo::FindProperty(const StringView PropertyName) const noexcept
{
    for (const FPropertyInfo& Property : Properties)
    {
        if (Property.Name == PropertyName)
        {
            return &Property;
        }
    }
    return nullptr;
}

inline const FEnumValueInfo* FEnumInfo::FindValue(const StringView ValueName) const noexcept
{
    for (const FEnumValueInfo& Value : Values)
    {
        if (Value.Name == ValueName)
        {
            return &Value;
        }
    }
    return nullptr;
}

inline const FEnumValueInfo* FEnumInfo::FindValueByBits(const uint64 ValueBits) const noexcept
{
    if (!IsCanonicalEnumValueBits(UnderlyingType, ValueBits))
    {
        return nullptr;
    }
    for (const FEnumValueInfo& Value : Values)
    {
        if (Value.ValueBits == ValueBits)
        {
            return &Value;
        }
    }
    return nullptr;
}

} // namespace LE
