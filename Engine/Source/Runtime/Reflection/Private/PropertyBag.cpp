#include "Reflection/PropertyBag.h"

#include "Containers/Array.h"

#include <new>
#include <utility>

namespace LE
{
namespace
{

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

struct FPropertyEntry
{
    FPropertyEntry(FPropertyId InId, FPropertyValue&& InValue) noexcept
        : Id(InId)
        , Value(std::move(InValue))
    {
    }

    FPropertyEntry(const FPropertyEntry&) = delete;
    FPropertyEntry& operator=(const FPropertyEntry&) = delete;
    FPropertyEntry(FPropertyEntry&&) noexcept = default;
    FPropertyEntry& operator=(FPropertyEntry&&) noexcept = default;

    FPropertyId Id;
    FPropertyValue Value;
};

} // namespace

struct FPropertyBag::FImpl
{
    explicit FImpl(IAllocator& Allocator) noexcept
        : Fields(Allocator)
    {
    }

    FTypeId SchemaTypeId;
    uint32 SchemaVersion = 0;
    Array<FPropertyEntry> Fields;
};

FPropertyBag::FPropertyBag() noexcept
    : Allocator(&GetDefaultAllocator())
{
}

FPropertyBag::FPropertyBag(IAllocator& InAllocator) noexcept
    : Allocator(&InAllocator)
{
}

FPropertyBag::~FPropertyBag() noexcept
{
    Delete(*Allocator, Impl);
}

FPropertyBag::FPropertyBag(FPropertyBag&& Other) noexcept
    : Impl(Other.Impl)
    , Allocator(Other.Allocator)
{
    Other.Impl = nullptr;
}

FPropertyBag& FPropertyBag::operator=(FPropertyBag&& Other) noexcept
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

bool FPropertyBag::IsValid() const noexcept
{
    return Impl != nullptr && Impl->SchemaTypeId.IsValid() && Impl->SchemaVersion != 0;
}

FTypeId FPropertyBag::GetSchemaTypeId() const noexcept
{
    return Impl == nullptr ? FTypeId{} : Impl->SchemaTypeId;
}

uint32 FPropertyBag::GetSchemaVersion() const noexcept
{
    return Impl == nullptr ? 0 : Impl->SchemaVersion;
}

std::size_t FPropertyBag::GetFieldCount() const noexcept
{
    return Impl == nullptr ? 0 : Impl->Fields.Size();
}

IAllocator& FPropertyBag::GetAllocator() const noexcept
{
    return *Allocator;
}

bool FPropertyBag::TryInitialize(
    const FTypeId SchemaTypeId,
    const uint32 SchemaVersion) noexcept
{
    if (!SchemaTypeId.IsValid() || SchemaVersion == 0)
    {
        return false;
    }
    FImpl* Candidate = TryNew<FImpl>(*Allocator, *Allocator);
    if (Candidate == nullptr)
    {
        return false;
    }
    Candidate->SchemaTypeId = SchemaTypeId;
    Candidate->SchemaVersion = SchemaVersion;
    Delete(*Allocator, Impl);
    Impl = Candidate;
    return true;
}

void FPropertyBag::Reset() noexcept
{
    Delete(*Allocator, Impl);
}

EPropertyBagMutationResult FPropertyBag::TryInsert(
    const FPropertyId PropertyId,
    FPropertyValue&& Value) noexcept
{
    if (!IsValid()) return EPropertyBagMutationResult::InvalidBag;
    if (!PropertyId.IsValid()) return EPropertyBagMutationResult::InvalidPropertyId;
    if (!Value.IsValid()) return EPropertyBagMutationResult::InvalidValue;

    std::size_t Position = 0;
    while (Position < Impl->Fields.Size() && Impl->Fields[Position].Id < PropertyId)
    {
        ++Position;
    }
    if (Position < Impl->Fields.Size() && Impl->Fields[Position].Id == PropertyId)
    {
        return EPropertyBagMutationResult::DuplicatePropertyId;
    }
    FPropertyValue Normalized(*Allocator);
    FPropertyValue* ValueToInsert = &Value;
    if (&Value.GetAllocator() != Allocator)
    {
        if (!Value.TryClone(Normalized))
        {
            return EPropertyBagMutationResult::AllocationFailed;
        }
        ValueToInsert = &Normalized;
    }

    // Reserve before consuming either rvalue. A failed insertion therefore
    // leaves both the bag and the caller's value logically unchanged.
    if (Impl->Fields.Size() == Array<FPropertyEntry>::MaxSize() ||
        !Impl->Fields.TryReserve(Impl->Fields.Size() + 1))
    {
        return EPropertyBagMutationResult::AllocationFailed;
    }
    FPropertyEntry Entry(PropertyId, std::move(*ValueToInsert));
    const bool bInserted = Impl->Fields.TryInsert(Position, std::move(Entry));
    if (!bInserted)
    {
        HandleContractViolation("reserved property bag insertion failed", __FILE__, __LINE__);
    }
    Value.Reset();
    return EPropertyBagMutationResult::Success;
}

EPropertyBagMutationResult FPropertyBag::TrySet(
    const FPropertyId PropertyId,
    FPropertyValue&& Value) noexcept
{
    if (!IsValid()) return EPropertyBagMutationResult::InvalidBag;
    if (!PropertyId.IsValid()) return EPropertyBagMutationResult::InvalidPropertyId;
    if (!Value.IsValid()) return EPropertyBagMutationResult::InvalidValue;
    for (FPropertyEntry& Entry : Impl->Fields)
    {
        if (Entry.Id == PropertyId)
        {
            if (&Value.GetAllocator() == Allocator)
            {
                Entry.Value = std::move(Value);
            }
            else
            {
                FPropertyValue Normalized(*Allocator);
                if (!Value.TryClone(Normalized))
                {
                    return EPropertyBagMutationResult::AllocationFailed;
                }
                Entry.Value = std::move(Normalized);
                Value.Reset();
            }
            return EPropertyBagMutationResult::Success;
        }
    }
    return TryInsert(PropertyId, std::move(Value));
}

EPropertyBagMutationResult FPropertyBag::Erase(const FPropertyId PropertyId) noexcept
{
    if (!IsValid()) return EPropertyBagMutationResult::InvalidBag;
    if (!PropertyId.IsValid()) return EPropertyBagMutationResult::InvalidPropertyId;
    for (std::size_t Index = 0; Index < Impl->Fields.Size(); ++Index)
    {
        if (Impl->Fields[Index].Id == PropertyId)
        {
            Impl->Fields.Erase(Index);
            return EPropertyBagMutationResult::Success;
        }
    }
    return EPropertyBagMutationResult::NotFound;
}

const FPropertyValue* FPropertyBag::Find(const FPropertyId PropertyId) const noexcept
{
    if (Impl == nullptr) return nullptr;
    for (const FPropertyEntry& Entry : Impl->Fields)
    {
        if (Entry.Id == PropertyId) return &Entry.Value;
    }
    return nullptr;
}

FPropertyValue* FPropertyBag::Find(const FPropertyId PropertyId) noexcept
{
    return const_cast<FPropertyValue*>(static_cast<const FPropertyBag*>(this)->Find(PropertyId));
}

FPropertyId FPropertyBag::GetPropertyIdAt(const std::size_t Index) const noexcept
{
    const FPropertyEntry* const Entry = Impl == nullptr ? nullptr : Impl->Fields.TryGet(Index);
    return Entry == nullptr ? FPropertyId{} : Entry->Id;
}

const FPropertyValue* FPropertyBag::GetValueAt(const std::size_t Index) const noexcept
{
    const FPropertyEntry* const Entry = Impl == nullptr ? nullptr : Impl->Fields.TryGet(Index);
    return Entry == nullptr ? nullptr : &Entry->Value;
}

bool FPropertyBag::TryClone(FPropertyBag& OutBag) const noexcept
{
    if (!IsValid())
    {
        return false;
    }
    FPropertyBag Candidate(OutBag.GetAllocator());
    if (!Candidate.TryInitialize(GetSchemaTypeId(), GetSchemaVersion()))
    {
        return false;
    }
    for (const FPropertyEntry& Entry : Impl->Fields)
    {
        FPropertyValue Value(Candidate.GetAllocator());
        if (!Entry.Value.TryClone(Value) ||
            Candidate.TryInsert(Entry.Id, std::move(Value)) != EPropertyBagMutationResult::Success)
        {
            return false;
        }
    }
    OutBag = std::move(Candidate);
    return true;
}

} // namespace LE
