#include "AssetLoading/AssetLoading.h"
#include "Scene/SceneDocument.h"

#include <cstdio>

#if PLATFORM_WINDOWS
#include <Windows.h>
#endif

namespace LE
{
namespace
{
bool CheckReference(StringView FilePath, FSceneEntityId Entity, const char* Field,
    EAssetLoadResult Result, const FAssetLoadDiagnostic& Diagnostic) noexcept
{
    if (Result == EAssetLoadResult::Success)
    {
        return true;
    }
    std::fprintf(stderr, "%.*s: entity %s field %s: asset %s (%s), result %u, file %s, format %u\n",
        static_cast<int>(FilePath.Size()), FilePath.Data(), Entity.ToString().Data(), Field,
        Diagnostic.Id.ToString().Data(), Diagnostic.Path.Data(),
        static_cast<unsigned>(Result), DescribeContentIOResult(Diagnostic.FileResult),
        static_cast<unsigned>(Diagnostic.FormatResult));
    return false;
}

int CheckScene(StringView FilePath, StringView MountName, StringView AbsoluteRoot) noexcept
{
    Array<uint8> Bytes;
    const auto FileResult = ReadContentFile(FilePath, MaxSceneDocumentBytes, Bytes);
    if (FileResult != EContentIOResult::Success)
    {
        std::fprintf(stderr, "%.*s: %s\n", static_cast<int>(FilePath.Size()), FilePath.Data(),
            DescribeContentIOResult(FileResult));
        return 1;
    }

    FSceneDocument Document;
    FSceneDocumentDiagnostic DocumentDiagnostic;
    const auto DocumentResult = DecodeSceneDocument(
        {reinterpret_cast<const char*>(Bytes.Data()), Bytes.Size()}, Document, &DocumentDiagnostic);
    if (DocumentResult != ESceneDocumentResult::Success)
    {
        const String Id = DocumentDiagnostic.Entity.IsValid()
            ? DocumentDiagnostic.Entity.ToString() : String("-");
        std::fprintf(stderr, "%.*s:%zu:%zu: %s (entity %s, field %.*s)\n",
            static_cast<int>(FilePath.Size()), FilePath.Data(), DocumentDiagnostic.Line,
            DocumentDiagnostic.Column, DescribeSceneDocumentResult(DocumentResult), Id.Data(),
            static_cast<int>(DocumentDiagnostic.Field.Size()),
            DocumentDiagnostic.Field.IsEmpty() ? "-" : DocumentDiagnostic.Field.Data());
        return 1;
    }

    const FAssetMount Mount{MountName, AbsoluteRoot};
    FAssetCatalog Catalog;
    FAssetLoadDiagnostic CatalogDiagnostic;
    if (Catalog.Rebuild({&Mount, 1}, &CatalogDiagnostic) != EAssetLoadResult::Success)
    {
        std::fprintf(stderr, "%.*s: asset catalog validation failed at %s (result %u, file %s, format %u)\n",
            static_cast<int>(FilePath.Size()), FilePath.Data(), CatalogDiagnostic.Path.Data(),
            static_cast<unsigned>(CatalogDiagnostic.Result), DescribeContentIOResult(CatalogDiagnostic.FileResult),
            static_cast<unsigned>(CatalogDiagnostic.FormatResult));
        return 1;
    }

    HashMap<FAssetId, bool> MeshUVs;
    HashMap<FAssetId, FAssetId> MaterialTextures;
    FAssetLoadSession CpuTextures(Catalog);
    for (const auto& Entity : Document.Entities)
    {
        if (!Entity.bHasMesh)
        {
            continue;
        }
        FAssetLoadDiagnostic Diagnostic;
        const bool* CachedUVs = MeshUVs.Find(Entity.Mesh.Mesh);
        bool bHasUVs = false;
        if (CachedUVs != nullptr)
        {
            bHasUVs = *CachedUVs;
        }
        else
        {
            FMeshAssetData Mesh;
            if (!CheckReference(FilePath, Entity.Id, "mesh.mesh",
                    Catalog.Decode(Entity.Mesh.Mesh, Mesh, &Diagnostic), Diagnostic))
            {
                return 1;
            }
            bHasUVs = !Mesh.UVs.IsEmpty();
            if (MeshUVs.TryInsert(Entity.Mesh.Mesh, bHasUVs) != EHashInsertResult::Inserted)
            {
                std::fputs("SceneTool: could not cache decoded mesh metadata\n", stderr);
                return 1;
            }
        }
        FAssetId Texture;
        if (const FAssetId* Cached = MaterialTextures.Find(Entity.Mesh.Material))
        {
            Texture = *Cached;
        }
        else
        {
            FMaterialAssetData Material;
            if (!CheckReference(FilePath, Entity.Id, "mesh.material",
                    Catalog.Decode(Entity.Mesh.Material, Material, &Diagnostic), Diagnostic))
            {
                return 1;
            }
            Texture = Material.BaseColorTexture;
            if (Texture.IsValid())
            {
                FCpuTextureAssetHandle Handle;
                if (!CheckReference(FilePath, Entity.Id, "mesh.material.baseColorTexture",
                        CpuTextures.Load(Texture, Handle, &Diagnostic), Diagnostic))
                {
                    return 1;
                }
            }
            if (MaterialTextures.TryInsert(Entity.Mesh.Material, Texture) != EHashInsertResult::Inserted)
            {
                std::fputs("SceneTool: could not cache decoded material metadata\n", stderr);
                return 1;
            }
        }
        if (Texture.IsValid() && !bHasUVs)
        {
            std::fprintf(stderr, "%.*s: entity %s mesh %s has no UVs for material %s and texture %s\n",
                static_cast<int>(FilePath.Size()), FilePath.Data(), Entity.Id.ToString().Data(),
                Entity.Mesh.Mesh.ToString().Data(), Entity.Mesh.Material.ToString().Data(),
                Texture.ToString().Data());
            return 1;
        }
    }
    std::printf("Scene valid: %.*s (%zu entities)\n",
        static_cast<int>(FilePath.Size()), FilePath.Data(), Document.Entities.Size());
    return 0;
}

void PrintUsage() noexcept
{
    std::fputs("Usage: SceneTool check SCENE.json MOUNT ABSOLUTE_ASSET_ROOT\n", stderr);
}

#if PLATFORM_WINDOWS
bool ToUtf8(const wchar_t* Wide, String& Out) noexcept
{
    const int ByteCount = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Wide, -1,
        nullptr, 0, nullptr, nullptr);
    Array<char> Buffer;
    return ByteCount > 0 && Buffer.TryResize(static_cast<std::size_t>(ByteCount)) &&
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Wide, -1,
            Buffer.Data(), ByteCount, nullptr, nullptr) == ByteCount &&
        Out.TryAssign({Buffer.Data(), static_cast<std::size_t>(ByteCount - 1)});
}

int RunSceneTool(int Count, wchar_t** Args) noexcept
{
    String Command;
    String Path;
    String Mount;
    String Root;
    if (Count != 5 || !ToUtf8(Args[1], Command) || Command.View() != StringView("check") ||
        !ToUtf8(Args[2], Path) || !ToUtf8(Args[3], Mount) || !ToUtf8(Args[4], Root))
    {
        PrintUsage();
        return 2;
    }
    return CheckScene(Path.View(), Mount.View(), Root.View());
}
#else
int RunSceneTool(int Count, char** Args) noexcept
{
    if (Count != 5 || StringView(Args[1]) != StringView("check"))
    {
        PrintUsage();
        return 2;
    }
    return CheckScene(Args[2], Args[3], Args[4]);
}
#endif
} // namespace
} // namespace LE

#if PLATFORM_WINDOWS
int wmain(int Count, wchar_t** Args)
{
    return LE::RunSceneTool(Count, Args);
}
#else
int main(int Count, char** Args)
{
    return LE::RunSceneTool(Count, Args);
}
#endif
