#include "AssetLoading/AssetLoading.h"
#include "Scene/Scene.h"

#include <cstdio>

#if PLATFORM_WINDOWS
#include <Windows.h>
#endif

namespace LE
{
namespace
{
struct FCheckReport
{
    StringView Code;
    StringView Message;
    FSceneEntityId Entity{};
    FAssetId Asset{};
    StringView Field{};
    std::size_t Line = 0;
    std::size_t Column = 0;
};

// Keep well-formed UTF-8 as UTF-8. POSIX argv can contain arbitrary bytes.
std::size_t Utf8SequenceSize(StringView Text) noexcept
{
    const auto Lead = static_cast<uint8>(Text[0]);
    if (Lead < 0xc2 || Lead > 0xf4)
    {
        return 0;
    }
    const std::size_t Size = Lead <= 0xdf ? 2 : (Lead <= 0xef ? 3 : 4);
    if (Text.Size() < Size)
    {
        return 0;
    }
    for (std::size_t Index = 1; Index < Size; ++Index)
    {
        const auto Byte = static_cast<uint8>(Text[Index]);
        if (Byte < 0x80 || Byte > 0xbf)
        {
            return 0;
        }
    }
    const auto Second = static_cast<uint8>(Text[1]);
    if ((Lead == 0xe0 && Second < 0xa0) || (Lead == 0xed && Second > 0x9f) ||
        (Lead == 0xf0 && Second < 0x90) || (Lead == 0xf4 && Second > 0x8f))
    {
        return 0;
    }
    return Size;
}

// Console adapter for the SceneTool JSON protocol.
void WriteJsonString(StringView Text) noexcept
{
    std::fputc('"', stdout);
    for (std::size_t Index = 0; Index < Text.Size();)
    {
        const auto Byte = static_cast<uint8>(Text[Index]);
        if (Byte < 0x20)
        {
            std::printf("\\u%04x", static_cast<unsigned>(Byte));
        }
        else if (Byte < 0x80)
        {
            if (Byte == '"' || Byte == '\\')
            {
                std::fputc('\\', stdout);
            }
            std::fputc(Byte, stdout);
        }
        else
        {
            const std::size_t Length = Utf8SequenceSize(Text.Substr(Index, Text.Size() - Index));
            if (Length != 0)
            {
                std::fwrite(Text.Data() + Index, 1, Length, stdout);
                Index += Length;
                continue;
            }
            std::fputs("\\ufffd", stdout);
        }
        ++Index;
    }
    std::fputc('"', stdout);
}

void WriteJsonReport(StringView FilePath, const FCheckReport& Report, std::size_t EntityCount = 0) noexcept
{
    const bool bOk = Report.Code == StringView("ok");
    std::printf("{\"ok\":%s,\"code\":", bOk ? "true" : "false");
    WriteJsonString(Report.Code);
    std::fputs(",\"message\":", stdout);
    WriteJsonString(Report.Message);
    std::fputs(",\"path\":", stdout);
    WriteJsonString(FilePath);
    if (bOk)
    {
        std::printf(",\"entityCount\":%zu", EntityCount);
    }
    if (Report.Entity.IsValid())
    {
        std::fputs(",\"entity\":", stdout);
        WriteJsonString(Report.Entity.ToString().View());
    }
    if (Report.Asset.IsValid())
    {
        std::fputs(",\"asset\":", stdout);
        WriteJsonString(Report.Asset.ToString().View());
    }
    if (!Report.Field.IsEmpty())
    {
        std::fputs(",\"field\":", stdout);
        WriteJsonString(Report.Field);
    }
    if (Report.Line != 0)
    {
        std::printf(",\"line\":%zu,\"column\":%zu", Report.Line, Report.Column);
    }
    std::fputs("}\n", stdout);
}

bool CheckReference(bool bJson, StringView FilePath, FSceneEntityId Entity, const char* Field, EAssetLoadResult Result, const FAssetLoadDiagnostic& Diagnostic) noexcept
{
    if (Result == EAssetLoadResult::Success)
    {
        return true;
    }
    if (bJson)
    {
        WriteJsonReport(FilePath, {"asset_reference_error", "asset reference validation failed", Entity, Diagnostic.Id, Field});
    }
    else
    {
        std::fprintf(stderr, "%.*s: entity %s field %s: asset %s (%s), result %u, file %s, format %u\n", static_cast<int>(FilePath.Size()), FilePath.Data(), Entity.ToString().Data(), Field, Diagnostic.Id.ToString().Data(), Diagnostic.Path.Data(), static_cast<unsigned>(Result), DescribeFileSystemResult(Diagnostic.FileResult), static_cast<unsigned>(Diagnostic.FormatResult));
    }
    return false;
}

int ReportCacheFailure(bool bJson, StringView FilePath, const FCheckReport& Detail) noexcept
{
    if (bJson)
    {
        WriteJsonReport(FilePath, Detail);
    }
    else
    {
        std::fprintf(stderr, "SceneTool: %.*s\n", static_cast<int>(Detail.Message.Size()), Detail.Message.Data());
    }
    return 1;
}

struct FMeshMetadata
{
    bool bHasUVs = false;
    bool bHasNormals = false;
};

using FMeshMetadataMap = HashMap<FAssetId, FMeshMetadata>;

bool MeshHasNormals(const void* Context, FAssetId Id) noexcept
{
    const auto* Metadata = static_cast<const FMeshMetadataMap*>(Context)->Find(Id);
    return Metadata != nullptr && Metadata->bHasNormals;
}

int CheckScene(StringView FilePath, StringView MountName, StringView AbsoluteRoot, bool bJson) noexcept
{
    Array<uint8> Bytes;
    const auto FileResult = ReadContentFile(FilePath, MaxSceneDocumentBytes, Bytes);
    if (FileResult != EFileSystemResult::Success)
    {
        const char* const Message = DescribeFileSystemResult(FileResult);
        if (bJson)
        {
            WriteJsonReport(FilePath, {"scene_file_error", Message});
        }
        else
        {
            std::fprintf(stderr, "%.*s: %s\n", static_cast<int>(FilePath.Size()), FilePath.Data(), Message);
        }
        return 1;
    }

    FSceneDocument Document;
    FSceneDocumentDiagnostic DocumentDiagnostic;
    const auto DocumentResult = DecodeSceneDocument({reinterpret_cast<const char*>(Bytes.Data()), Bytes.Size()}, Document, &DocumentDiagnostic);
    if (DocumentResult != ESceneDocumentResult::Success)
    {
        const char* const Message = DescribeSceneDocumentResult(DocumentResult);
        if (bJson)
        {
            WriteJsonReport(FilePath, {"scene_document_error", Message, DocumentDiagnostic.Entity, {}, DocumentDiagnostic.Field, DocumentDiagnostic.Line, DocumentDiagnostic.Column});
        }
        else
        {
            const String Id = DocumentDiagnostic.Entity.IsValid() ? DocumentDiagnostic.Entity.ToString() : String("-");
            const StringView Field = DocumentDiagnostic.Field.IsEmpty() ? StringView("") : DocumentDiagnostic.Field;
            std::fprintf(stderr, "%.*s:%zu:%zu: %s (entity %s, field %.*s)\n", static_cast<int>(FilePath.Size()), FilePath.Data(), DocumentDiagnostic.Line, DocumentDiagnostic.Column, Message, Id.Data(), static_cast<int>(Field.Size()), Field.Data());
        }
        return 1;
    }

    const FAssetMount Mount{MountName, AbsoluteRoot};
    FAssetCatalog Catalog;
    FAssetLoadDiagnostic CatalogDiagnostic;
    if (Catalog.Rebuild({&Mount, 1}, &CatalogDiagnostic) != EAssetLoadResult::Success)
    {
        if (bJson)
        {
            WriteJsonReport(FilePath, {"asset_catalog_error", "asset catalog validation failed", {}, CatalogDiagnostic.Id});
        }
        else
        {
            std::fprintf(stderr, "%.*s: asset catalog validation failed at %s (result %u, file %s, format %u)\n", static_cast<int>(FilePath.Size()), FilePath.Data(), CatalogDiagnostic.Path.Data(), static_cast<unsigned>(CatalogDiagnostic.Result), DescribeFileSystemResult(CatalogDiagnostic.FileResult), static_cast<unsigned>(CatalogDiagnostic.FormatResult));
        }
        return 1;
    }

    FMeshMetadataMap Meshes;
    HashMap<FAssetId, FAssetId> MaterialTextures;
    FAssetLoadSession CpuTextures(Catalog);
    for (const auto& Entity : Document.Entities)
    {
        if (!Entity.bHasMesh)
        {
            continue;
        }
        FAssetLoadDiagnostic Diagnostic;
        FMeshMetadata Metadata;
        if (const auto* Cached = Meshes.Find(Entity.Mesh.Mesh))
        {
            Metadata = *Cached;
        }
        else
        {
            FMeshAssetData Mesh;
            if (!CheckReference(bJson, FilePath, Entity.Id, "mesh.mesh", Catalog.Decode(Entity.Mesh.Mesh, Mesh, &Diagnostic), Diagnostic))
            {
                return 1;
            }
            Metadata = {!Mesh.UVs.IsEmpty(), !Mesh.Normals.IsEmpty()};
            if (Meshes.TryInsert(Entity.Mesh.Mesh, Metadata) != EHashInsertResult::Inserted)
            {
                return ReportCacheFailure(bJson, FilePath, {"out_of_memory", "could not cache decoded mesh metadata", Entity.Id, Entity.Mesh.Mesh, "mesh.mesh"});
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
            if (!CheckReference(bJson, FilePath, Entity.Id, "mesh.material", Catalog.Decode(Entity.Mesh.Material, Material, &Diagnostic), Diagnostic))
            {
                return 1;
            }
            Texture = Material.BaseColorTexture;
            if (Texture.IsValid())
            {
                FCpuTextureAssetHandle Handle;
                if (!CheckReference(bJson, FilePath, Entity.Id, "mesh.material.baseColorTexture", CpuTextures.Load(Texture, Handle, &Diagnostic), Diagnostic))
                {
                    return 1;
                }
            }
            if (MaterialTextures.TryInsert(Entity.Mesh.Material, Texture) != EHashInsertResult::Inserted)
            {
                return ReportCacheFailure(bJson, FilePath, {"out_of_memory", "could not cache decoded material metadata", Entity.Id, Entity.Mesh.Material, "mesh.material"});
            }
        }
        if (Texture.IsValid() && !Metadata.bHasUVs)
        {
            if (bJson)
            {
                WriteJsonReport(FilePath, {"mesh_missing_uvs", "mesh has no UVs for its textured material", Entity.Id, Entity.Mesh.Mesh, "mesh.mesh"});
            }
            else
            {
                std::fprintf(stderr, "%.*s: entity %s mesh %s has no UVs for material %s and texture %s\n", static_cast<int>(FilePath.Size()), FilePath.Data(), Entity.Id.ToString().Data(), Entity.Mesh.Mesh.ToString().Data(), Entity.Mesh.Material.ToString().Data(), Texture.ToString().Data());
            }
            return 1;
        }
    }

    FSceneOperationDiagnostic Preflight;
    const auto PreflightResult = ValidateSceneMeshTransforms(Document, {&Meshes, &MeshHasNormals}, &Preflight);
    if (PreflightResult != ESceneOperationResult::Success)
    {
        const char* const Message = DescribeSceneOperationResult(PreflightResult);
        if (bJson)
        {
            const char* const Code = PreflightResult == ESceneOperationResult::InvalidNormalTransform ?
                "invalid_normal_transform" : "scene_preflight_error";
            WriteJsonReport(FilePath, {Code, Message, Preflight.Entity, Preflight.Asset, Preflight.Document.Field});
        }
        else
        {
            std::fprintf(stderr, "%.*s: %s", static_cast<int>(FilePath.Size()), FilePath.Data(), Message);
            if (Preflight.Entity.IsValid())
            {
                std::fprintf(stderr, ", entity %s", Preflight.Entity.ToString().Data());
            }
            if (Preflight.Asset.IsValid())
            {
                std::fprintf(stderr, ", asset %s", Preflight.Asset.ToString().Data());
            }
            if (!Preflight.Document.Field.IsEmpty())
            {
                std::fprintf(stderr, ", field %.*s", static_cast<int>(Preflight.Document.Field.Size()), Preflight.Document.Field.Data());
            }
            std::fputc('\n', stderr);
        }
        return 1;
    }

    if (bJson)
    {
        WriteJsonReport(FilePath, {"ok", "Scene valid"}, Document.Entities.Size());
    }
    else
    {
        std::printf("Scene valid: %.*s (%zu entities)\n", static_cast<int>(FilePath.Size()), FilePath.Data(), Document.Entities.Size());
    }
    return 0;
}

int ReportUsage(bool bJson) noexcept
{
    constexpr const char* Usage = "Usage: SceneTool check [--json] SCENE.json MOUNT ABSOLUTE_ASSET_ROOT";
    if (bJson)
    {
        WriteJsonReport({}, {"invalid_arguments", Usage});
    }
    else
    {
        std::fprintf(stderr, "%s\n", Usage);
    }
    return 2;
}

#if PLATFORM_WINDOWS
bool ToUtf8(const wchar_t* Wide, String& Out) noexcept
{
    const int ByteCount = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Wide, -1, nullptr, 0, nullptr, nullptr);
    Array<char> Buffer;
    return ByteCount > 0 && Buffer.TryResize(static_cast<std::size_t>(ByteCount)) && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Wide, -1, Buffer.Data(), ByteCount, nullptr, nullptr) == ByteCount && Out.TryAssign({Buffer.Data(), static_cast<std::size_t>(ByteCount - 1)});
}

int RunSceneTool(int Count, wchar_t** Args) noexcept
{
    String Command;
    String Option;
    String Path;
    String Mount;
    String Root;
    if (Count < 2 || !ToUtf8(Args[1], Command) || Command.View() != StringView("check"))
    {
        return ReportUsage(false);
    }
    const bool bJson = Count > 2 && ToUtf8(Args[2], Option) && Option.View() == StringView("--json");
    const int First = bJson ? 3 : 2;
    if (Count != First + 3 || !ToUtf8(Args[First], Path) || !ToUtf8(Args[First + 1], Mount) || !ToUtf8(Args[First + 2], Root))
    {
        return ReportUsage(bJson);
    }
    return CheckScene(Path.View(), Mount.View(), Root.View(), bJson);
}
#else
int RunSceneTool(int Count, char** Args) noexcept
{
    if (Count < 2 || StringView(Args[1]) != StringView("check"))
    {
        return ReportUsage(false);
    }
    const bool bJson = Count > 2 && StringView(Args[2]) == StringView("--json");
    const int First = bJson ? 3 : 2;
    if (Count != First + 3)
    {
        return ReportUsage(bJson);
    }
    return CheckScene(Args[First], Args[First + 1], Args[First + 2], bJson);
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
