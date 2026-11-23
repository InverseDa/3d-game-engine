#include "AssetLoading/AssetLoading.h"
#include "Renderer/RenderAssets.h"

#include <cstdio>
#include <cstdlib>
#include <new>

#if PLATFORM_WINDOWS
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace LE
{
namespace
{
int Check(bool Condition, const char* Description)
{
    if (Condition)
    {
        return 0;
    }
    std::fprintf(stderr, "FAILED: asset loading: %s\n", Description);
    return 1;
}

class FRegistrationAllocator final : public IAllocator
{
public:
    void* Allocate(std::size_t Size, std::size_t Alignment) noexcept override { return bFail ? nullptr : GetDefaultAllocator().Allocate(Size, Alignment); }

    void Deallocate(void* Address, std::size_t Size, std::size_t Alignment) noexcept override { GetDefaultAllocator().Deallocate(Address, Size, Alignment); }

    bool bFail = false;
};

struct FMaterialProviderState
{
    int Created = 0;
    int Destroyed = 0;
    bool bFailCreate = false;
    FRegistrationAllocator* FailRegistration = nullptr;
    FAssetLoadSession* Session = nullptr;
    FAssetId ObservedTexture;
    uint8 FirstPixel = 0;
    EAssetLoadResult Reentrant = EAssetLoadResult::Success;
};

FMaterialAsset* CreateMaterial(void* Context, const FMaterialAssetData& Data, const FCpuTextureAsset* Texture) noexcept
{
    auto& State = *static_cast<FMaterialProviderState*>(Context);
    State.ObservedTexture = Data.BaseColorTexture;
    State.FirstPixel = Texture != nullptr && !Texture->Pixels.IsEmpty() ? Texture->Pixels[0] : 0;
    if (State.Session != nullptr && Texture != nullptr)
    {
        FCpuTextureAssetHandle Handle;
        State.Reentrant = State.Session->Load(Data.BaseColorTexture, Handle);
    }
    if (State.bFailCreate)
    {
        return nullptr;
    }
    auto* Material = new (std::nothrow) FMaterialAsset;
    if (Material != nullptr)
    {
        ++State.Created;
        Material->BaseColor = Math::Vector4f(Data.BaseColor[0], Data.BaseColor[1], Data.BaseColor[2], 1.0f);
        if (State.FailRegistration != nullptr)
        {
            State.FailRegistration->bFail = true;
        }
    }
    return Material;
}

void DestroyMaterial(void* Context, void* Object) noexcept
{
    auto& State = *static_cast<FMaterialProviderState*>(Context);
    ++State.Destroyed;
    delete static_cast<FMaterialAsset*>(Object);
}

struct FFiles
{
    String Root;
    String Material;
    String Other;
    String Texture;
    bool bCreated = false;

    bool Setup()
    {
        FUuid Id;
        char Current[4096];
#if PLATFORM_WINDOWS
        const bool bHaveDirectory = _getcwd(Current, sizeof(Current)) != nullptr;
#else
        const bool bHaveDirectory = getcwd(Current, sizeof(Current)) != nullptr;
#endif
        if (!bHaveDirectory || !FUuid::TryGenerate(Id))
        {
            return false;
        }
        Root.Assign(Current);
        Root.Append("/asset-loading-");
        Root.Append(Id.ToString().View());
#if PLATFORM_WINDOWS
        bCreated = _mkdir(Root.Data()) == 0;
#else
        bCreated = mkdir(Root.Data(), 0700) == 0;
#endif
        if (bCreated)
        {
            Material = Root;
            Material.Append("/Material.leasset");
            Other = Root;
            Other.Append("/Other.leasset");
            Texture = Root;
            Texture.Append("/Texture.leasset");
        }
        return bCreated;
    }

    ~FFiles()
    {
        if (!bCreated)
        {
            return;
        }
        std::remove(Material.Data());
        std::remove(Other.Data());
        std::remove(Texture.Data());
#if PLATFORM_WINDOWS
        _rmdir(Root.Data());
#else
        rmdir(Root.Data());
#endif
    }
};

bool ChangeDirectory(const char* Path) noexcept
{
#if PLATFORM_WINDOWS
    return _chdir(Path) == 0;
#else
    return chdir(Path) == 0;
#endif
}

bool GetCurrentDirectory(char* Out, int Size) noexcept
{
#if PLATFORM_WINDOWS
    return _getcwd(Out, Size) != nullptr;
#else
    return getcwd(Out, static_cast<std::size_t>(Size)) != nullptr;
#endif
}
} // namespace

int RunAssetLoadingTests()
{
    FFiles Files;
    if (!Files.Setup())
    {
        return Check(false, "create isolated asset fixture directory");
    }
    FAssetId MaterialId;
    FAssetId TextureId;
    FAssetId OtherId;
    FAssetId::TryParse("02345678-1234-4000-8000-1234567890ab", MaterialId);
    FAssetId::TryParse("12345678-1234-4000-8000-1234567890ab", TextureId);
    FAssetId::TryParse("22345678-1234-4000-8000-1234567890ab", OtherId);
    Array<uint8> MaterialBytes;
    Array<uint8> TextureBytes;
    FCpuTextureAsset Texture;
    Texture.Width = 1;
    Texture.Height = 1;
    Texture.Pixels.PushBack(32);
    Texture.Pixels.PushBack(64);
    Texture.Pixels.PushBack(128);
    Texture.Pixels.PushBack(255);
    if (EncodeAssetFile(MaterialId, "Game/Material", FMaterialAssetData{}, MaterialBytes) != EAssetFormatResult::Success || EncodeAssetFile(TextureId, "Game/Texture", Texture, TextureBytes) != EAssetFormatResult::Success || WriteContentFileAtomically(Files.Material.View(), {MaterialBytes.Data(), MaterialBytes.Size()}) != EContentIOResult::Success || WriteContentFileAtomically(Files.Texture.View(), {TextureBytes.Data(), TextureBytes.Size()}) != EContentIOResult::Success)
    {
        return Check(false, "write catalog fixture");
    }

    FAssetCatalog Catalog;
    FAssetLoadDiagnostic Diagnostic;
    const FAssetMount Mount{"Game", Files.Root.View()};
    if (Catalog.Rebuild({&Mount, 1}, &Diagnostic) != EAssetLoadResult::Success)
    {
        return Check(false, "build a valid absolute mount");
    }
    FAssetCatalogEntry ById;
    FAssetCatalogEntry ByPath;
    int Failures = Check(Catalog.Find(MaterialId, ById) == EAssetLoadResult::Success && Catalog.Find("Game//Folder/../Material", ByPath) == EAssetLoadResult::Success && ById.Id == ByPath.Id && ById.Path == ByPath.Path, "canonical path and ID identify the same entry");

    char OriginalDirectory[4096];
    if (GetCurrentDirectory(OriginalDirectory, sizeof(OriginalDirectory)) && ChangeDirectory(Files.Root.Data()))
    {
        FAssetCatalog Alternate;
        FAssetCatalogEntry Entry;
        const bool bMatches = Alternate.Rebuild({&Mount, 1}) == EAssetLoadResult::Success && Alternate.Find("Game/Material", Entry) == EAssetLoadResult::Success && Entry.Id == MaterialId;
        if (!ChangeDirectory(OriginalDirectory))
        {
            return Failures + Check(false, "restore fixture working directory");
        }
        Failures += Check(bMatches, "absolute mount is independent of the working directory");
    }
    else
    {
        Failures += Check(false, "enter alternate fixture working directory");
    }

    const FAssetMount MountAliases[] = {Mount, {"game", Files.Root.View()}};
    Failures += Check(Catalog.Rebuild({MountAliases, 2}, &Diagnostic) == EAssetLoadResult::DuplicatePath && Catalog.GetEntryCount() == 2, "case alias preserves previously published index");
    Array<uint8> OtherBytes;
    if (EncodeAssetFile(MaterialId, "Game/Other", FMaterialAssetData{}, OtherBytes) != EAssetFormatResult::Success || WriteContentFileAtomically(Files.Other.View(), {OtherBytes.Data(), OtherBytes.Size()}) != EContentIOResult::Success)
    {
        return Failures + Check(false, "write duplicate ID fixture");
    }
    Failures += Check(Catalog.Rebuild({&Mount, 1}, &Diagnostic) == EAssetLoadResult::DuplicateId && Catalog.GetEntryCount() == 2 && Catalog.Find(MaterialId, ById) == EAssetLoadResult::Success, "duplicate ID preserves index");
    Failures += Check(WriteContentFileAtomically(Files.Other.View(), {MaterialBytes.Data(), MaterialBytes.Size()}) == EContentIOResult::Success && Catalog.Rebuild({&Mount, 1}, &Diagnostic) == EAssetLoadResult::IdentityMismatch && Catalog.GetEntryCount() == 2, "reject logical path inconsistent with file location");

    FMaterialProviderState Provider;
    FAssetLoadProviders Providers;
    Providers.Material = {&Provider, &CreateMaterial, &DestroyMaterial};
    FRegistrationAllocator Allocator;
    {
        FAssetLoadSession Session(Catalog, Providers, Allocator);
        FMaterialAssetHandle Material;
        FMaterialAssetHandle Repeat;
        FCpuTextureAssetHandle TextureHandle;
        Failures += Check(Session.Load(TextureId, TextureHandle) == EAssetLoadResult::Success, "load CPU texture without provider");
        const FCpuTextureAsset* Pixels = nullptr;
        Failures += Check(Session.GetRegistry().Resolve(TextureHandle, Pixels) == EAssetResolveResult::Success && Pixels != nullptr && Pixels->Width == 1 && Pixels->Height == 1 && Pixels->Pixels.Size() == 4 && Pixels->Pixels[0] == 32, "resolve decoded pixels");
        Failures += Check(Session.Load(MaterialId, TextureHandle, &Diagnostic) == EAssetLoadResult::TypeMismatch && Diagnostic.Id == MaterialId && TextureHandle.IsValid(), "wrong type leaves existing handle untouched");
        Provider.bFailCreate = true;
        Failures += Check(Session.Load(MaterialId, Material) == EAssetLoadResult::ProviderFailed && !Material.IsValid() && Session.GetRegistry().GetAssetCount() == 1, "provider failure publishes nothing");
        Provider.bFailCreate = false;
        Provider.FailRegistration = &Allocator;
        Failures += Check(Session.Load(MaterialId, Material, &Diagnostic) == EAssetLoadResult::RegistrationFailed && Diagnostic.RegistrationResult == EAssetRegisterResult::AllocationFailed && Provider.Created == 1 && Provider.Destroyed == 1 && !Material.IsValid(), "registration failure destroys its constructed object");
        Allocator.bFail = false;
        Provider.FailRegistration = nullptr;
        Failures += Check(Session.Load(MaterialId, Material) == EAssetLoadResult::Success && Session.Load("Game//Material", Repeat) == EAssetLoadResult::Success && Material == Repeat && Provider.Created == 2, "retry publishes and repeated loads reuse the handle");
    }
    Failures += Check(Provider.Destroyed == 2, "session releases the cached provider object once");

    FMaterialAssetData Textured;
    Textured.BaseColorTexture = TextureId;
    const auto WriteMaterial = [&](FAssetId Id, StringView Path, const String& File) {
        Array<uint8> Bytes;
        return EncodeAssetFile(Id, Path, Textured, Bytes) == EAssetFormatResult::Success && WriteContentFileAtomically(File.View(), Bytes.AsSpan()) == EContentIOResult::Success;
    };
    if (!WriteMaterial(MaterialId, "Game/Material", Files.Material) || !WriteMaterial(OtherId, "Game/Other", Files.Other) || Catalog.Rebuild({&Mount, 1}) != EAssetLoadResult::Success)
    {
        return Failures + Check(false, "write textured material fixtures");
    }
    {
        FAssetLoadSession Session(Catalog, Providers, Allocator);
        Provider.Session = &Session;
        FMaterialAssetHandle Material;
        FMaterialAssetHandle OtherMaterial;
        FCpuTextureAssetHandle TextureHandle;
        Provider.bFailCreate = true;
        Failures += Check(Session.Load(MaterialId, Material, &Diagnostic) == EAssetLoadResult::ProviderFailed && Diagnostic.Id == MaterialId && Diagnostic.DependencyId == TextureId && !Material.IsValid() && Session.GetRegistry().GetAssetCount() == 0, "provider failure rolls back its newly registered CPU dependency");
        Provider.bFailCreate = false;
        Provider.FailRegistration = &Allocator;
        Failures += Check(Session.Load(MaterialId, Material, &Diagnostic) == EAssetLoadResult::RegistrationFailed && Diagnostic.DependencyId == TextureId && Session.GetRegistry().GetAssetCount() == 0, "failure at material publication rolls back dependency even under allocation failure");
        Allocator.bFail = false;
        Provider.FailRegistration = nullptr;
        Failures += Check(Session.Load(MaterialId, Material) == EAssetLoadResult::Success && Session.Load(TextureId, TextureHandle) == EAssetLoadResult::Success && Provider.FirstPixel == 32 && Provider.ObservedTexture == TextureId && Provider.Reentrant == EAssetLoadResult::ReentrantLoad, "material provider receives valid pixels while public reentrancy remains rejected");
        Provider.bFailCreate = true;
        Failures += Check(Session.Load(OtherId, OtherMaterial) == EAssetLoadResult::ProviderFailed && Session.GetRegistry().GetAssetCount() == 2 && Session.GetRegistry().TryFindById(TextureId, TextureHandle) == EAssetFindResult::Success, "failed material does not unregister a previously cached dependency");
        Provider.bFailCreate = false;
        Failures += Check(Session.Load(OtherId, OtherMaterial) == EAssetLoadResult::Success && Session.GetRegistry().GetAssetCount() == 3, "two materials share one CPU dependency entry");
        Provider.Session = nullptr;
    }
    {
        FAssetLoadSession Session(Catalog, Providers);
        FMaterialAssetHandle Material;
        Failures += Check(WriteContentFileAtomically(Files.Texture.View(), {TextureBytes.Data(), 2}) == EContentIOResult::Success && Session.Load(MaterialId, Material, &Diagnostic) == EAssetLoadResult::FormatError && Diagnostic.Id == MaterialId && Diagnostic.DependencyId == TextureId && Session.GetRegistry().GetAssetCount() == 0, "corrupt dependency identifies both assets");
        std::remove(Files.Texture.Data());
        Failures += Check(Session.Load(MaterialId, Material, &Diagnostic) == EAssetLoadResult::FileError && Diagnostic.FileResult == EContentIOResult::NotFound && Diagnostic.DependencyId == TextureId, "missing dependency file is diagnosed");
        Textured.BaseColorTexture = OtherId;
        Failures += Check(WriteMaterial(MaterialId, "Game/Material", Files.Material) && Session.Load(MaterialId, Material, &Diagnostic) == EAssetLoadResult::TypeMismatch && Diagnostic.DependencyId == OtherId && !Material.IsValid(), "a material cannot satisfy a texture dependency");
    }

    {
        FAssetLoadSession Session(Catalog, Providers);
        FMaterialAssetHandle Material;
        Failures += Check(EncodeAssetFile(OtherId, "Game/Material", FMaterialAssetData{}, OtherBytes) == EAssetFormatResult::Success && WriteContentFileAtomically(Files.Material.View(), {OtherBytes.Data(), OtherBytes.Size()}) == EContentIOResult::Success && Session.Load(MaterialId, Material) == EAssetLoadResult::IdentityMismatch && !Material.IsValid(), "revalidate identity after the file is replaced");
        Failures += Check(WriteContentFileAtomically(Files.Material.View(), {OtherBytes.Data(), 2}) == EContentIOResult::Success && Session.Load(MaterialId, Material) == EAssetLoadResult::FormatError, "reject truncation after indexing");
        std::remove(Files.Material.Data());
        Failures += Check(Session.Load(MaterialId, Material, &Diagnostic) == EAssetLoadResult::FileError && Diagnostic.FileResult == EContentIOResult::NotFound && Session.GetRegistry().GetAssetCount() == 0, "missing file publishes nothing");
    }
    return Failures;
}
} // namespace LE
