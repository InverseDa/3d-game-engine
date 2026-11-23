#pragma once

#include "Asset/AssetTypes.h"
#include "Asset/SoftAssetPath.h"
#include "Memory/Allocator.h"

#include <cstddef>

namespace LE
{

// Registry access is intentionally single-threaded and non-reentrant. Metadata
// views returned through FAssetInfo and object pointers returned by Resolve are
// borrowed. Conservatively treat every successful Register, Replace,
// Unregister, or registry destruction as invalidating all such values.
class ASSET_API FAssetRegistry final
{
private:
    struct FErasedRegistration
    {
        FAssetId Id;
        FAssetTypeId TypeId;
        StringView SoftPath;
        StringView DisplayName;
        StringView ProviderName;
        void* Object = nullptr;
        EAssetOwnership Ownership = EAssetOwnership::Borrowed;
        FAssetDestroyThunk Destroy = nullptr;
        void* ProviderContext = nullptr;
    };

public:
    FAssetRegistry() noexcept;
    explicit FAssetRegistry(IAllocator& Allocator) noexcept;
    ~FAssetRegistry() noexcept;

    FAssetRegistry(const FAssetRegistry&) = delete;
    FAssetRegistry& operator=(const FAssetRegistry&) = delete;
    FAssetRegistry(FAssetRegistry&&) = delete;
    FAssetRegistry& operator=(FAssetRegistry&&) = delete;

    bool IsValid() const noexcept;
    std::size_t GetAssetCount() const noexcept;

    template <typename AssetType> EAssetRegisterResult Register(const TAssetRegistration<AssetType>& Registration, TAssetHandle<AssetType>& OutHandle) noexcept
    {
        FErasedRegistration Erased;
        Erased.Id = Registration.Id;
        Erased.TypeId = GetAssetTypeId<AssetType>();
        Erased.SoftPath = Registration.SoftPath;
        Erased.DisplayName = Registration.DisplayName;
        Erased.ProviderName = Registration.ProviderName;
        Erased.Object = Registration.Object;
        Erased.Ownership = Registration.Ownership;
        Erased.Destroy = Registration.Destroy;
        Erased.ProviderContext = Registration.ProviderContext;

        FAssetSlotHandle Slot;
        uint64 Domain = 0;
        const EAssetRegisterResult Result = RegisterErased(Erased, Slot, Domain);
        if (Result == EAssetRegisterResult::Success)
        {
            OutHandle = TAssetHandle<AssetType>(Slot, Domain);
        }
        return Result;
    }

    template <typename AssetType> EAssetFindResult TryFindById(const FAssetId Id, TAssetHandle<AssetType>& OutHandle) const noexcept
    {
        FAssetSlotHandle Slot;
        uint64 Domain = 0;
        const EAssetFindResult Result = FindByIdErased(Id, GetAssetTypeId<AssetType>(), Slot, Domain);
        if (Result == EAssetFindResult::Success)
        {
            OutHandle = TAssetHandle<AssetType>(Slot, Domain);
        }
        return Result;
    }

    template <typename AssetType> EAssetFindResult TryFindByPath(const StringView Path, TAssetHandle<AssetType>& OutHandle) const noexcept
    {
        FAssetSlotHandle Slot;
        uint64 Domain = 0;
        const EAssetFindResult Result = FindByPathErased(Path, GetAssetTypeId<AssetType>(), Slot, Domain);
        if (Result == EAssetFindResult::Success)
        {
            OutHandle = TAssetHandle<AssetType>(Slot, Domain);
        }
        return Result;
    }

    template <typename AssetType> EAssetResolveResult Resolve(const TAssetHandle<AssetType> Handle, const AssetType*& OutObject) const noexcept
    {
        const void* Object = nullptr;
        const EAssetResolveResult Result = ResolveErased(Handle.Slot, Handle.RegistryDomain, GetAssetTypeId<AssetType>(), Object);
        if (Result == EAssetResolveResult::Success)
        {
            OutObject = static_cast<const AssetType*>(Object);
        }
        return Result;
    }

    template <typename AssetType> EAssetResolveResult TryGetAssetId(const TAssetHandle<AssetType> Handle, FAssetId& OutId) const noexcept { return GetAssetIdErased(Handle.Slot, Handle.RegistryDomain, GetAssetTypeId<AssetType>(), OutId); }

    template <typename AssetType> EAssetResolveResult TryGetInfo(const TAssetHandle<AssetType> Handle, FAssetInfo& OutInfo) const noexcept { return GetInfoErased(Handle.Slot, Handle.RegistryDomain, GetAssetTypeId<AssetType>(), OutInfo); }

    template <typename AssetType> EAssetUnregisterResult Unregister(const TAssetHandle<AssetType> Handle) noexcept { return UnregisterErased(Handle.Slot, Handle.RegistryDomain, GetAssetTypeId<AssetType>()); }

    template <typename AssetType> EAssetReplaceResult Replace(const TAssetHandle<AssetType> Handle, const TAssetRegistration<AssetType>& Registration, TAssetHandle<AssetType>& OutNewHandle) noexcept
    {
        FErasedRegistration Erased;
        Erased.Id = Registration.Id;
        Erased.TypeId = GetAssetTypeId<AssetType>();
        Erased.SoftPath = Registration.SoftPath;
        Erased.DisplayName = Registration.DisplayName;
        Erased.ProviderName = Registration.ProviderName;
        Erased.Object = Registration.Object;
        Erased.Ownership = Registration.Ownership;
        Erased.Destroy = Registration.Destroy;
        Erased.ProviderContext = Registration.ProviderContext;

        FAssetSlotHandle NewSlot;
        uint64 Domain = 0;
        const EAssetReplaceResult Result = ReplaceErased(Handle.Slot, Handle.RegistryDomain, Erased, NewSlot, Domain);
        if (Result == EAssetReplaceResult::Success)
        {
            OutNewHandle = TAssetHandle<AssetType>(NewSlot, Domain);
        }
        return Result;
    }

private:
    EAssetRegisterResult RegisterErased(const FErasedRegistration& Registration, FAssetSlotHandle& OutSlot, uint64& OutDomain) noexcept;
    EAssetFindResult FindByIdErased(FAssetId Id, FAssetTypeId TypeId, FAssetSlotHandle& OutSlot, uint64& OutDomain) const noexcept;
    EAssetFindResult FindByPathErased(StringView Path, FAssetTypeId TypeId, FAssetSlotHandle& OutSlot, uint64& OutDomain) const noexcept;
    EAssetResolveResult ResolveErased(FAssetSlotHandle Slot, uint64 Domain, FAssetTypeId TypeId, const void*& OutObject) const noexcept;
    EAssetResolveResult GetAssetIdErased(FAssetSlotHandle Slot, uint64 Domain, FAssetTypeId TypeId, FAssetId& OutId) const noexcept;
    EAssetResolveResult GetInfoErased(FAssetSlotHandle Slot, uint64 Domain, FAssetTypeId TypeId, FAssetInfo& OutInfo) const noexcept;
    EAssetUnregisterResult UnregisterErased(FAssetSlotHandle Slot, uint64 Domain, FAssetTypeId TypeId) noexcept;
    EAssetReplaceResult ReplaceErased(FAssetSlotHandle Slot, uint64 Domain, const FErasedRegistration& Registration, FAssetSlotHandle& OutNewSlot, uint64& OutDomain) noexcept;

    struct FImpl;
    FImpl* Impl = nullptr;
};

} // namespace LE
