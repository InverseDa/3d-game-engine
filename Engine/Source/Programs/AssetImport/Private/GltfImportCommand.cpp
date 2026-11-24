#include "GltfImportCommand.h"

#include "Asset/SoftAssetPath.h"
#include "AssetImport.h"
#include "Containers/HashSet.h"
#include "FileSystem/FileSystem.h"
#include "GltfImportPlatform.h"
#include "GltfSource.h"
#include "Scene/SceneDocument.h"

#include <charconv>
#include <cstdio>

namespace LE::Detail
{
namespace
{

constexpr std::size_t MaxPlanBytes = MaxSceneDocumentBytes + 24 * 1024 * 1024;
constexpr std::size_t MaxAssetRows = 16'384;

bool Fail(FGltfDiagnostic& Diagnostic, const char* Code, const char* Message, StringView File, StringView Selector = {}) noexcept
{
    Diagnostic.Code = Code;
    Diagnostic.Message = Message;
    (void)Diagnostic.File.TryAssign(File);
    (void)Diagnostic.Selector.TryAssign(Selector);
    return false;
}

std::size_t Utf8SequenceLength(StringView Text) noexcept
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

// Preserve names from a valid source and keep failure reports valid JSON even
// when a malformed source supplies ill-formed byte sequences.
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
            const std::size_t Length = Utf8SequenceLength(Text.Substr(Index, Text.Size() - Index));
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

void WriteDiagnosticFields(const FGltfDiagnostic& Diagnostic) noexcept
{
    std::fputs("\"code\":", stdout);
    WriteJsonString(Diagnostic.Code);
    std::fputs(",\"file\":", stdout);
    WriteJsonString(Diagnostic.File.View());
    std::fputs(",\"selector\":", stdout);
    WriteJsonString(Diagnostic.Selector.View());
    std::fputs(",\"message\":", stdout);
    WriteJsonString(Diagnostic.Message);
}

void WriteWarnings(Span<const FGltfDiagnostic> Warnings) noexcept
{
    std::fputs(",\"warnings\":[", stdout);
    bool First = true;
    for (const FGltfDiagnostic& Warning : Warnings)
    {
        if (!First)
        {
            std::fputc(',', stdout);
        }
        First = false;
        std::fputc('{', stdout);
        WriteDiagnosticFields(Warning);
        std::fputc('}', stdout);
    }
    std::fputc(']', stdout);
}

int ReportResult(bool Success, const FGltfDiagnostic& Diagnostic, Span<const FGltfDiagnostic> Warnings = {}) noexcept
{
    std::printf("{\"ok\":%s,", Success ? "true" : "false");
    WriteDiagnosticFields(Diagnostic);
    WriteWarnings(Warnings);
    std::fputs("}\n", stdout);
    return Success ? 0 : 1;
}

void WriteSourceIndex(uint32 Index) noexcept
{
    if (Index == GltfNoIndex)
    {
        std::fputs("null", stdout);
    }
    else
    {
        std::printf("%u", static_cast<unsigned>(Index));
    }
}

void WriteVector(const Math::Vector3f& Vector) noexcept
{
    std::printf("[%.9g,%.9g,%.9g]", static_cast<double>(Vector[0]), static_cast<double>(Vector[1]), static_cast<double>(Vector[2]));
}

bool WriteInspection(const FGltfSource& Source, StringView Digest, FGltfDiagnostic& Diagnostic) noexcept
{
    const auto Primitives = Source.Primitives();
    HashMap<uint32, Span<const FGltfPrimitive>> ByMesh;
    for (std::size_t Start = 0; Start < Primitives.Size();)
    {
        const uint32 Mesh = Primitives[Start].Mesh;
        std::size_t End = Start + 1;
        while (End < Primitives.Size() && Primitives[End].Mesh == Mesh)
        {
            ++End;
        }
        if (ByMesh.TryInsert(Mesh, Primitives.Subspan(Start, End - Start)) != EHashInsertResult::Inserted)
        {
            return Fail(Diagnostic, "inspect_mesh_index_failed", "Could not build the primitive lookup.", {});
        }
        Start = End;
    }

    const auto Nodes = Source.Nodes();
    std::size_t EntityCount = Nodes.Size();
    for (const auto& Node : Nodes)
    {
        if (const auto* Mesh = ByMesh.Find(Node.Mesh))
        {
            EntityCount += Mesh->Size();
            if (EntityCount > MaxSceneEntities)
            {
                return Fail(Diagnostic, "scene_entity_limit", "The nodes and drawable instances exceed the Scene entity limit.", {});
            }
        }
    }

    std::fputs("{\"ok\":true,\"code\":\"ok\",\"sourceDocumentSha256\":", stdout);
    WriteJsonString(Digest);
    std::fputs(",\"nodes\":[", stdout);
    for (std::size_t Index = 0; Index < Nodes.Size(); ++Index)
    {
        const FGltfNode& Node = Nodes[Index];
        std::fputs(Index == 0 ? "{\"index\":" : ",{\"index\":", stdout);
        WriteSourceIndex(Node.Index);
        std::fputs(",\"parent\":", stdout);
        WriteSourceIndex(Node.Parent);
        std::fputs(",\"name\":", stdout);
        WriteJsonString(Node.Name.View());
        std::fputs(",\"transform\":{\"translation\":", stdout);
        WriteVector(Node.Translation);
        std::printf(",\"rotation\":[%.9g,%.9g,%.9g,%.9g],\"scale\":", static_cast<double>(Node.Rotation.x), static_cast<double>(Node.Rotation.y), static_cast<double>(Node.Rotation.z), static_cast<double>(Node.Rotation.w));
        WriteVector(Node.Scale);
        std::fputs("},\"primitives\":[", stdout);
        if (const auto* Mesh = ByMesh.Find(Node.Mesh))
        {
            for (std::size_t PrimitiveIndex = 0; PrimitiveIndex < Mesh->Size(); ++PrimitiveIndex)
            {
                const auto& Primitive = (*Mesh)[PrimitiveIndex];
                std::fputs(PrimitiveIndex == 0 ? "{\"mesh\":" : ",{\"mesh\":", stdout);
                WriteSourceIndex(Primitive.Mesh);
                std::fputs(",\"primitive\":", stdout);
                WriteSourceIndex(Primitive.Index);
                std::fputs(",\"material\":", stdout);
                WriteSourceIndex(Primitive.Material);
                std::fputc('}', stdout);
            }
        }
        std::fputs("]}", stdout);
    }
    std::fputs("],\"materials\":[", stdout);
    const auto Materials = Source.Materials();
    for (std::size_t Index = 0; Index < Materials.Size(); ++Index)
    {
        const auto& Material = Materials[Index];
        std::fputs(Index == 0 ? "{\"index\":" : ",{\"index\":", stdout);
        WriteSourceIndex(Material.Index);
        std::fputs(",\"name\":", stdout);
        WriteJsonString(Material.Name.View());
        std::fputs(",\"image\":", stdout);
        WriteSourceIndex(Material.Image);
        std::fputc('}', stdout);
    }
    std::fputs("],\"images\":[", stdout);
    const auto Images = Source.Images();
    for (std::size_t Index = 0; Index < Images.Size(); ++Index)
    {
        const auto& Image = Images[Index];
        std::fputs(Index == 0 ? "{\"index\":" : ",{\"index\":", stdout);
        WriteSourceIndex(Image.Index);
        std::fputs(",\"name\":", stdout);
        WriteJsonString(Image.Name.View());
        std::fputc('}', stdout);
    }
    std::fputc(']', stdout);
    WriteWarnings(Source.Warnings());
    std::fputs("}\n", stdout);
    return true;
}

bool ParseIndex(StringView Text, uint32& Index) noexcept
{
    if (Text.IsEmpty())
    {
        return false;
    }
    const char* End = Text.Data() + Text.Size();
    const auto Result = std::from_chars(Text.Data(), End, Index);
    return Result.ec == std::errc{} && Result.ptr == End && Index != GltfNoIndex;
}

bool SplitOnce(StringView& Remaining, char Delimiter, StringView& First) noexcept
{
    for (std::size_t Index = 0; Index < Remaining.Size(); ++Index)
    {
        if (Remaining[Index] == Delimiter)
        {
            First = Remaining.Substr(0, Index);
            Remaining = Remaining.Substr(Index + 1, Remaining.Size() - Index - 1);
            return true;
        }
    }
    return false;
}

bool ReadPlan(Array<char>& Plan) noexcept
{
    if (!SetBinaryInput())
    {
        return false;
    }
    constexpr std::size_t ChunkSize = 16 * 1024;
    while (Plan.Size() < MaxPlanBytes)
    {
        const std::size_t Start = Plan.Size();
        if (!Plan.TryResize(Start + ChunkSize))
        {
            return false;
        }
        const std::size_t Size = std::fread(Plan.Data() + Start, 1, ChunkSize, stdin);
        if (Size < ChunkSize)
        {
            return std::ferror(stdin) == 0 && Plan.TryResize(Start + Size);
        }
    }
    return std::fgetc(stdin) == EOF && std::ferror(stdin) == 0;
}

enum class EAssetKind : uint8
{
    Mesh,
    Material,
    Texture,
};

struct FAssetBinding
{
    EAssetKind Kind = EAssetKind::Mesh;
    uint32 Index = GltfNoIndex;
    uint32 Primitive = GltfNoIndex;
    FAssetId Dependency;
    FAssetId Id;
    StringView Path;
};

bool ValidatePath(StringView Path, StringView Mount) noexcept
{
    FSoftAssetPath Canonical;
    return !Mount.IsEmpty() && Path.Size() > Mount.Size() + 1 && Path.Size() <= MaxAssetPathBytes &&
           Path.Substr(0, Mount.Size()) == Mount && Path[Mount.Size()] == '/' &&
           FSoftAssetPath::TryParse(Path, Canonical) == ESoftAssetPathResult::Success && Canonical.View() == Path;
}

bool ParseAssetBinding(StringView Kind, StringView Remaining, StringView Mount, FAssetBinding& Binding) noexcept
{
    StringView Index;
    StringView Secondary;
    StringView Id;
    if (!SplitOnce(Remaining, '\t', Index) || !SplitOnce(Remaining, '\t', Secondary) || !SplitOnce(Remaining, '\t', Id) ||
        !FAssetId::TryParse(Id, Binding.Id) || !ValidatePath(Remaining, Mount))
    {
        return false;
    }
    Binding.Path = Remaining;
    if (Kind == StringView("mesh"))
    {
        Binding.Kind = EAssetKind::Mesh;
        return ParseIndex(Index, Binding.Index) && ParseIndex(Secondary, Binding.Primitive);
    }
    if (Kind == StringView("material"))
    {
        Binding.Kind = EAssetKind::Material;
        return (Index == StringView("-1") || ParseIndex(Index, Binding.Index)) && (Secondary == StringView("-") || FAssetId::TryParse(Secondary, Binding.Dependency));
    }
    if (Kind == StringView("texture"))
    {
        Binding.Kind = EAssetKind::Texture;
        return ParseIndex(Index, Binding.Index) && Secondary == StringView("-");
    }
    return false;
}

bool ParseRecipe(StringView Plan, StringView Digest, StringView Mount, StringView Source, Array<FAssetBinding>& Bindings, StringView& Scene, FGltfDiagnostic& Diagnostic) noexcept
{
    StringView ExpectedDigest;
    if (!SplitOnce(Plan, '\n', ExpectedDigest) || ExpectedDigest != Digest)
    {
        return Fail(Diagnostic, "source_digest_mismatch", "The approved source digest differs from the inspected document. Inspect and review source bindings again.", Source);
    }
    HashSet<FAssetId> Ids;
    HashSet<StringView> Paths;
    StringView Row;
    while (SplitOnce(Plan, '\n', Row))
    {
        StringView Kind;
        if (!SplitOnce(Row, '\t', Kind))
        {
            return Fail(Diagnostic, "invalid_import_recipe", "Expected a tab-delimited asset or scene record.", Source);
        }
        if (Kind == StringView("scene"))
        {
            if (!Plan.IsEmpty() || Row.IsEmpty())
            {
                return Fail(Diagnostic, "invalid_import_recipe", "The scene JSON must be the last record.", Source);
            }
            Scene = Row;
            return true;
        }
        FAssetBinding Binding;
        if (Bindings.Size() >= MaxAssetRows || !ParseAssetBinding(Kind, Row, Mount, Binding))
        {
            return Fail(Diagnostic, "invalid_import_recipe", "An asset row is invalid or exceeds the import limit.", Source, Kind);
        }
        if (Ids.TryInsert(Binding.Id) != EHashInsertResult::Inserted || Paths.TryInsert(Binding.Path) != EHashInsertResult::Inserted)
        {
            return Fail(Diagnostic, "duplicate_asset_binding", "Could not register the asset ID and canonical output path uniquely.", Source, Binding.Path);
        }
        if (!Bindings.TryPushBack(Binding))
        {
            return Fail(Diagnostic, "out_of_memory", "Could not allocate the asset bindings.", Source);
        }
    }
    return Fail(Diagnostic, "invalid_import_recipe", "The recipe is missing a terminated scene record.", Source);
}

bool CookAsset(const FGltfSource& Source, const FAssetBinding& Binding, StringView SourcePath, Array<uint8>& Bytes, FGltfDiagnostic& Diagnostic) noexcept
{
    EAssetFormatResult Result;
    switch (Binding.Kind)
    {
    case EAssetKind::Mesh:
    {
        FMeshAssetData Mesh;
        if (!Source.DecodeMesh(Binding.Index, Binding.Primitive, Mesh, Diagnostic))
        {
            return false;
        }
        Result = EncodeAssetFile(Binding.Id, Binding.Path, Mesh, Bytes);
        break;
    }
    case EAssetKind::Material:
    {
        FMaterialAssetData Material;
        if (!Source.DecodeMaterial(Binding.Index, Binding.Dependency, Material, Diagnostic))
        {
            return false;
        }
        Result = EncodeAssetFile(Binding.Id, Binding.Path, Material, Bytes);
        break;
    }
    case EAssetKind::Texture:
    {
        FCpuTextureAsset Texture;
        if (!Source.DecodeTexture(Binding.Index, Texture, Diagnostic))
        {
            return false;
        }
        Result = EncodeAssetFile(Binding.Id, Binding.Path, Texture, Bytes);
        break;
    }
    default:
        return Fail(Diagnostic, "invalid_import_recipe", "Unsupported cooked asset kind.", SourcePath, Binding.Path);
    }
    return Result == EAssetFormatResult::Success || Fail(Diagnostic, "asset_encode_failed", DescribeImportFormatError(Result), SourcePath, Binding.Path);
}

bool WriteBytes(StringView Output, Span<const uint8> Data, FGltfDiagnostic& Diagnostic) noexcept
{
    const EFileSystemResult Write = WriteContentFileAtomically(Output, Data);
    return Write == EFileSystemResult::Success || Fail(Diagnostic, "candidate_write_failed", DescribeFileSystemResult(Write), Output);
}

bool RunCook(const FGltfSource& Source, StringView Digest, StringView SourcePath, StringView Mount, StringView CandidateRoot, FGltfDiagnostic& Diagnostic) noexcept
{
    Array<char> Plan;
    if (!ReadPlan(Plan))
    {
        return Fail(Diagnostic, "import_recipe_read_failed", "Could not read the bounded UTF-8 import recipe.", SourcePath);
    }
    Array<FAssetBinding> Bindings;
    StringView Scene;
    if (!ParseRecipe({Plan.Data(), Plan.Size()}, Digest, Mount, SourcePath, Bindings, Scene, Diagnostic))
    {
        return false;
    }
    FSceneDocument Document;
    FSceneDocumentDiagnostic SceneDiagnostic;
    const auto Decode = DecodeSceneDocument(Scene, Document, &SceneDiagnostic);
    if (Decode != ESceneDocumentResult::Success)
    {
        return Fail(Diagnostic, "generated_scene_invalid", DescribeSceneDocumentResult(Decode), SourcePath, SceneDiagnostic.Field);
    }
    String SceneText;
    const auto Encode = EncodeSceneDocument(Document, SceneText, &SceneDiagnostic);
    if (Encode != ESceneDocumentResult::Success)
    {
        return Fail(Diagnostic, "generated_scene_encode_failed", DescribeSceneDocumentResult(Encode), SourcePath, SceneDiagnostic.Field);
    }
    Array<uint8> Bytes;
    for (const auto& Binding : Bindings)
    {
        if (!CookAsset(Source, Binding, SourcePath, Bytes, Diagnostic))
        {
            return false;
        }
        if (!CandidateRoot.IsEmpty())
        {
            const StringView RelativePath = Binding.Path.Substr(Mount.Size() + 1, Binding.Path.Size() - Mount.Size() - 1);
            String Output;
            if (!Output.TryAssign(CandidateRoot) || !Output.TryAppend("/Assets/") || !Output.TryAppend(RelativePath) || !Output.TryAppend(".leasset"))
            {
                return Fail(Diagnostic, "out_of_memory", "Could not build the cooked asset output path.", SourcePath, Binding.Path);
            }
            if (!WriteBytes(Output.View(), Bytes.AsSpan(), Diagnostic))
            {
                return false;
            }
        }
    }
    if (CandidateRoot.IsEmpty())
    {
        return true;
    }
    String Output;
    if (!Output.TryAssign(CandidateRoot) || !Output.TryAppend("/Scene.json"))
    {
        return Fail(Diagnostic, "out_of_memory", "Could not build the generated scene output path.", CandidateRoot);
    }
    return WriteBytes(Output.View(), {reinterpret_cast<const uint8*>(SceneText.Data()), SceneText.Size()}, Diagnostic);
}

} // namespace

int RunGltfImportCommand(Span<const StringView> Arguments) noexcept
{
    FGltfDiagnostic Diagnostic;
    if (Arguments.Size() == 4 && Arguments[0] == StringView("publish"))
    {
        const bool Success = PublishGeneration(Arguments[1], Arguments[2], Arguments[3], Diagnostic);
        return ReportResult(Success, Diagnostic);
    }

    const bool Inspect = Arguments.Size() == 4 && Arguments[0] == StringView("inspect");
    const bool Check = Arguments.Size() == 5 && Arguments[0] == StringView("check");
    const bool Cook = Arguments.Size() == 6 && Arguments[0] == StringView("cook") && !Arguments[5].IsEmpty();
    uint32 Scene;
    if ((!Inspect && !Check && !Cook) || !ParseIndex(Arguments[3], Scene))
    {
        (void)Fail(Diagnostic, "invalid_arguments", "Expected gltf inspect ROOT SOURCE SCENE, check ROOT SOURCE SCENE MOUNT, cook ROOT SOURCE SCENE MOUNT CANDIDATE, or publish ROOT BASELINE CANDIDATE.", {});
        return ReportResult(false, Diagnostic);
    }
    FGltfSource Source;
    if (!Source.Open(Arguments[1], Arguments[2], Scene, Diagnostic))
    {
        return ReportResult(false, Diagnostic, Source.Warnings());
    }
    char Hash[DocumentDigestHexLength];
    if (!HashDocument(Source.DocumentBytes(), Hash))
    {
        (void)Fail(Diagnostic, "digest_failed", "Could not calculate the source document SHA-256.", Arguments[2]);
        return ReportResult(false, Diagnostic, Source.Warnings());
    }
    const StringView Digest(Hash, sizeof(Hash));
    if (Inspect)
    {
        return WriteInspection(Source, Digest, Diagnostic) ? 0 : ReportResult(false, Diagnostic, Source.Warnings());
    }
    const bool Success = RunCook(Source, Digest, Arguments[2], Arguments[4], Cook ? Arguments[5] : StringView{}, Diagnostic);
    return ReportResult(Success, Diagnostic, Source.Warnings());
}

} // namespace LE::Detail
