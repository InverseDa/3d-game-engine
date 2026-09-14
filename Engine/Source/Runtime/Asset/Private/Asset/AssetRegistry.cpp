#include "Asset/AssetRegistry.h"

#include "Containers/Array.h"
#include "Containers/HashMap.h"
#include "Containers/String.h"
#include "Memory/Allocator.h"

#include <atomic>
#include <limits>
#include <new>
#include <utility>

namespace LE
{
namespace
{

std::atomic<uint64> GNextAssetRegistryDomain{1};

uint64 AcquireAssetRegistryDomain() noexcept
{
    uint64 Candidate = GNextAssetRegistryDomain.load(std::memory_order_relaxed);
    while (Candidate != 0)
    {
        const uint64 Next = Candidate == (std::numeric_limits<uint64>::max)()
            ? uint64{0}
            : Candidate + 1;
        if (GNextAssetRegistryDomain.compare_exchange_weak(
                Candidate,
                Next,
                std::memory_order_relaxed,
                std::memory_order_relaxed))
        {
            return Candidate;
        }
    }
    return 0;
}

bool IsValidMetadata(const StringView Value, const bool bAllowEmpty) noexcept
{
    if (!bAllowEmpty && Value.IsEmpty())
    {
        return false;
    }
    for (std::size_t Index = 0; Index < Value.Size(); ++Index)
    {
        if (Value[Index] == '\0')
        {
            return false;
        }
    }
    return true;
}

bool IsValidOwnership(
    const EAssetOwnership Ownership,
    const FAssetDestroyThunk Destroy) noexcept
{
    if (Ownership == EAssetOwnership::Borrowed)
    {
        return Destroy == nullptr;
    }
    if (Ownership == EAssetOwnership::OwnedByProviderThunk)
    {
        return Destroy != nullptr;
    }
    return false;
}

template <typename T>
bool TryReserveOneMore(Array<T>& Values) noexcept
{
    if (Values.Size() < Values.Capacity())
    {
        return true;
    }
    if (Values.Size() == Array<T>::MaxSize())
    {
        return false;
    }

    const std::size_t Required = Values.Size() + 1;
    std::size_t Capacity = Values.Capacity() == 0 ? 4 : Values.Capacity();
    while (Capacity < Required)
    {
        const std::size_t Growth = Capacity / 2 + 1;
        if (Capacity > Array<T>::MaxSize() - Growth)
        {
            Capacity = Required;
            break;
        }
        Capacity += Growth;
    }
    return Values.TryReserve(Capacity);
}

struct FOwnedAssetEntry
{
    explicit FOwnedAssetEntry(IAllocator& Allocator) noexcept
        : SoftPath(Allocator)
        , DisplayName(Allocator)
        , ProviderName(Allocator)
    {
    }

    FOwnedAssetEntry(const FOwnedAssetEntry&) = delete;
    FOwnedAssetEntry& operator=(const FOwnedAssetEntry&) = delete;
    FOwnedAssetEntry(FOwnedAssetEntry&& Other) noexcept
        : Id(Other.Id)
        , TypeId(Other.TypeId)
        , SoftPath(std::move(Other.SoftPath))
        , DisplayName(std::move(Other.DisplayName))
        , ProviderName(std::move(Other.ProviderName))
        , Object(Other.Object)
        , Ownership(Other.Ownership)
        , Destroy(Other.Destroy)
        , ProviderContext(Other.ProviderContext)
    {
        Other.Id = {};
        Other.TypeId = {};
        Other.Object = nullptr;
        Other.Ownership = EAssetOwnership::Borrowed;
        Other.Destroy = nullptr;
        Other.ProviderContext = nullptr;
    }

    FOwnedAssetEntry& operator=(FOwnedAssetEntry&& Other) noexcept
    {
        if (this != &Other)
        {
            Id = Other.Id;
            TypeId = Other.TypeId;
            SoftPath = std::move(Other.SoftPath);
            DisplayName = std::move(Other.DisplayName);
            ProviderName = std::move(Other.ProviderName);
            Object = Other.Object;
            Ownership = Other.Ownership;
            Destroy = Other.Destroy;
            ProviderContext = Other.ProviderContext;

            Other.Id = {};
            Other.TypeId = {};
            Other.Object = nullptr;
            Other.Ownership = EAssetOwnership::Borrowed;
            Other.Destroy = nullptr;
            Other.ProviderContext = nullptr;
        }
        return *this;
    }

    FAssetId Id;
    FAssetTypeId TypeId;
    String SoftPath;
    String DisplayName;
    String ProviderName;
    void* Object = nullptr;
    EAssetOwnership Ownership = EAssetOwnership::Borrowed;
    FAssetDestroyThunk Destroy = nullptr;
    void* ProviderContext = nullptr;
};

struct FAssetSlot
{
    explicit FAssetSlot(IAllocator& Allocator) noexcept : Entry(Allocator) {}

    FAssetSlot(const FAssetSlot&) = delete;
    FAssetSlot& operator=(const FAssetSlot&) = delete;
    FAssetSlot(FAssetSlot&&) noexcept = default;
    FAssetSlot& operator=(FAssetSlot&&) noexcept = default;

    FOwnedAssetEntry Entry;
    uint32 Generation = 0;
    bool bAlive = false;
    bool bRetired = false;
};

static_assert(std::is_nothrow_move_constructible<FAssetSlot>::value &&
    std::is_nothrow_move_assignable<FAssetSlot>::value,
    "asset slots must relocate without introducing a fallible commit step");

enum class ESlotValidation : uint8
{
    Success,
    InvalidHandle,
    ForeignRegistry,
    StaleHandle,
    TypeMismatch,
};

} // namespace

struct FAssetRegistry::FImpl
{
    explicit FImpl(IAllocator& InAllocator, const uint64 InDomain) noexcept
        : Allocator(InAllocator)
        , Domain(InDomain)
        , Slots(InAllocator)
        , FreeIndices(InAllocator)
        , RegistrationOrder(InAllocator)
        , IdToSlot(InAllocator)
        , PathToSlot(InAllocator)
        , ObjectToSlot(InAllocator)
    {
    }

    ESlotValidation Validate(
        const FAssetSlotHandle Handle,
        const uint64 HandleDomain,
        const FAssetTypeId ExpectedType,
        const FAssetSlot*& OutSlot) const noexcept
    {
        if (!Handle.IsValid() || HandleDomain == 0)
        {
            return ESlotValidation::InvalidHandle;
        }
        if (HandleDomain != Domain)
        {
            return ESlotValidation::ForeignRegistry;
        }
        if (static_cast<std::size_t>(Handle.Index) >= Slots.Size())
        {
            return ESlotValidation::StaleHandle;
        }

        const FAssetSlot& Slot = Slots[static_cast<std::size_t>(Handle.Index)];
        if (!Slot.bAlive || Slot.bRetired || Slot.Generation != Handle.Generation)
        {
            return ESlotValidation::StaleHandle;
        }
        if (Slot.Entry.TypeId != ExpectedType)
        {
            return ESlotValidation::TypeMismatch;
        }
        OutSlot = &Slot;
        return ESlotValidation::Success;
    }

    ESlotValidation Validate(
        const FAssetSlotHandle Handle,
        const uint64 HandleDomain,
        const FAssetTypeId ExpectedType,
        FAssetSlot*& OutSlot) noexcept
    {
        const FAssetSlot* ConstSlot = nullptr;
        const ESlotValidation Result = static_cast<const FImpl&>(*this).Validate(
            Handle,
            HandleDomain,
            ExpectedType,
            ConstSlot);
        OutSlot = const_cast<FAssetSlot*>(ConstSlot);
        return Result;
    }

    void DestroyEntry(FOwnedAssetEntry& Entry) noexcept
    {
        void* const Object = Entry.Object;
        const FAssetDestroyThunk Destroy = Entry.Destroy;
        void* const ProviderContext = Entry.ProviderContext;
        Entry.Object = nullptr;
        Entry.Destroy = nullptr;
        Entry.ProviderContext = nullptr;
        if (Entry.Ownership == EAssetOwnership::OwnedByProviderThunk &&
            Object != nullptr && Destroy != nullptr)
        {
            Destroy(ProviderContext, Object);
        }
    }

    void DestroyRemainingInReverseRegistrationOrder() noexcept
    {
        bMutating = true;
        for (std::size_t Position = RegistrationOrder.Size(); Position > 0; --Position)
        {
            const FAssetSlotHandle Handle = RegistrationOrder[Position - 1];
            if (static_cast<std::size_t>(Handle.Index) >= Slots.Size())
            {
                continue;
            }
            FAssetSlot& Slot = Slots[static_cast<std::size_t>(Handle.Index)];
            if (!Slot.bAlive || Slot.Generation != Handle.Generation)
            {
                continue;
            }
            Slot.bAlive = false;
            DestroyEntry(Slot.Entry);
        }
    }

    IAllocator& Allocator;
    uint64 Domain = 0;
    Array<FAssetSlot> Slots;
    Array<uint32> FreeIndices;
    Array<FAssetSlotHandle> RegistrationOrder;
    HashMap<FAssetId, uint32> IdToSlot;
    HashMap<String, uint32> PathToSlot;
    HashMap<void*, uint32> ObjectToSlot;
    std::size_t AssetCount = 0;
    bool bMutating = false;
};

namespace
{

EAssetResolveResult ToResolveResult(const ESlotValidation Result) noexcept
{
    switch (Result)
    {
    case ESlotValidation::Success: return EAssetResolveResult::Success;
    case ESlotValidation::InvalidHandle: return EAssetResolveResult::InvalidHandle;
    case ESlotValidation::ForeignRegistry: return EAssetResolveResult::ForeignRegistry;
    case ESlotValidation::StaleHandle: return EAssetResolveResult::StaleHandle;
    case ESlotValidation::TypeMismatch: return EAssetResolveResult::TypeMismatch;
    }
    return EAssetResolveResult::InvalidHandle;
}

EAssetUnregisterResult ToUnregisterResult(const ESlotValidation Result) noexcept
{
    switch (Result)
    {
    case ESlotValidation::Success: return EAssetUnregisterResult::Success;
    case ESlotValidation::InvalidHandle: return EAssetUnregisterResult::InvalidHandle;
    case ESlotValidation::ForeignRegistry: return EAssetUnregisterResult::ForeignRegistry;
    case ESlotValidation::StaleHandle: return EAssetUnregisterResult::StaleHandle;
    case ESlotValidation::TypeMismatch: return EAssetUnregisterResult::TypeMismatch;
    }
    return EAssetUnregisterResult::InvalidHandle;
}

EAssetReplaceResult ToReplaceResult(const ESlotValidation Result) noexcept
{
    switch (Result)
    {
    case ESlotValidation::Success: return EAssetReplaceResult::Success;
    case ESlotValidation::InvalidHandle: return EAssetReplaceResult::InvalidHandle;
    case ESlotValidation::ForeignRegistry: return EAssetReplaceResult::ForeignRegistry;
    case ESlotValidation::StaleHandle: return EAssetReplaceResult::StaleHandle;
    case ESlotValidation::TypeMismatch: return EAssetReplaceResult::TypeMismatch;
    }
    return EAssetReplaceResult::InvalidHandle;
}

ESoftAssetPathResult CanonicalizePath(
    IAllocator& Allocator,
    const StringView Source,
    FSoftAssetPath& OutPath) noexcept
{
    FSoftAssetPath Candidate(Allocator);
    const ESoftAssetPathResult Result = Candidate.TryAssign(Source);
    if (Result == ESoftAssetPathResult::Success)
    {
        OutPath = std::move(Candidate);
    }
    return Result;
}

} // namespace

FAssetRegistry::FAssetRegistry() noexcept
    : FAssetRegistry(GetDefaultAllocator())
{
}

FAssetRegistry::FAssetRegistry(IAllocator& Allocator) noexcept
{
    FImpl* const Storage = TryAllocateArray<FImpl>(Allocator, 1);
    if (Storage == nullptr)
    {
        return;
    }
    const uint64 Domain = AcquireAssetRegistryDomain();
    if (Domain == 0)
    {
        DeallocateArray(Allocator, Storage, 1);
        return;
    }
    Impl = new (Storage) FImpl(Allocator, Domain);
}

FAssetRegistry::~FAssetRegistry() noexcept
{
    if (Impl == nullptr)
    {
        return;
    }
    IAllocator& Allocator = Impl->Allocator;
    Impl->DestroyRemainingInReverseRegistrationOrder();
    Impl->~FImpl();
    DeallocateArray(Allocator, Impl, 1);
    Impl = nullptr;
}

bool FAssetRegistry::IsValid() const noexcept
{
    return Impl != nullptr && Impl->Domain != 0;
}

std::size_t FAssetRegistry::GetAssetCount() const noexcept
{
    return IsValid() ? Impl->AssetCount : 0;
}

EAssetRegisterResult FAssetRegistry::RegisterErased(
    const FErasedRegistration& Registration,
    FAssetSlotHandle& OutSlot,
    uint64& OutDomain) noexcept
{
    if (!IsValid())
    {
        return EAssetRegisterResult::InvalidRegistry;
    }
    if (Impl->bMutating)
    {
        return EAssetRegisterResult::InvalidState;
    }
    if (!Registration.Id.IsValid())
    {
        return EAssetRegisterResult::InvalidAssetId;
    }
    if (!Registration.TypeId.IsValid())
    {
        return EAssetRegisterResult::InvalidTypeId;
    }
    if (Registration.Object == nullptr)
    {
        return EAssetRegisterResult::NullObject;
    }
    if (!IsValidMetadata(Registration.DisplayName, true) ||
        !IsValidMetadata(Registration.ProviderName, false))
    {
        return EAssetRegisterResult::InvalidMetadata;
    }
    if (!IsValidOwnership(Registration.Ownership, Registration.Destroy))
    {
        return EAssetRegisterResult::InvalidOwnership;
    }

    FSoftAssetPath CanonicalPath(Impl->Allocator);
    const ESoftAssetPathResult PathResult = CanonicalizePath(
        Impl->Allocator,
        Registration.SoftPath,
        CanonicalPath);
    if (PathResult == ESoftAssetPathResult::AllocationFailed)
    {
        return EAssetRegisterResult::AllocationFailed;
    }
    if (PathResult != ESoftAssetPathResult::Success)
    {
        return EAssetRegisterResult::InvalidPath;
    }
    if (Impl->IdToSlot.Contains(Registration.Id))
    {
        return EAssetRegisterResult::DuplicateAssetId;
    }
    if (Impl->PathToSlot.Contains(CanonicalPath.View()))
    {
        return EAssetRegisterResult::DuplicatePath;
    }
    if (Impl->ObjectToSlot.Contains(Registration.Object))
    {
        return EAssetRegisterResult::DuplicateObject;
    }

    FOwnedAssetEntry Staged(Impl->Allocator);
    Staged.Id = Registration.Id;
    Staged.TypeId = Registration.TypeId;
    Staged.Object = Registration.Object;
    Staged.Ownership = Registration.Ownership;
    Staged.Destroy = Registration.Destroy;
    Staged.ProviderContext = Registration.ProviderContext;
    if (!Staged.SoftPath.TryAssign(CanonicalPath.View()) ||
        !Staged.DisplayName.TryAssign(Registration.DisplayName) ||
        !Staged.ProviderName.TryAssign(Registration.ProviderName))
    {
        return EAssetRegisterResult::AllocationFailed;
    }

    String PathIndexKey(Impl->Allocator);
    if (!PathIndexKey.TryAssign(CanonicalPath.View()))
    {
        return EAssetRegisterResult::AllocationFailed;
    }

    const bool bReuseSlot = !Impl->FreeIndices.IsEmpty();
    uint32 SlotIndex = 0;
    uint32 Generation = 1;
    if (bReuseSlot)
    {
        SlotIndex = Impl->FreeIndices.Back();
        const FAssetSlot& Slot = Impl->Slots[static_cast<std::size_t>(SlotIndex)];
        Generation = Slot.Generation + 1;
    }
    else
    {
        if (Impl->Slots.Size() >= static_cast<std::size_t>(FAssetSlotHandle::InvalidIndex))
        {
            return EAssetRegisterResult::AllocationFailed;
        }
        SlotIndex = static_cast<uint32>(Impl->Slots.Size());
        if (!TryReserveOneMore(Impl->Slots))
        {
            return EAssetRegisterResult::AllocationFailed;
        }
    }

    if (!TryReserveOneMore(Impl->RegistrationOrder) ||
        !Impl->IdToSlot.TryReserve(Impl->IdToSlot.Size() + 1) ||
        !Impl->PathToSlot.TryReserve(Impl->PathToSlot.Size() + 1) ||
        !Impl->ObjectToSlot.TryReserve(Impl->ObjectToSlot.Size() + 1))
    {
        return EAssetRegisterResult::AllocationFailed;
    }

    if (Impl->IdToSlot.TryInsert(Registration.Id, SlotIndex) != EHashInsertResult::Inserted)
    {
        return EAssetRegisterResult::AllocationFailed;
    }
    if (Impl->PathToSlot.TryInsert(std::move(PathIndexKey), SlotIndex) !=
        EHashInsertResult::Inserted)
    {
        Impl->IdToSlot.Erase(Registration.Id);
        return EAssetRegisterResult::AllocationFailed;
    }
    if (Impl->ObjectToSlot.TryInsert(Registration.Object, SlotIndex) !=
        EHashInsertResult::Inserted)
    {
        Impl->PathToSlot.Erase(Staged.SoftPath.View());
        Impl->IdToSlot.Erase(Registration.Id);
        return EAssetRegisterResult::AllocationFailed;
    }

    if (!bReuseSlot && !Impl->Slots.TryEmplaceBack(Impl->Allocator))
    {
        Impl->ObjectToSlot.Erase(Registration.Object);
        Impl->PathToSlot.Erase(Staged.SoftPath.View());
        Impl->IdToSlot.Erase(Registration.Id);
        return EAssetRegisterResult::AllocationFailed;
    }

    const FAssetSlotHandle PublishedHandle{SlotIndex, Generation};
    if (!Impl->RegistrationOrder.TryPushBack(PublishedHandle))
    {
        if (!bReuseSlot)
        {
            Impl->Slots.PopBack();
        }
        Impl->ObjectToSlot.Erase(Registration.Object);
        Impl->PathToSlot.Erase(Staged.SoftPath.View());
        Impl->IdToSlot.Erase(Registration.Id);
        return EAssetRegisterResult::AllocationFailed;
    }

    FAssetSlot& Slot = Impl->Slots[static_cast<std::size_t>(SlotIndex)];
    Slot.Entry = std::move(Staged);
    Slot.Generation = Generation;
    Slot.bAlive = true;
    Slot.bRetired = false;
    if (bReuseSlot)
    {
        Impl->FreeIndices.PopBack();
    }
    ++Impl->AssetCount;

    OutSlot = PublishedHandle;
    OutDomain = Impl->Domain;
    return EAssetRegisterResult::Success;
}

EAssetFindResult FAssetRegistry::FindByIdErased(
    const FAssetId Id,
    const FAssetTypeId TypeId,
    FAssetSlotHandle& OutSlot,
    uint64& OutDomain) const noexcept
{
    if (!IsValid()) return EAssetFindResult::InvalidRegistry;
    if (Impl->bMutating) return EAssetFindResult::InvalidState;
    if (!Id.IsValid()) return EAssetFindResult::InvalidAssetId;

    const uint32* const Index = Impl->IdToSlot.Find(Id);
    if (Index == nullptr) return EAssetFindResult::NotFound;
    const FAssetSlot& Slot = Impl->Slots[static_cast<std::size_t>(*Index)];
    if (Slot.Entry.TypeId != TypeId) return EAssetFindResult::TypeMismatch;

    OutSlot = FAssetSlotHandle{*Index, Slot.Generation};
    OutDomain = Impl->Domain;
    return EAssetFindResult::Success;
}

EAssetFindResult FAssetRegistry::FindByPathErased(
    const StringView Path,
    const FAssetTypeId TypeId,
    FAssetSlotHandle& OutSlot,
    uint64& OutDomain) const noexcept
{
    if (!IsValid()) return EAssetFindResult::InvalidRegistry;
    if (Impl->bMutating) return EAssetFindResult::InvalidState;

    FSoftAssetPath CanonicalPath(Impl->Allocator);
    const ESoftAssetPathResult PathResult = CanonicalizePath(
        Impl->Allocator,
        Path,
        CanonicalPath);
    if (PathResult == ESoftAssetPathResult::AllocationFailed)
    {
        return EAssetFindResult::AllocationFailed;
    }
    if (PathResult != ESoftAssetPathResult::Success)
    {
        return EAssetFindResult::InvalidPath;
    }

    const uint32* const Index = Impl->PathToSlot.Find(CanonicalPath.View());
    if (Index == nullptr) return EAssetFindResult::NotFound;
    const FAssetSlot& Slot = Impl->Slots[static_cast<std::size_t>(*Index)];
    if (Slot.Entry.TypeId != TypeId) return EAssetFindResult::TypeMismatch;

    OutSlot = FAssetSlotHandle{*Index, Slot.Generation};
    OutDomain = Impl->Domain;
    return EAssetFindResult::Success;
}

EAssetResolveResult FAssetRegistry::ResolveErased(
    const FAssetSlotHandle SlotHandle,
    const uint64 Domain,
    const FAssetTypeId TypeId,
    const void*& OutObject) const noexcept
{
    if (!IsValid()) return EAssetResolveResult::InvalidRegistry;
    if (Impl->bMutating) return EAssetResolveResult::InvalidState;
    const FAssetSlot* Slot = nullptr;
    const EAssetResolveResult Result = ToResolveResult(
        Impl->Validate(SlotHandle, Domain, TypeId, Slot));
    if (Result == EAssetResolveResult::Success)
    {
        OutObject = Slot->Entry.Object;
    }
    return Result;
}

EAssetResolveResult FAssetRegistry::GetAssetIdErased(
    const FAssetSlotHandle SlotHandle,
    const uint64 Domain,
    const FAssetTypeId TypeId,
    FAssetId& OutId) const noexcept
{
    if (!IsValid()) return EAssetResolveResult::InvalidRegistry;
    if (Impl->bMutating) return EAssetResolveResult::InvalidState;
    const FAssetSlot* Slot = nullptr;
    const EAssetResolveResult Result = ToResolveResult(
        Impl->Validate(SlotHandle, Domain, TypeId, Slot));
    if (Result == EAssetResolveResult::Success)
    {
        OutId = Slot->Entry.Id;
    }
    return Result;
}

EAssetResolveResult FAssetRegistry::GetInfoErased(
    const FAssetSlotHandle SlotHandle,
    const uint64 Domain,
    const FAssetTypeId TypeId,
    FAssetInfo& OutInfo) const noexcept
{
    if (!IsValid()) return EAssetResolveResult::InvalidRegistry;
    if (Impl->bMutating) return EAssetResolveResult::InvalidState;
    const FAssetSlot* Slot = nullptr;
    const EAssetResolveResult Result = ToResolveResult(
        Impl->Validate(SlotHandle, Domain, TypeId, Slot));
    if (Result == EAssetResolveResult::Success)
    {
        FAssetInfo Info;
        Info.Id = Slot->Entry.Id;
        Info.TypeId = Slot->Entry.TypeId;
        Info.SoftPath = Slot->Entry.SoftPath.View();
        Info.DisplayName = Slot->Entry.DisplayName.View();
        Info.ProviderName = Slot->Entry.ProviderName.View();
        Info.Ownership = Slot->Entry.Ownership;
        OutInfo = Info;
    }
    return Result;
}

EAssetUnregisterResult FAssetRegistry::UnregisterErased(
    const FAssetSlotHandle SlotHandle,
    const uint64 Domain,
    const FAssetTypeId TypeId) noexcept
{
    if (!IsValid()) return EAssetUnregisterResult::InvalidRegistry;
    if (Impl->bMutating) return EAssetUnregisterResult::InvalidState;

    FAssetSlot* Slot = nullptr;
    const ESlotValidation Validation = Impl->Validate(
        SlotHandle,
        Domain,
        TypeId,
        Slot);
    if (Validation != ESlotValidation::Success)
    {
        return ToUnregisterResult(Validation);
    }

    Impl->bMutating = true;
    Impl->IdToSlot.Erase(Slot->Entry.Id);
    Impl->PathToSlot.Erase(Slot->Entry.SoftPath.View());
    Impl->ObjectToSlot.Erase(Slot->Entry.Object);

    FOwnedAssetEntry Removed(Impl->Allocator);
    Removed = std::move(Slot->Entry);
    Slot->bAlive = false;
    --Impl->AssetCount;
    if (Slot->Generation == (std::numeric_limits<uint32>::max)())
    {
        Slot->bRetired = true;
    }
    else if (!Impl->FreeIndices.TryPushBack(SlotHandle.Index))
    {
        Slot->bRetired = true;
    }

    Impl->DestroyEntry(Removed);
    Impl->bMutating = false;
    return EAssetUnregisterResult::Success;
}

EAssetReplaceResult FAssetRegistry::ReplaceErased(
    const FAssetSlotHandle SlotHandle,
    const uint64 Domain,
    const FErasedRegistration& Registration,
    FAssetSlotHandle& OutNewSlot,
    uint64& OutDomain) noexcept
{
    if (!IsValid()) return EAssetReplaceResult::InvalidRegistry;
    if (Impl->bMutating) return EAssetReplaceResult::InvalidState;

    // Descriptor identity validation precedes handle validation. This keeps an
    // invalid provider trait or persistent ID diagnosable even when the caller
    // also supplied stale runtime bytes.
    if (!Registration.Id.IsValid()) return EAssetReplaceResult::InvalidAssetId;
    if (!Registration.TypeId.IsValid()) return EAssetReplaceResult::InvalidTypeId;

    FAssetSlot* Slot = nullptr;
    const ESlotValidation Validation = Impl->Validate(
        SlotHandle,
        Domain,
        Registration.TypeId,
        Slot);
    if (Validation != ESlotValidation::Success)
    {
        return ToReplaceResult(Validation);
    }
    if (Registration.Id != Slot->Entry.Id || Registration.TypeId != Slot->Entry.TypeId)
    {
        return EAssetReplaceResult::IdentityMismatch;
    }
    if (Registration.Object == nullptr) return EAssetReplaceResult::NullObject;
    if (!IsValidMetadata(Registration.DisplayName, true) ||
        !IsValidMetadata(Registration.ProviderName, false))
    {
        return EAssetReplaceResult::InvalidMetadata;
    }
    if (!IsValidOwnership(Registration.Ownership, Registration.Destroy))
    {
        return EAssetReplaceResult::InvalidOwnership;
    }
    if (Slot->Generation == (std::numeric_limits<uint32>::max)())
    {
        return EAssetReplaceResult::GenerationExhausted;
    }

    FSoftAssetPath CanonicalPath(Impl->Allocator);
    const ESoftAssetPathResult PathResult = CanonicalizePath(
        Impl->Allocator,
        Registration.SoftPath,
        CanonicalPath);
    if (PathResult == ESoftAssetPathResult::AllocationFailed)
    {
        return EAssetReplaceResult::AllocationFailed;
    }
    if (PathResult != ESoftAssetPathResult::Success)
    {
        return EAssetReplaceResult::InvalidPath;
    }

    if (const uint32* const Existing = Impl->PathToSlot.Find(CanonicalPath.View());
        Existing != nullptr && *Existing != SlotHandle.Index)
    {
        return EAssetReplaceResult::DuplicatePath;
    }
    if (Impl->ObjectToSlot.Contains(Registration.Object))
    {
        return EAssetReplaceResult::DuplicateObject;
    }

    FOwnedAssetEntry Staged(Impl->Allocator);
    Staged.Id = Registration.Id;
    Staged.TypeId = Registration.TypeId;
    Staged.Object = Registration.Object;
    Staged.Ownership = Registration.Ownership;
    Staged.Destroy = Registration.Destroy;
    Staged.ProviderContext = Registration.ProviderContext;
    if (!Staged.SoftPath.TryAssign(CanonicalPath.View()) ||
        !Staged.DisplayName.TryAssign(Registration.DisplayName) ||
        !Staged.ProviderName.TryAssign(Registration.ProviderName))
    {
        return EAssetReplaceResult::AllocationFailed;
    }

    const bool bPathChanges = Staged.SoftPath != Slot->Entry.SoftPath;
    String PathIndexKey(Impl->Allocator);
    if (bPathChanges && !PathIndexKey.TryAssign(Staged.SoftPath.View()))
    {
        return EAssetReplaceResult::AllocationFailed;
    }
    if (!TryReserveOneMore(Impl->RegistrationOrder) ||
        !Impl->ObjectToSlot.TryReserve(Impl->ObjectToSlot.Size() + 1) ||
        (bPathChanges &&
            !Impl->PathToSlot.TryReserve(Impl->PathToSlot.Size() + 1)))
    {
        return EAssetReplaceResult::AllocationFailed;
    }

    if (Impl->ObjectToSlot.TryInsert(Registration.Object, SlotHandle.Index) !=
        EHashInsertResult::Inserted)
    {
        return EAssetReplaceResult::AllocationFailed;
    }
    if (bPathChanges &&
        Impl->PathToSlot.TryInsert(std::move(PathIndexKey), SlotHandle.Index) !=
            EHashInsertResult::Inserted)
    {
        Impl->ObjectToSlot.Erase(Registration.Object);
        return EAssetReplaceResult::AllocationFailed;
    }

    const FAssetSlotHandle NewHandle{SlotHandle.Index, Slot->Generation + 1};
    if (!Impl->RegistrationOrder.TryPushBack(NewHandle))
    {
        if (bPathChanges) Impl->PathToSlot.Erase(Staged.SoftPath.View());
        Impl->ObjectToSlot.Erase(Registration.Object);
        return EAssetReplaceResult::AllocationFailed;
    }

    Impl->bMutating = true;
    if (bPathChanges)
    {
        Impl->PathToSlot.Erase(Slot->Entry.SoftPath.View());
    }
    Impl->ObjectToSlot.Erase(Slot->Entry.Object);

    FOwnedAssetEntry Removed(Impl->Allocator);
    Removed = std::move(Slot->Entry);
    Slot->Entry = std::move(Staged);
    Slot->Generation = NewHandle.Generation;
    Impl->DestroyEntry(Removed);
    Impl->bMutating = false;

    OutNewSlot = NewHandle;
    OutDomain = Impl->Domain;
    return EAssetReplaceResult::Success;
}

} // namespace LE
