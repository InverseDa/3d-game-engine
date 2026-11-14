#include "AssetLoading/AssetLoading.h"

#include "Memory/UniquePtr.h"
#include "Containers/HashSet.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <utility>

namespace LE
{
namespace
{
constexpr std::size_t MaxCatalogEntries = 16384;
constexpr std::size_t MaxMounts = 64;
constexpr StringView AssetSuffix(".leasset", 8);

EAssetLoadResult Report(FAssetLoadDiagnostic* Diagnostic, EAssetLoadResult Result,
    FAssetId Id = {}, StringView Path = {},
    EContentIOResult FileResult = EContentIOResult::Success,
    EAssetFormatResult FormatResult = EAssetFormatResult::Success,
    EAssetRegisterResult RegistrationResult = EAssetRegisterResult::Success) noexcept
{
    if (Diagnostic != nullptr)
    {
        Diagnostic->Result = Result;
        Diagnostic->Id = Id;
        Diagnostic->FileResult = FileResult;
        Diagnostic->FormatResult = FormatResult;
        Diagnostic->RegistrationResult = RegistrationResult;
        if (!Diagnostic->Path.TryAssign(Path))
        {
            Diagnostic->Path.Clear();
        }
    }
    return Result;
}

bool IsPortableName(StringView Path) noexcept
{
    for (std::size_t Index = 0; Index < Path.Size(); ++Index)
    {
        if (static_cast<unsigned char>(Path[Index]) >= 128)
        {
            return false;
        }
    }
    return true;
}

bool FoldAscii(StringView Value, String& Out) noexcept
{
    if (!Out.TryReserve(Value.Size()))
    {
        return false;
    }
    for (std::size_t Index = 0; Index < Value.Size(); ++Index)
    {
        const char Character = Value[Index];
        const char Folded = Character >= 'A' && Character <= 'Z'
            ? static_cast<char>(Character + ('a' - 'A')) : Character;
        if (!Out.TryAppend(StringView(&Folded, 1)))
        {
            return false;
        }
    }
    return true;
}

EAssetLoadResult RememberPhysicalPrefixes(StringView Path,
    HashMap<String, String>& Spellings, IAllocator& Allocator) noexcept
{
    for (std::size_t End = 0; End <= Path.Size(); ++End)
    {
        if (End != Path.Size() && Path[End] != '/')
        {
            continue;
        }
        const StringView Prefix = Path.Substr(0, End);
        String Folded(Allocator);
        if (!FoldAscii(Prefix, Folded))
        {
            return EAssetLoadResult::AllocationFailed;
        }
        if (const String* Existing = Spellings.Find(Folded.View()))
        {
            if (Existing->View() != Prefix || End == Path.Size())
            {
                return EAssetLoadResult::DuplicatePath;
            }
        }
        else
        {
            String Spelling(Allocator);
            if (!Spelling.TryAssign(Prefix) ||
                Spellings.TryInsert(std::move(Folded), std::move(Spelling)) != EHashInsertResult::Inserted)
            {
                return EAssetLoadResult::AllocationFailed;
            }
        }
    }
    return EAssetLoadResult::Success;
}

struct FCatalogRecord
{
    explicit FCatalogRecord(IAllocator& Allocator) noexcept
        : Path(Allocator), Relative(Allocator) {}

    FAssetCatalogEntry View() const noexcept { return {Id, Type, Path.View()}; }

    FAssetId Id;
    FAssetTypeId Type;
    String Path;
    String Relative;
    std::size_t MountIndex = 0;
};

struct FLoadingScope
{
    explicit FLoadingScope(bool& Loading) noexcept : bLoading(Loading) { bLoading = true; }
    ~FLoadingScope() { bLoading = false; }
    bool& bLoading;
};

void DestroyCpuTexture(void* Context, void* Object) noexcept
{
    auto& Allocator = *static_cast<IAllocator*>(Context);
    auto* Texture = static_cast<FCpuTextureAsset*>(Object);
    Texture->~FCpuTextureAsset();
    DeallocateArray(Allocator, Texture, 1);
}
} // namespace

struct FAssetCatalog::FIndex
{
    explicit FIndex(IAllocator& Allocator) noexcept
        : Roots(Allocator), Records(Allocator), ById(Allocator), ByPath(Allocator) {}

    Array<String> Roots;
    Array<FCatalogRecord> Records;
    HashMap<FAssetId, std::size_t> ById;
    HashMap<String, std::size_t> ByPath;
};

FAssetCatalog::FAssetCatalog(IAllocator& InAllocator) noexcept : Allocator(InAllocator) {}

FAssetCatalog::~FAssetCatalog() noexcept = default;

EAssetLoadResult FAssetCatalog::Rebuild(Span<const FAssetMount> Mounts,
    FAssetLoadDiagnostic* Diagnostic) noexcept
{
    if (Mounts.Size() > MaxMounts)
    {
        return Report(Diagnostic, EAssetLoadResult::InvalidMount);
    }
    auto Candidate = TryMakeUniqueWithAllocator<FIndex>(Allocator, Allocator);
    if (!Candidate || !Candidate->Roots.TryReserve(Mounts.Size()))
    {
        return Report(Diagnostic, EAssetLoadResult::AllocationFailed);
    }
    HashSet<String> MountNames(Allocator);
    for (const FAssetMount& Mount : Mounts)
    {
        String Probe(Allocator);
        FSoftAssetPath Canonical(Allocator);
        if (!Probe.TryAssign(Mount.Name) || !Probe.TryAppend("/Asset"))
        {
            return Report(Diagnostic, EAssetLoadResult::AllocationFailed, {}, Mount.Name);
        }
        if (Mount.Name.IsEmpty() || !IsPortableName(Mount.Name) ||
            Canonical.TryAssign(Probe.View()) != ESoftAssetPathResult::Success ||
            Canonical.View() != Probe.View() ||
            std::memchr(Mount.Name.Data(), '/', Mount.Name.Size()) != nullptr)
        {
            return Report(Diagnostic, EAssetLoadResult::InvalidMount, {}, Mount.Name);
        }
        String FoldedMount(Allocator);
        if (!FoldAscii(Mount.Name, FoldedMount))
        {
            return Report(Diagnostic, EAssetLoadResult::AllocationFailed, {}, Mount.Name);
        }
        const EHashInsertResult MountResult = MountNames.TryInsert(std::move(FoldedMount));
        if (MountResult != EHashInsertResult::Inserted)
        {
            return Report(Diagnostic, MountResult == EHashInsertResult::AlreadyExists
                ? EAssetLoadResult::DuplicatePath : EAssetLoadResult::AllocationFailed, {}, Mount.Name);
        }
        Array<String> Files(Allocator);
        String FailingRelativePath(Allocator);
        const auto ListResult = EnumerateContentFiles(Mount.AbsoluteRoot, AssetSuffix,
            MaxCatalogEntries - Candidate->Records.Size(), Files, &FailingRelativePath);
        if (ListResult != EContentIOResult::Success)
        {
            String FailingPath(Allocator);
            if (!FailingRelativePath.IsEmpty() && FailingPath.TryAssign(Mount.Name) &&
                FailingPath.TryAppend("/") && FailingPath.TryAppend(FailingRelativePath.View()))
            {
                return Report(Diagnostic, EAssetLoadResult::FileError, {}, FailingPath.View(), ListResult);
            }
            return Report(Diagnostic, EAssetLoadResult::FileError, {}, Mount.AbsoluteRoot, ListResult);
        }
        const std::size_t MountIndex = Candidate->Roots.Size();
        String Root(Allocator);
        if (!Root.TryAssign(Mount.AbsoluteRoot) || !Candidate->Roots.TryPushBack(std::move(Root)))
        {
            return Report(Diagnostic, EAssetLoadResult::AllocationFailed, {}, Mount.AbsoluteRoot);
        }
        if (Files.Size() > 1)
        {
            std::sort(Files.Data(), Files.Data() + Files.Size());
        }
        HashMap<String, String> PhysicalSpellings(Allocator);
        for (String& Relative : Files)
        {
            String Resolved(Allocator);
            auto FileResult = ResolveContentFile(Mount.AbsoluteRoot, Relative.View(), Resolved);
            Array<uint8> Prefix(Allocator);
            uint64 FileSize = 0;
            if (FileResult == EContentIOResult::Success)
            {
                FileResult = ReadContentFilePrefix(Resolved.View(), MaxAssetFileHeaderBytes, Prefix, FileSize);
            }
            if (FileResult != EContentIOResult::Success)
            {
                return Report(Diagnostic, EAssetLoadResult::FileError, {}, Relative.View(), FileResult);
            }
            FAssetFileHeaderView Header;
            const EAssetFormatResult HeaderResult = FileSize <= MaxAssetFileBytes
                ? ReadAssetFileHeader({Prefix.Data(), Prefix.Size()}, static_cast<std::size_t>(FileSize), Header, Allocator)
                : EAssetFormatResult::LimitExceeded;
            if (HeaderResult != EAssetFormatResult::Success)
            {
                return Report(Diagnostic, EAssetLoadResult::FormatError, {}, Relative.View(),
                    EContentIOResult::Success, HeaderResult);
            }
            FCatalogRecord Entry(Allocator);
            Entry.Id = Header.Id;
            Entry.Type = Header.TypeId;
            Entry.MountIndex = MountIndex;
            if (Relative.Size() <= AssetSuffix.Size() ||
                Relative.View().Substr(Relative.Size() - AssetSuffix.Size(), AssetSuffix.Size()) != AssetSuffix ||
                !IsPortableName(Header.Path))
            {
                return Report(Diagnostic, EAssetLoadResult::IdentityMismatch, Header.Id, Relative.View());
            }
            if (!Entry.Path.TryAssign(Mount.Name) || !Entry.Path.TryAppend("/") ||
                !Entry.Path.TryAppend(Relative.View().Substr(0, Relative.Size() - AssetSuffix.Size())))
            {
                return Report(Diagnostic, EAssetLoadResult::AllocationFailed, Header.Id, Relative.View());
            }
            if (Entry.Path.View() != Header.Path)
            {
                return Report(Diagnostic, EAssetLoadResult::IdentityMismatch, Header.Id, Entry.Path.View());
            }
            String Key(Allocator);
            if (!Key.TryAssign(Entry.Path.View()))
            {
                return Report(Diagnostic, EAssetLoadResult::AllocationFailed, Header.Id, Entry.Path.View());
            }
            if (Candidate->ById.Contains(Entry.Id))
            {
                return Report(Diagnostic, EAssetLoadResult::DuplicateId, Header.Id, Entry.Path.View());
            }
            const auto PortableResult = RememberPhysicalPrefixes(Relative.View(), PhysicalSpellings, Allocator);
            if (PortableResult != EAssetLoadResult::Success)
            {
                return Report(Diagnostic, PortableResult, Header.Id, Entry.Path.View());
            }
            const std::size_t Slot = Candidate->Records.Size();
            if (Candidate->ById.TryInsert(Entry.Id, Slot) != EHashInsertResult::Inserted ||
                Candidate->ByPath.TryInsert(std::move(Key), Slot) != EHashInsertResult::Inserted)
            {
                return Report(Diagnostic, EAssetLoadResult::AllocationFailed, Header.Id, Entry.Path.View());
            }
            Entry.Relative = std::move(Relative);
            if (!Candidate->Records.TryPushBack(std::move(Entry)))
            {
                return Report(Diagnostic, EAssetLoadResult::AllocationFailed, Header.Id, Header.Path);
            }
        }
    }
    Index = std::move(Candidate);
    return Report(Diagnostic, EAssetLoadResult::Success);
}

EAssetLoadResult FAssetCatalog::Find(FAssetId Id, FAssetCatalogEntry& Out) const noexcept
{
    if (!Id.IsValid())
    {
        return EAssetLoadResult::InvalidInput;
    }
    const std::size_t* Slot = Index ? Index->ById.Find(Id) : nullptr;
    if (Slot == nullptr)
    {
        return EAssetLoadResult::NotFound;
    }
    Out = Index->Records[*Slot].View();
    return EAssetLoadResult::Success;
}

EAssetLoadResult FAssetCatalog::Find(StringView Path, FAssetCatalogEntry& Out) const noexcept
{
    FSoftAssetPath Canonical(Allocator);
    const auto Result = Canonical.TryAssign(Path);
    if (Result != ESoftAssetPathResult::Success)
    {
        return Result == ESoftAssetPathResult::AllocationFailed
            ? EAssetLoadResult::AllocationFailed : EAssetLoadResult::InvalidInput;
    }
    const std::size_t* Slot = Index ? Index->ByPath.Find(Canonical.View()) : nullptr;
    if (Slot == nullptr)
    {
        return EAssetLoadResult::NotFound;
    }
    Out = Index->Records[*Slot].View();
    return EAssetLoadResult::Success;
}

std::size_t FAssetCatalog::GetEntryCount() const noexcept
{
    return Index ? Index->Records.Size() : 0;
}

EAssetLoadResult FAssetCatalog::Read(FAssetId Id, Array<uint8>& Out,
    FAssetLoadDiagnostic* Diagnostic) const noexcept
{
    const std::size_t* Slot = Index ? Index->ById.Find(Id) : nullptr;
    if (Slot == nullptr)
    {
        return Report(Diagnostic, EAssetLoadResult::NotFound, Id);
    }
    const FCatalogRecord& Entry = Index->Records[*Slot];
    String Resolved(Allocator);
    auto FileResult = ResolveContentFile(Index->Roots[Entry.MountIndex].View(),
        Entry.Relative.View(), Resolved);
    if (FileResult == EContentIOResult::Success)
    {
        FileResult = ReadContentFile(Resolved.View(), MaxAssetFileBytes, Out);
    }
    if (FileResult != EContentIOResult::Success)
    {
        return Report(Diagnostic, EAssetLoadResult::FileError, Id, Entry.Path.View(), FileResult);
    }
    FAssetFileHeaderView Header;
    const auto Result = ReadAssetFileHeader({Out.Data(), Out.Size()}, Header, Allocator);
    if (Result != EAssetFormatResult::Success)
    {
        return Report(Diagnostic, EAssetLoadResult::FormatError, Id, Entry.Path.View(),
            EContentIOResult::Success, Result);
    }
    if (Header.Id != Entry.Id || Header.TypeId != Entry.Type || Header.Path != Entry.Path.View())
    {
        return Report(Diagnostic, EAssetLoadResult::IdentityMismatch, Id, Entry.Path.View());
    }
    return EAssetLoadResult::Success;
}

FAssetLoadSession::FAssetLoadSession(const FAssetCatalog& InCatalog,
    FAssetLoadProviders InProviders, IAllocator& InAllocator) noexcept
    : Catalog(InCatalog), Providers(InProviders), Allocator(InAllocator), Registry(InAllocator) {}

FAssetLoadSession::~FAssetLoadSession() noexcept = default;

template <typename Asset>
EAssetLoadResult FAssetLoadSession::LoadPath(StringView Path, TAssetHandle<Asset>& Out,
    FAssetLoadDiagnostic* Diagnostic) noexcept
{
    FAssetCatalogEntry Entry;
    const auto Result = Catalog.Find(Path, Entry);
    return Result == EAssetLoadResult::Success
        ? Load(Entry.Id, Out, Diagnostic) : Report(Diagnostic, Result, {}, Path);
}

template <typename Asset, typename Data>
EAssetLoadResult FAssetLoadSession::LoadAsset(FAssetId Id, TAssetHandle<Asset>& Out,
    const TAssetLoadProvider<Asset, Data>& Provider, FAssetLoadDiagnostic* Diagnostic) noexcept
{
    if (bLoading)
    {
        return Report(Diagnostic, EAssetLoadResult::ReentrantLoad, Id);
    }
    FLoadingScope Loading(bLoading);
    FAssetCatalogEntry Entry;
    const auto FindResult = Catalog.Find(Id, Entry);
    if (FindResult != EAssetLoadResult::Success)
    {
        return Report(Diagnostic, FindResult, Id);
    }
    if (Entry.Type != GetAssetTypeId<Asset>())
    {
        return Report(Diagnostic, EAssetLoadResult::TypeMismatch, Id, Entry.Path);
    }
    if (!Registry.IsValid())
    {
        return Report(Diagnostic, EAssetLoadResult::AllocationFailed, Id, Entry.Path);
    }
    if (Registry.TryFindById(Id, Out) == EAssetFindResult::Success)
    {
        return Report(Diagnostic, EAssetLoadResult::Success, Id, Entry.Path);
    }
    if constexpr (!std::is_same<Asset, FCpuTextureAsset>::value)
    {
        if (Provider.Create == nullptr || Provider.Destroy == nullptr)
        {
            return Report(Diagnostic, EAssetLoadResult::ProviderUnavailable, Id, Entry.Path);
        }
    }
    Array<uint8> Bytes(Allocator);
    const auto ReadResult = Catalog.Read(Id, Bytes, Diagnostic);
    if (ReadResult != EAssetLoadResult::Success)
    {
        return ReadResult;
    }
    auto DecodeAndRegister = [&](Data& Payload) noexcept
    {
        const auto FormatResult = DecodeAssetFile({Bytes.Data(), Bytes.Size()}, Payload);
        if (FormatResult != EAssetFormatResult::Success)
        {
            const auto Result = FormatResult == EAssetFormatResult::AllocationFailed
                ? EAssetLoadResult::AllocationFailed : EAssetLoadResult::FormatError;
            return Report(Diagnostic, Result, Id, Entry.Path, EContentIOResult::Success, FormatResult);
        }

        TAssetRegistration<Asset> Registration;
        Registration.Id = Id;
        Registration.SoftPath = Entry.Path;
        Registration.ProviderName = "AssetLoading";
        Registration.Ownership = EAssetOwnership::OwnedByProviderThunk;
        if constexpr (std::is_same<Asset, FCpuTextureAsset>::value)
        {
            Asset* Storage = TryAllocateArray<Asset>(Allocator, 1);
            if (Storage == nullptr)
            {
                return Report(Diagnostic, EAssetLoadResult::AllocationFailed, Id, Entry.Path);
            }
            Registration.Object = new (Storage) Asset(std::move(Payload));
            Registration.ProviderContext = &Allocator;
            Registration.Destroy = &DestroyCpuTexture;
        }
        else
        {
            Registration.Object = Provider.Create(Provider.Context, Payload);
            if (Registration.Object == nullptr)
            {
                return Report(Diagnostic, EAssetLoadResult::ProviderFailed, Id, Entry.Path);
            }
            Registration.ProviderContext = Provider.Context;
            Registration.Destroy = Provider.Destroy;
        }
        const auto RegisterResult = Registry.Register(Registration, Out);
        if (RegisterResult != EAssetRegisterResult::Success)
        {
            Registration.Destroy(Registration.ProviderContext, Registration.Object);
            return Report(Diagnostic, EAssetLoadResult::RegistrationFailed, Id, Entry.Path,
                EContentIOResult::Success, EAssetFormatResult::Success, RegisterResult);
        }
        return Report(Diagnostic, EAssetLoadResult::Success, Id, Entry.Path);
    };
    if constexpr (std::is_same<Data, FMaterialAssetData>::value)
    {
        Data Payload;
        return DecodeAndRegister(Payload);
    }
    else
    {
        Data Payload(Allocator);
        return DecodeAndRegister(Payload);
    }
}

EAssetLoadResult FAssetLoadSession::Load(FAssetId Id, FMeshAssetHandle& Out,
    FAssetLoadDiagnostic* Diagnostic) noexcept
{
    return LoadAsset(Id, Out, Providers.Mesh, Diagnostic);
}

EAssetLoadResult FAssetLoadSession::Load(FAssetId Id, FMaterialAssetHandle& Out,
    FAssetLoadDiagnostic* Diagnostic) noexcept
{
    return LoadAsset(Id, Out, Providers.Material, Diagnostic);
}

EAssetLoadResult FAssetLoadSession::Load(FAssetId Id, FCpuTextureAssetHandle& Out,
    FAssetLoadDiagnostic* Diagnostic) noexcept
{
    return LoadAsset(Id, Out, TAssetLoadProvider<FCpuTextureAsset, FCpuTextureAsset>{}, Diagnostic);
}

EAssetLoadResult FAssetLoadSession::Load(StringView Path, FMeshAssetHandle& Out,
    FAssetLoadDiagnostic* Diagnostic) noexcept
{
    return LoadPath(Path, Out, Diagnostic);
}

EAssetLoadResult FAssetLoadSession::Load(StringView Path, FMaterialAssetHandle& Out,
    FAssetLoadDiagnostic* Diagnostic) noexcept
{
    return LoadPath(Path, Out, Diagnostic);
}

EAssetLoadResult FAssetLoadSession::Load(StringView Path, FCpuTextureAssetHandle& Out,
    FAssetLoadDiagnostic* Diagnostic) noexcept
{
    return LoadPath(Path, Out, Diagnostic);
}
} // namespace LE
