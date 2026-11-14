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

    for (const auto& Entity : Document.Entities)
    {
        if (!Entity.bHasMesh)
        {
            continue;
        }
        const FAssetId References[]{Entity.Mesh.Mesh, Entity.Mesh.Material};
        const FAssetTypeId Types[]{GetMeshAssetTypeId(), GetMaterialAssetTypeId()};
        const char* Fields[]{"mesh.mesh", "mesh.material"};
        for (std::size_t Index = 0; Index < 2; ++Index)
        {
            FAssetCatalogEntry Entry;
            const auto Found = Catalog.Find(References[Index], Entry);
            if (Found != EAssetLoadResult::Success || Entry.Type != Types[Index])
            {
                std::fprintf(stderr, "%.*s: entity %s field %s: %s asset %s\n",
                    static_cast<int>(FilePath.Size()), FilePath.Data(), Entity.Id.ToString().Data(),
                    Fields[Index], Found == EAssetLoadResult::Success ? "wrong type for" : "missing",
                    References[Index].ToString().Data());
                return 1;
            }
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
