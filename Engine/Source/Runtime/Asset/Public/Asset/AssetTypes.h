#pragma once

#include "Containers/HashMap.h"
#include "Containers/StringView.h"
#include "Types/RuntimeHandle.h"
#include "Types/Uuid.h"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace LE
{

struct FAssetIdTag;
struct FAssetTypeIdTag;
struct FAssetSlotTag;

// FAssetId is persistent content identity. It is not a loaded-object handle and
// never contains registry-local state.
using FAssetId = TStableId<FAssetIdTag>;

// FAssetTypeId identifies one logical asset kind without coupling Asset to the
// reflected native C++ type registry.
using FAssetTypeId = TStableId<FAssetTypeIdTag>;

using FAssetSlotHandle = TRuntimeHandle<FAssetSlotTag>;

class FAssetRegistry;

enum class EAssetOwnership : uint8
{
    Borrowed = 0,
    OwnedByProviderThunk,
};

using FAssetDestroyThunk = void (*)(void* ProviderContext, void* Object) noexcept;

struct FAssetInfo
{
    FAssetId Id;
    FAssetTypeId TypeId;
    StringView SoftPath;
    StringView DisplayName;
    StringView ProviderName;
    EAssetOwnership Ownership = EAssetOwnership::Borrowed;
};

enum class EAssetRegisterResult : uint8
{
    Success = 0,
    InvalidRegistry,
    InvalidState,
    InvalidAssetId,
    InvalidTypeId,
    InvalidPath,
    InvalidMetadata,
    NullObject,
    InvalidOwnership,
    DuplicateAssetId,
    DuplicatePath,
    DuplicateObject,
    AllocationFailed,
};

enum class EAssetFindResult : uint8
{
    Success = 0,
    InvalidRegistry,
    InvalidState,
    InvalidAssetId,
    InvalidPath,
    NotFound,
    TypeMismatch,
    AllocationFailed,
};

enum class EAssetResolveResult : uint8
{
    Success = 0,
    InvalidRegistry,
    InvalidState,
    InvalidHandle,
    ForeignRegistry,
    StaleHandle,
    TypeMismatch,
};

enum class EAssetUnregisterResult : uint8
{
    Success = 0,
    InvalidRegistry,
    InvalidState,
    InvalidHandle,
    ForeignRegistry,
    StaleHandle,
    TypeMismatch,
};

enum class EAssetReplaceResult : uint8
{
    Success = 0,
    InvalidRegistry,
    InvalidState,
    InvalidHandle,
    ForeignRegistry,
    StaleHandle,
    TypeMismatch,
    InvalidAssetId,
    InvalidTypeId,
    InvalidPath,
    InvalidMetadata,
    NullObject,
    InvalidOwnership,
    IdentityMismatch,
    DuplicatePath,
    DuplicateObject,
    GenerationExhausted,
    AllocationFailed,
};

template <typename AssetType> struct TAssetRegistration
{
    FAssetId Id;
    StringView SoftPath;
    StringView DisplayName;
    StringView ProviderName;
    AssetType* Object = nullptr;
    EAssetOwnership Ownership = EAssetOwnership::Borrowed;
    FAssetDestroyThunk Destroy = nullptr;
    void* ProviderContext = nullptr;
};

// Providers explicitly specialize this trait with a stable FAssetTypeId. The
// identity must be based on stable value bytes, never a DLL-local address.
template <typename AssetType> struct TAssetTypeTraits;

namespace Detail
{
template <typename...> using AssetVoidT = void;

template <typename> struct TMissingAssetTypeTraits : std::false_type
{
};

template <typename AssetType, typename = void> struct TAssetTypeIdAccessor
{
    static FAssetTypeId Get() noexcept
    {
        static_assert(TMissingAssetTypeTraits<AssetType>::value, "TAssetTypeTraits<AssetType> must provide static FAssetTypeId GetTypeId() noexcept");
        return {};
    }
};

template <typename AssetType> struct TAssetTypeIdAccessor<AssetType, AssetVoidT<decltype(TAssetTypeTraits<AssetType>::GetTypeId())>>
{
    static FAssetTypeId Get() noexcept
    {
        static_assert(std::is_same<decltype(TAssetTypeTraits<AssetType>::GetTypeId()), FAssetTypeId>::value, "TAssetTypeTraits<AssetType>::GetTypeId() must return FAssetTypeId");
        static_assert(noexcept(TAssetTypeTraits<AssetType>::GetTypeId()), "TAssetTypeTraits<AssetType>::GetTypeId() must be noexcept");
        return TAssetTypeTraits<AssetType>::GetTypeId();
    }
};
} // namespace Detail

template <typename AssetType> FAssetTypeId GetAssetTypeId() noexcept
{
    return Detail::TAssetTypeIdAccessor<AssetType>::Get();
}

// A typed, non-owning identity for one object loaded in one FAssetRegistry.
// Persistent IDs and logical type IDs deliberately remain in registry entries.
template <typename AssetType> class TAssetHandle
{
public:
    constexpr TAssetHandle() noexcept = default;

    bool IsValid() const noexcept { return RegistryDomain != 0 && Slot.IsValid(); }

    uint32 GetIndex() const noexcept { return Slot.Index; }
    uint32 GetGeneration() const noexcept { return Slot.Generation; }

    void Reset() noexcept
    {
        Slot.Reset();
        RegistryDomain = 0;
    }

    friend bool operator==(const TAssetHandle& Left, const TAssetHandle& Right) noexcept { return Left.Slot == Right.Slot && Left.RegistryDomain == Right.RegistryDomain; }

    friend bool operator!=(const TAssetHandle& Left, const TAssetHandle& Right) noexcept { return !(Left == Right); }

private:
    friend class FAssetRegistry;
    template <typename, typename> friend struct DefaultHash;

    constexpr TAssetHandle(const FAssetSlotHandle InSlot, const uint64 InRegistryDomain) noexcept : Slot(InSlot), RegistryDomain(InRegistryDomain) {}

    FAssetSlotHandle Slot;
    uint64 RegistryDomain = 0;
};

template <typename AssetType> struct DefaultHash<TAssetHandle<AssetType>>
{
    std::size_t operator()(const TAssetHandle<AssetType>& Handle) const noexcept
    {
        const std::size_t SlotHash = DefaultHash<FAssetSlotHandle>{}(Handle.Slot);
        const std::size_t DomainHash = DefaultHash<uint64>{}(Handle.RegistryDomain);
        return SlotHash ^ (DomainHash + static_cast<std::size_t>(0x9e3779b9U) + (SlotHash << 6) + (SlotHash >> 2));
    }
};

static_assert(sizeof(TAssetHandle<void>) == sizeof(FAssetSlotHandle) + sizeof(uint64), "TAssetHandle must remain a compact slot plus registry-domain value");
static_assert(std::is_trivially_copyable<TAssetHandle<void>>::value, "TAssetHandle must remain trivially copyable");

} // namespace LE
