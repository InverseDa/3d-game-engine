#include "GltfSource.h"

#include "FileSystem/FileSystem.h"
#include "cgltf.h"
#include "lodepng.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace LE::Detail
{
namespace
{

constexpr std::size_t MaxDocumentBytes = 8 * 1024 * 1024;
constexpr std::size_t MaxParserBytes = 64 * 1024 * 1024;
constexpr std::size_t MaxBufferBytes = 128 * 1024 * 1024;
constexpr std::size_t MaxTotalBufferBytes = 192 * 1024 * 1024;
constexpr std::size_t MaxPngBytes = 64 * 1024 * 1024;
constexpr std::size_t MaxElements = 65536;
constexpr std::size_t MaxNameBytes = 1024;

struct alignas(std::max_align_t) FAllocationHeader
{
    std::size_t Size;
};

// cgltf uses these callbacks for its parse-only graph. Its storage is kept
// privately and freed through the same callbacks before discarding the input.
struct FParserBudget
{
    std::size_t Bytes = 0;

    static void* Allocate(void* Context, cgltf_size Size) noexcept
    {
        auto& Budget = *static_cast<FParserBudget*>(Context);
        if (Size > MaxParserBytes - Budget.Bytes || sizeof(FAllocationHeader) > MaxParserBytes - Budget.Bytes - Size)
        {
            return nullptr;
        }
        const std::size_t Total = Size + sizeof(FAllocationHeader);
        auto* const Header = static_cast<FAllocationHeader*>(GetDefaultAllocator().Allocate(Total, alignof(FAllocationHeader)));
        if (Header == nullptr)
        {
            return nullptr;
        }
        Header->Size = Total;
        Budget.Bytes += Total;
        return Header + 1;
    }

    static void Free(void* Context, void* Address) noexcept
    {
        if (Address != nullptr)
        {
            auto& Budget = *static_cast<FParserBudget*>(Context);
            auto* const Header = static_cast<FAllocationHeader*>(Address) - 1;
            Budget.Bytes -= Header->Size;
            GetDefaultAllocator().Deallocate(Header, Header->Size, alignof(FAllocationHeader));
        }
    }
};

struct FPngDecode
{
    LodePNGState State;
    uint8* Pixels = nullptr;

    FPngDecode() noexcept { lodepng_state_init(&State); }
    ~FPngDecode()
    {
        std::free(Pixels);
        lodepng_state_cleanup(&State);
    }
};

struct FElementSelector
{
    char Text[96]{};

    FElementSelector(const char* Collection, uint32 Index, uint32 Primitive = GltfNoIndex) noexcept
    {
        if (Index == GltfNoIndex)
        {
            std::snprintf(Text, sizeof(Text), "/%s/default", Collection);
        }
        else if (Primitive == GltfNoIndex)
        {
            std::snprintf(Text, sizeof(Text), "/%s/%u", Collection, Index);
        }
        else
        {
            std::snprintf(Text, sizeof(Text), "/%s/%u/primitives/%u", Collection, Index, Primitive);
        }
    }
};

bool Fail(FGltfDiagnostic& Out, StringView File, StringView Selector, const char* Code, const char* Message) noexcept
{
    Out.Code = Code;
    Out.Message = Message;
    if (!Out.File.TryAssign(File) || !Out.Selector.TryAssign(Selector))
    {
        Out.Code = "out_of_memory";
        Out.Message = "could not allocate glTF diagnostic context";
    }
    return false;
}

bool SuffixEquals(StringView Value, StringView Suffix) noexcept
{
    if (Value.Size() < Suffix.Size())
    {
        return false;
    }
    const std::size_t Start = Value.Size() - Suffix.Size();
    for (std::size_t Index = 0; Index < Suffix.Size(); ++Index)
    {
        char Character = Value[Start + Index];
        if (Character >= 'A' && Character <= 'Z')
        {
            Character = static_cast<char>(Character - 'A' + 'a');
        }
        if (Character != Suffix[Index])
        {
            return false;
        }
    }
    return true;
}

int HexDigit(char Character) noexcept
{
    if (Character >= '0' && Character <= '9')
    {
        return Character - '0';
    }
    if (Character >= 'A' && Character <= 'F')
    {
        return Character - 'A' + 10;
    }
    return Character >= 'a' && Character <= 'f' ? Character - 'a' + 10 : -1;
}

// Decode one local relative URI. FileSystem validates the decoded segments
// below the import root, including traversal and redirected paths.
bool DecodeUri(StringView Uri, String& Out) noexcept
{
    if (Uri.IsEmpty() || Uri.Size() > MaxAssetPathBytes || Uri[0] == '/')
    {
        return false;
    }
    for (std::size_t Index = 0; Index < Uri.Size(); ++Index)
    {
        unsigned char Character = static_cast<unsigned char>(Uri[Index]);
        if (Character == '%')
        {
            if (Uri.Size() - Index < 3)
            {
                return false;
            }
            const int High = HexDigit(Uri[Index + 1]);
            const int Low = HexDigit(Uri[Index + 2]);
            if (High < 0 || Low < 0)
            {
                return false;
            }
            Character = static_cast<unsigned char>((High << 4) | Low);
            Index += 2;
        }
        const char Byte = static_cast<char>(Character);
        if (Character < 32 || Character == 127 || Character == '\\' || Character == ':' || Character == '?' || Character == '#' ||
            (Out.IsEmpty() && Character == '/') || !Out.TryAppend(StringView(&Byte, 1)))
        {
            return false;
        }
    }
    return !Out.IsEmpty();
}

bool HasBoundedJsonRoot(Span<const uint8> Bytes) noexcept
{
    // Bound depth before cgltf descends through skipped extras and other
    // JSON subtrees. The parser still owns token and glTF schema decoding.
    constexpr unsigned MaxDepth = 128;
    unsigned Depth = 0;
    bool bStarted = false;
    bool bQuoted = false;
    bool bEscaped = false;
    for (const uint8 Byte : Bytes)
    {
        if (Byte == 0)
        {
            return false;
        }
        if (bQuoted)
        {
            if (!bEscaped && Byte == '"')
            {
                bQuoted = false;
            }
            bEscaped = !bEscaped && Byte == '\\';
            continue;
        }
        if (Byte == ' ' || Byte == '\t' || Byte == '\r' || Byte == '\n')
        {
            continue;
        }
        if (Depth == 0)
        {
            if (bStarted || Byte != '{')
            {
                return false;
            }
            bStarted = true;
        }
        if (Byte == '{' || Byte == '[')
        {
            if (++Depth > MaxDepth)
            {
                return false;
            }
        }
        else if (Byte == '}' || Byte == ']')
        {
            --Depth;
        }
        else if (Byte == '"')
        {
            bQuoted = true;
        }
    }
    return bStarted && Depth == 0 && !bQuoted;
}

template <typename T> uint32 SourceIndex(const T* Value, const T* Base) noexcept
{
    return Value == nullptr ? GltfNoIndex : static_cast<uint32>(Value - Base);
}

bool CopyName(char* Name, String& Out) noexcept
{
    if (Name == nullptr)
    {
        return true;
    }
    // cgltf provides the raw JSON string. Decode a private copy because a
    // resource can be reached by several nodes or appear in multiple lookups.
    const StringView Encoded(Name);
    if (Encoded.Size() > MaxNameBytes * 6)
    {
        return false;
    }
    Array<char> Buffer;
    if (!Buffer.TryResize(Encoded.Size() + 1))
    {
        return false;
    }
    std::memcpy(Buffer.Data(), Name, Buffer.Size());
    const std::size_t Length = cgltf_decode_string(Buffer.Data());
    return Length <= MaxNameBytes && Out.TryAssign(StringView(Buffer.Data(), Length));
}

bool IsFiniteVector(Math::Vector3f Value) noexcept
{
    return std::isfinite(Value[0]) && std::isfinite(Value[1]) && std::isfinite(Value[2]);
}

float FlipAxis(float Value) noexcept
{
    return Value == 0.0f ? 0.0f : -Value;
}

bool ConvertNode(const cgltf_node& Source, FGltfNode& Out) noexcept
{
    Out.Translation = Math::Vector3f{FlipAxis(Source.translation[0]), Source.translation[1], FlipAxis(Source.translation[2])};
    Out.Scale = Math::Vector3f{Source.scale[0], Source.scale[1], Source.scale[2]};
    const double X = Source.rotation[0];
    const double Y = Source.rotation[1];
    const double Z = Source.rotation[2];
    const double W = Source.rotation[3];
    const double SquaredLength = X * X + Y * Y + Z * Z + W * W;
    if (!IsFiniteVector(Out.Translation) || !IsFiniteVector(Out.Scale) ||
        !std::isfinite(SquaredLength) || std::fabs(SquaredLength - 1.0) > 1e-3)
    {
        return false;
    }
    const double Scale = 1.0 / std::sqrt(SquaredLength);
    Out.Rotation = Math::Quaternionf(FlipAxis(static_cast<float>(X * Scale)), static_cast<float>(Y * Scale),
        FlipAxis(static_cast<float>(Z * Scale)), static_cast<float>(W * Scale));
    return true;
}

struct FMeshAccessors
{
    const cgltf_accessor* Positions = nullptr;
    const cgltf_accessor* Normals = nullptr;
    const cgltf_accessor* Uvs = nullptr;
    const cgltf_accessor* Indices = nullptr;
    bool bIgnoredAttributes = false;
};

bool ReadMeshAccessors(const cgltf_primitive& Primitive, bool bTextured, FMeshAccessors& Out) noexcept
{
    if (Primitive.type != cgltf_primitive_type_triangles || Primitive.indices == nullptr ||
        Primitive.targets_count != 0 || Primitive.has_draco_mesh_compression || Primitive.mappings_count != 0 ||
        Primitive.extensions_count != 0)
    {
        return false;
    }
    for (cgltf_size Index = 0; Index < Primitive.attributes_count; ++Index)
    {
        const auto& Attribute = Primitive.attributes[Index];
        const cgltf_accessor** Destination = nullptr;
        if (Attribute.type == cgltf_attribute_type_position)
        {
            Destination = &Out.Positions;
        }
        else if (Attribute.type == cgltf_attribute_type_normal)
        {
            Destination = &Out.Normals;
        }
        else if (Attribute.type == cgltf_attribute_type_texcoord && Attribute.index == 0)
        {
            Destination = &Out.Uvs;
        }
        if (Destination == nullptr)
        {
            Out.bIgnoredAttributes = true;
        }
        else if (*Destination != nullptr || Attribute.data == nullptr)
        {
            return false;
        }
        else
        {
            *Destination = Attribute.data;
        }
    }
    Out.Indices = Primitive.indices;
    return Out.Positions != nullptr && Out.Normals != nullptr && (!bTextured || Out.Uvs != nullptr) &&
        Out.Normals->count == Out.Positions->count && (Out.Uvs == nullptr || Out.Uvs->count == Out.Positions->count) &&
        Out.Indices->count % 3 == 0;
}

std::size_t IndexSize(cgltf_component_type Type) noexcept
{
    switch (Type)
    {
    case cgltf_component_type_r_8u: return 1;
    case cgltf_component_type_r_16u: return 2;
    case cgltf_component_type_r_32u: return 4;
    default: return 0;
    }
}

bool ValidateAccessor(const cgltf_accessor& Accessor, bool bIndex, cgltf_type Type, std::size_t ElementBytes) noexcept
{
    const auto* const View = Accessor.buffer_view;
    const std::size_t MaxCount = bIndex ? MaxAssetMeshIndices : MaxAssetMeshVertices;
    const std::size_t Alignment = bIndex ? ElementBytes : sizeof(float);
    if (ElementBytes == 0 || Accessor.type != Type || Accessor.count == 0 || Accessor.count > MaxCount ||
        Accessor.is_sparse || Accessor.normalized || Accessor.extensions_count != 0 || View == nullptr ||
        View->buffer == nullptr || View->has_meshopt_compression || View->extensions_count != 0 ||
        (!bIndex && Accessor.component_type != cgltf_component_type_r_32f) ||
        (bIndex && View->stride != 0) ||
        View->offset % Alignment != 0 || Accessor.offset % Alignment != 0 ||
        View->offset > View->buffer->size || View->size > View->buffer->size - View->offset ||
        Accessor.offset > View->size || ElementBytes > View->size - Accessor.offset)
    {
        return false;
    }
    const std::size_t Stride = View->stride == 0 ? ElementBytes : View->stride;
    if (Stride < ElementBytes || Stride % Alignment != 0 || (!bIndex && Stride > 252))
    {
        return false;
    }
    return Accessor.count - 1 <= (View->size - Accessor.offset - ElementBytes) / Stride;
}

struct FAccessorBytes
{
    const uint8* Data;
    std::size_t Stride;

    const uint8* operator[](std::size_t Index) const noexcept { return Data + Index * Stride; }
};

float ReadFloat(const uint8* Bytes) noexcept
{
    const uint32 Word = static_cast<uint32>(Bytes[0]) | (static_cast<uint32>(Bytes[1]) << 8) |
        (static_cast<uint32>(Bytes[2]) << 16) | (static_cast<uint32>(Bytes[3]) << 24);
    float Value;
    static_assert(sizeof(Value) == sizeof(Word));
    std::memcpy(&Value, &Word, sizeof(Value));
    return Value;
}

uint32 ReadIndex(const uint8* Bytes, std::size_t Size) noexcept
{
    uint32 Result = 0;
    for (std::size_t Offset = 0; Offset < Size; ++Offset)
    {
        Result |= static_cast<uint32>(Bytes[Offset]) << (8 * Offset);
    }
    return Result;
}

Math::Vector3f ReadVector(const uint8* Bytes) noexcept
{
    return Math::Vector3f{FlipAxis(ReadFloat(Bytes)), ReadFloat(Bytes + 4), FlipAxis(ReadFloat(Bytes + 8))};
}

bool NormalizeNormal(Math::Vector3f& Normal) noexcept
{
    const double X = Normal[0];
    const double Y = Normal[1];
    const double Z = Normal[2];
    const double Length = std::sqrt(X * X + Y * Y + Z * Z);
    if (!std::isfinite(Length) || Length == 0.0)
    {
        return false;
    }
    Normal = Math::Vector3f{static_cast<float>(X / Length), static_cast<float>(Y / Length), static_cast<float>(Z / Length)};
    return true;
}

} // namespace

struct FGltfSource::FState
{
    ~FState()
    {
        if (Data != nullptr)
        {
            cgltf_free(Data);
        }
    }

    bool Parse(StringView AbsoluteRoot, StringView RelativeGltf, uint32 SceneIndex, FGltfDiagnostic& Diagnostic) noexcept;
    bool CollectScene(const cgltf_scene& Scene, FGltfDiagnostic& Diagnostic) noexcept;
    bool CollectMesh(uint32 Index, FGltfDiagnostic& Diagnostic) noexcept;
    bool CollectMaterial(uint32 Index, FGltfDiagnostic& Diagnostic) noexcept;
    bool CollectImage(uint32 Index, FGltfDiagnostic& Diagnostic) noexcept;
    bool LoadBuffer(const cgltf_buffer& Buffer, StringView Selector, FGltfDiagnostic& Diagnostic) noexcept;
    bool ResolveSidecar(StringView Uri, StringView Suffix, StringView Selector, String& RelativePath, String& AbsolutePath,
        FGltfDiagnostic& Diagnostic) const noexcept;
    FAccessorBytes MakeAccessor(const cgltf_accessor& Accessor, std::size_t Width) const noexcept;
    bool Warn(StringView Selector, const char* Code, const char* Message, FGltfDiagnostic& Diagnostic) noexcept;

    String Root;
    String RelativeFile;
    String RelativeDirectory;
    Array<uint8> Document;
    FParserBudget ParserBudget;
    cgltf_data* Data = nullptr;
    std::size_t BufferBytes = 0;
    Array<Array<uint8>> Buffers;
    Array<FGltfNode> NodeEntries;
    Array<FGltfPrimitive> MeshEntries;
    Array<FGltfMaterial> MaterialEntries;
    Array<FGltfImage> ImageEntries;
    Array<FGltfDiagnostic> WarningEntries;
    Array<uint8> ReachableMeshes;
    Array<uint8> ReachableMaterials;
    Array<uint8> ReachableImages;
    bool bDefaultMaterial = false;
};

bool FGltfSource::FState::Warn(StringView Selector, const char* Code, const char* Message, FGltfDiagnostic& Diagnostic) noexcept
{
    FGltfDiagnostic Warning;
    Warning.Code = Code;
    Warning.Message = Message;
    if (!Warning.File.TryAssign(RelativeFile) || !Warning.Selector.TryAssign(Selector) || !WarningEntries.TryPushBack(std::move(Warning)))
    {
        return Fail(Diagnostic, RelativeFile, Selector, "out_of_memory", "could not store a glTF warning");
    }
    return true;
}

bool FGltfSource::FState::Parse(StringView AbsoluteRoot, StringView RelativeGltf, uint32 SceneIndex, FGltfDiagnostic& Diagnostic) noexcept
{
    if (!SuffixEquals(RelativeGltf, ".gltf"))
    {
        return Fail(Diagnostic, RelativeGltf, "/", "unsupported_container", "static import requires a .gltf document");
    }
    if (!Root.TryAssign(AbsoluteRoot) || !RelativeFile.TryAssign(RelativeGltf))
    {
        return Fail(Diagnostic, RelativeGltf, "/", "out_of_memory", "could not store the source path");
    }
    for (std::size_t End = RelativeGltf.Size(); End != 0; --End)
    {
        if (RelativeGltf[End - 1] == '/' || RelativeGltf[End - 1] == '\\')
        {
            if (!RelativeDirectory.TryAssign(RelativeGltf.Substr(0, End)))
            {
                return Fail(Diagnostic, RelativeGltf, "/", "out_of_memory", "could not store the source directory");
            }
            break;
        }
    }
    String Path;
    if (const auto Result = ResolveContentFile(Root, RelativeFile, Path); Result != EFileSystemResult::Success)
    {
        return Fail(Diagnostic, RelativeFile, "/", "source_path", DescribeFileSystemResult(Result));
    }
    if (const auto Result = ReadContentFile(Path, MaxDocumentBytes, Document); Result != EFileSystemResult::Success)
    {
        return Fail(Diagnostic, RelativeFile, "/", "source_read", DescribeFileSystemResult(Result));
    }
    if (!HasBoundedJsonRoot(Document.AsSpan()))
    {
        return Fail(Diagnostic, RelativeFile, "/", "invalid_gltf", "expected a single JSON object with bounded nesting and no NUL bytes");
    }
    cgltf_options Options{};
    Options.type = cgltf_file_type_gltf;
    Options.memory.alloc_func = &FParserBudget::Allocate;
    Options.memory.free_func = &FParserBudget::Free;
    Options.memory.user_data = &ParserBudget;
    const auto ParseResult = cgltf_parse(&Options, Document.Data(), Document.Size(), &Data);
    if (ParseResult != cgltf_result_success)
    {
        return Fail(Diagnostic, RelativeFile, "/", "invalid_gltf",
            ParseResult == cgltf_result_out_of_memory ? "glTF parser exceeded its allocation budget" : "could not parse the glTF document");
    }
    if (StringView(Data->asset.version) != StringView("2.0") ||
        (Data->asset.min_version != nullptr && StringView(Data->asset.min_version) != StringView("2.0")))
    {
        return Fail(Diagnostic, RelativeFile, "/asset/version", "unsupported_version", "static import requires glTF 2.0");
    }
    if (Data->extensions_used_count != 0 || Data->extensions_required_count != 0 || Data->data_extensions_count != 0 || Data->asset.extensions_count != 0)
    {
        return Fail(Diagnostic, RelativeFile, Data->extensions_required_count != 0 ? "/extensionsRequired" : "/extensionsUsed",
            "unsupported_extension", "the static importer supports core glTF without extensions");
    }
    if (Data->animations_count != 0 || Data->skins_count != 0)
    {
        return Fail(Diagnostic, RelativeFile, Data->animations_count != 0 ? "/animations" : "/skins",
            "unsupported_animation", "animated and skinned models are outside the static import subset");
    }
    if (Data->nodes_count > MaxElements || Data->meshes_count > MaxElements || Data->materials_count > MaxElements ||
        Data->images_count > MaxElements || Data->buffers_count > MaxElements || Data->scenes_count > MaxElements ||
        Data->accessors_count > MaxElements || Data->buffer_views_count > MaxElements)
    {
        return Fail(Diagnostic, RelativeFile, "/", "limit_exceeded", "glTF source element count exceeds the import limit");
    }
    if (SceneIndex >= Data->scenes_count)
    {
        return Fail(Diagnostic, RelativeFile, FElementSelector("scenes", SceneIndex).Text,
            "unknown_scene", "the selected glTF scene index does not exist");
    }
    if (!Buffers.TryResize(Data->buffers_count) || !ReachableMeshes.TryResize(Data->meshes_count, 0) ||
        !ReachableMaterials.TryResize(Data->materials_count, 0) || !ReachableImages.TryResize(Data->images_count, 0))
    {
        return Fail(Diagnostic, RelativeFile, "/", "out_of_memory", "could not allocate source element tables");
    }
    const auto& Scene = Data->scenes[SceneIndex];
    if (Scene.extensions_count != 0 || Scene.nodes_count == 0 || Scene.nodes_count > Data->nodes_count)
    {
        return Fail(Diagnostic, RelativeFile, FElementSelector("scenes", SceneIndex).Text,
            "invalid_scene", "the selected scene must contain valid core glTF root nodes");
    }
    if (!CollectScene(Scene, Diagnostic))
    {
        return false;
    }
    for (uint32 Index = 0; Index < ReachableMeshes.Size(); ++Index)
    {
        if (ReachableMeshes[Index] != 0 && !CollectMesh(Index, Diagnostic))
        {
            return false;
        }
    }
    if (MeshEntries.IsEmpty())
    {
        return Fail(Diagnostic, RelativeFile, FElementSelector("scenes", SceneIndex).Text,
            "empty_geometry", "the selected scene contains no static mesh primitives");
    }
    return true;
}

bool FGltfSource::FState::CollectScene(const cgltf_scene& Scene, FGltfDiagnostic& Diagnostic) noexcept
{
    Array<const cgltf_node*> Pending;
    Array<uint8> Visited;
    if (!Visited.TryResize(Data->nodes_count, 0) || !Pending.TryReserve(Data->nodes_count))
    {
        return Fail(Diagnostic, RelativeFile, "/nodes", "out_of_memory", "could not allocate scene traversal");
    }
    for (cgltf_size RootIndex = 0; RootIndex < Scene.nodes_count; ++RootIndex)
    {
        const auto* const Node = Scene.nodes[RootIndex];
        if (Node == nullptr || Node->parent != nullptr || !Pending.TryPushBack(Node))
        {
            return Fail(Diagnostic, RelativeFile, "/scenes", "invalid_hierarchy", "a scene root has a parent or is invalid");
        }
    }
    while (!Pending.IsEmpty())
    {
        const auto* const Node = Pending.Back();
        Pending.PopBack();
        const uint32 Index = SourceIndex(Node, Data->nodes);
        const FElementSelector Selector("nodes", Index);
        if (Visited[Index] != 0)
        {
            return Fail(Diagnostic, RelativeFile, Selector.Text, "invalid_hierarchy", "node is reachable more than once");
        }
        Visited[Index] = 1;
        if (Node->has_matrix || Node->skin != nullptr || Node->weights_count != 0 ||
            Node->extensions_count != 0 || Node->has_mesh_gpu_instancing || Node->light != nullptr)
        {
            return Fail(Diagnostic, RelativeFile, Selector.Text, "unsupported_node", "only core static TRS nodes are supported");
        }
        FGltfNode Entry;
        Entry.Index = Index;
        Entry.Parent = SourceIndex(Node->parent, Data->nodes);
        Entry.Mesh = SourceIndex(Node->mesh, Data->meshes);
        if (!ConvertNode(*Node, Entry))
        {
            return Fail(Diagnostic, RelativeFile, Selector.Text, "invalid_transform", "node TRS must be finite with a unit quaternion");
        }
        if (!CopyName(Node->name, Entry.Name) || !NodeEntries.TryPushBack(std::move(Entry)))
        {
            return Fail(Diagnostic, RelativeFile, Selector.Text, "node_metadata", "node name or hierarchy exceeds the import limit");
        }
        if (Node->mesh != nullptr)
        {
            ReachableMeshes[SourceIndex(Node->mesh, Data->meshes)] = 1;
        }
        if (Node->camera != nullptr && !Warn(Selector.Text, "ignored_camera", "use the author Scene camera for preview", Diagnostic))
        {
            return false;
        }
        for (cgltf_size ChildIndex = 0; ChildIndex < Node->children_count; ++ChildIndex)
        {
            if (Pending.Size() >= Data->nodes_count || !Pending.TryPushBack(Node->children[ChildIndex]))
            {
                return Fail(Diagnostic, RelativeFile, Selector.Text, "invalid_hierarchy", "too many pending nodes");
            }
        }
    }
    return true;
}

bool FGltfSource::FState::ResolveSidecar(StringView Uri, StringView Suffix, StringView Selector, String& RelativePath,
    String& AbsolutePath, FGltfDiagnostic& Diagnostic) const noexcept
{
    String Decoded;
    if (!DecodeUri(Uri, Decoded) || !SuffixEquals(Decoded, Suffix))
    {
        return Fail(Diagnostic, RelativeFile, Selector, "unsupported_uri", "expected a local relative BIN or PNG URI");
    }
    if (!RelativePath.TryAssign(RelativeDirectory) || !RelativePath.TryAppend(Decoded))
    {
        return Fail(Diagnostic, RelativeFile, Selector, "out_of_memory", "could not store the external resource URI");
    }
    if (const auto Result = ResolveContentFile(Root, RelativePath, AbsolutePath); Result != EFileSystemResult::Success)
    {
        return Fail(Diagnostic, RelativePath, Selector, "sidecar_path", DescribeFileSystemResult(Result));
    }
    return true;
}

bool FGltfSource::FState::LoadBuffer(const cgltf_buffer& Buffer, StringView Selector, FGltfDiagnostic& Diagnostic) noexcept
{
    auto& Bytes = Buffers[SourceIndex(&Buffer, Data->buffers)];
    if (!Bytes.IsEmpty())
    {
        return true;
    }
    if (Buffer.size == 0 || Buffer.size > MaxBufferBytes || Buffer.size > MaxTotalBufferBytes - BufferBytes || Buffer.extensions_count != 0)
    {
        return Fail(Diagnostic, RelativeFile, Selector, "buffer_limit", "external buffer size is invalid or exceeds the import budget");
    }
    String Uri;
    if (!CopyName(Buffer.uri, Uri))
    {
        return Fail(Diagnostic, RelativeFile, Selector, "unsupported_uri", "external buffer URI is invalid");
    }
    String RelativePath;
    String AbsolutePath;
    if (!ResolveSidecar(Uri, ".bin", Selector, RelativePath, AbsolutePath, Diagnostic))
    {
        return false;
    }
    uint64 FileSize = 0;
    if (const auto Result = ReadContentFilePrefix(AbsolutePath, Buffer.size, Bytes, FileSize); Result != EFileSystemResult::Success)
    {
        return Fail(Diagnostic, RelativePath, Selector, "sidecar_read", DescribeFileSystemResult(Result));
    }
    if (FileSize < Buffer.size)
    {
        return Fail(Diagnostic, RelativePath, Selector, "buffer_length", "external buffer is shorter than its declared byteLength");
    }
    BufferBytes += Bytes.Size();
    return true;
}

bool FGltfSource::FState::CollectImage(uint32 Index, FGltfDiagnostic& Diagnostic) noexcept
{
    if (ReachableImages[Index] != 0)
    {
        return true;
    }
    const auto& Image = Data->images[Index];
    const FElementSelector Selector("images", Index);
    if (Image.uri == nullptr || Image.buffer_view != nullptr || Image.extensions_count != 0 ||
        (Image.mime_type != nullptr && StringView(Image.mime_type) != StringView("image/png")))
    {
        return Fail(Diagnostic, RelativeFile, Selector.Text, "unsupported_image", "expected an external Base Color PNG image");
    }
    FGltfImage Entry;
    Entry.Index = Index;
    if (!CopyName(Image.name == nullptr ? Image.uri : Image.name, Entry.Name) || !ImageEntries.TryPushBack(std::move(Entry)))
    {
        return Fail(Diagnostic, RelativeFile, Selector.Text, "image_metadata", "image name or count exceeds the import limit");
    }
    ReachableImages[Index] = 1;
    return true;
}

bool FGltfSource::FState::CollectMaterial(uint32 Index, FGltfDiagnostic& Diagnostic) noexcept
{
    const FElementSelector Selector("materials", Index);
    if (Index == GltfNoIndex)
    {
        if (!bDefaultMaterial)
        {
            FGltfMaterial Entry;
            if (!Entry.Name.TryAssign("Default") || !MaterialEntries.TryPushBack(std::move(Entry)))
            {
                return Fail(Diagnostic, RelativeFile, Selector.Text, "out_of_memory", "could not store the default material");
            }
            bDefaultMaterial = true;
        }
        return true;
    }
    if (ReachableMaterials[Index] != 0)
    {
        return true;
    }
    const auto& Source = Data->materials[Index];
    if (Source.alpha_mode != cgltf_alpha_mode_opaque || Source.extensions_count != 0 || Source.unlit ||
        Source.has_pbr_specular_glossiness || Source.has_transmission || Source.has_volume || Source.has_sheen ||
        Source.has_clearcoat || Source.has_ior || Source.has_specular || Source.has_emissive_strength ||
        Source.has_iridescence || Source.has_diffuse_transmission || Source.has_anisotropy || Source.has_dispersion)
    {
        return Fail(Diagnostic, RelativeFile, Selector.Text, "unsupported_material", "only core opaque materials are supported");
    }
    const auto& Pbr = Source.pbr_metallic_roughness;
    for (float Channel : Pbr.base_color_factor)
    {
        if (!std::isfinite(Channel) || Channel < 0.0f || Channel > 1.0f)
        {
            return Fail(Diagnostic, RelativeFile, Selector.Text, "invalid_color", "Base Color factors must be finite and in [0,1]");
        }
    }
    FGltfMaterial Entry;
    Entry.Index = Index;
    const auto& Color = Pbr.base_color_texture;
    if (const auto* Texture = Color.texture)
    {
        if (Color.texcoord != 0 || Color.has_transform || Texture->image == nullptr ||
            Texture->extensions_count != 0 || Texture->has_basisu || Texture->has_webp)
        {
            return Fail(Diagnostic, RelativeFile, Selector.Text, "unsupported_texture_binding",
                "the Base Color input requires a core PNG texture on TEXCOORD_0");
        }
        Entry.Image = SourceIndex(Texture->image, Data->images);
        if (!CollectImage(Entry.Image, Diagnostic))
        {
            return false;
        }
        const auto* const Sampler = Texture->sampler;
        if ((Sampler == nullptr || Sampler->mag_filter != cgltf_filter_type_nearest ||
            Sampler->min_filter != cgltf_filter_type_nearest || Sampler->wrap_s != cgltf_wrap_mode_clamp_to_edge ||
            Sampler->wrap_t != cgltf_wrap_mode_clamp_to_edge) &&
            !Warn(Selector.Text, "sampler_approximation", "preview uses nearest, clamp-to-edge and mip 0", Diagnostic))
        {
            return false;
        }
    }
    if ((Source.normal_texture.texture != nullptr || Source.occlusion_texture.texture != nullptr ||
        Source.emissive_texture.texture != nullptr || Source.emissive_factor[0] != 0.0f ||
        Source.emissive_factor[1] != 0.0f || Source.emissive_factor[2] != 0.0f ||
        Pbr.metallic_roughness_texture.texture != nullptr || Pbr.metallic_factor != 1.0f ||
        Pbr.roughness_factor != 1.0f || Source.double_sided) &&
        !Warn(Selector.Text, "material_approximation", "preview retains opaque Base Color; other PBR channels are unused", Diagnostic))
    {
        return false;
    }
    if (!CopyName(Source.name, Entry.Name) || !MaterialEntries.TryPushBack(std::move(Entry)))
    {
        return Fail(Diagnostic, RelativeFile, Selector.Text, "material_metadata", "material entries exceed the import limit");
    }
    ReachableMaterials[Index] = 1;
    return true;
}

bool FGltfSource::FState::CollectMesh(uint32 Index, FGltfDiagnostic& Diagnostic) noexcept
{
    const auto& Source = Data->meshes[Index];
    if (Source.primitives_count == 0 || Source.primitives_count > MaxElements - MeshEntries.Size() ||
        Source.weights_count != 0 || Source.extensions_count != 0)
    {
        return Fail(Diagnostic, RelativeFile, FElementSelector("meshes", Index).Text,
            "unsupported_mesh", "mesh contains no primitives, is outside the static subset or exceeds the import limit");
    }
    for (uint32 PrimitiveIndex = 0; PrimitiveIndex < Source.primitives_count; ++PrimitiveIndex)
    {
        const auto& Primitive = Source.primitives[PrimitiveIndex];
        const FElementSelector Selector("meshes", Index, PrimitiveIndex);
        FGltfPrimitive Entry;
        Entry.Mesh = Index;
        Entry.Index = PrimitiveIndex;
        Entry.Material = SourceIndex(Primitive.material, Data->materials);
        if (!CollectMaterial(Entry.Material, Diagnostic))
        {
            return false;
        }
        const bool bTextured = Primitive.material != nullptr && Primitive.material->pbr_metallic_roughness.base_color_texture.texture != nullptr;
        FMeshAccessors Accessors;
        if (!ReadMeshAccessors(Primitive, bTextured, Accessors))
        {
            return Fail(Diagnostic, RelativeFile, Selector.Text, "unsupported_primitive",
                "expected indexed triangles with matching positions, normals and UV count for textured meshes");
        }
        if (!ValidateAccessor(*Accessors.Positions, false, cgltf_type_vec3, 12) ||
            !ValidateAccessor(*Accessors.Normals, false, cgltf_type_vec3, 12) ||
            (Accessors.Uvs != nullptr && !ValidateAccessor(*Accessors.Uvs, false, cgltf_type_vec2, 8)) ||
            !ValidateAccessor(*Accessors.Indices, true, cgltf_type_scalar, IndexSize(Accessors.Indices->component_type)))
        {
            return Fail(Diagnostic, RelativeFile, Selector.Text, "invalid_accessor",
                "accessor format, alignment, stride, bounds or count is invalid");
        }
        const cgltf_accessor* Wires[] = {Accessors.Positions, Accessors.Normals, Accessors.Uvs, Accessors.Indices};
        for (const auto* Accessor : Wires)
        {
            if (Accessor != nullptr && !LoadBuffer(*Accessor->buffer_view->buffer, Selector.Text, Diagnostic))
            {
                return false;
            }
        }
        if (Accessors.bIgnoredAttributes && !Warn(Selector.Text, "ignored_attributes", "only POSITION, NORMAL and TEXCOORD_0 are used", Diagnostic))
        {
            return false;
        }
        if (!CopyName(Source.name, Entry.Name) || !MeshEntries.TryPushBack(std::move(Entry)))
        {
            return Fail(Diagnostic, RelativeFile, Selector.Text, "mesh_metadata", "mesh name or count exceeds the import limit");
        }
    }
    return true;
}

FAccessorBytes FGltfSource::FState::MakeAccessor(const cgltf_accessor& Accessor, std::size_t Width) const noexcept
{
    const auto& View = *Accessor.buffer_view;
    const auto& Buffer = Buffers[SourceIndex(View.buffer, Data->buffers)];
    return {Buffer.Data() + View.offset + Accessor.offset, View.stride == 0 ? Width : View.stride};
}

FGltfSource::FGltfSource() noexcept = default;
FGltfSource::~FGltfSource() = default;

bool FGltfSource::Open(StringView AbsoluteRoot, StringView RelativeGltf, uint32 Scene, FGltfDiagnostic& Diagnostic) noexcept
{
    Diagnostic = {};
    auto Candidate = TryMakeUnique<FState>();
    if (!Candidate)
    {
        return Fail(Diagnostic, RelativeGltf, "/", "out_of_memory", "could not allocate the glTF source");
    }
    if (!Candidate->Parse(AbsoluteRoot, RelativeGltf, Scene, Diagnostic))
    {
        return false;
    }
    State = std::move(Candidate);
    return true;
}

Span<const uint8> FGltfSource::DocumentBytes() const noexcept { return State ? State->Document.AsSpan() : Span<const uint8>{}; }
Span<const FGltfNode> FGltfSource::Nodes() const noexcept { return State ? State->NodeEntries.AsSpan() : Span<const FGltfNode>{}; }
Span<const FGltfPrimitive> FGltfSource::Primitives() const noexcept { return State ? State->MeshEntries.AsSpan() : Span<const FGltfPrimitive>{}; }
Span<const FGltfMaterial> FGltfSource::Materials() const noexcept { return State ? State->MaterialEntries.AsSpan() : Span<const FGltfMaterial>{}; }
Span<const FGltfImage> FGltfSource::Images() const noexcept { return State ? State->ImageEntries.AsSpan() : Span<const FGltfImage>{}; }
Span<const FGltfDiagnostic> FGltfSource::Warnings() const noexcept { return State ? State->WarningEntries.AsSpan() : Span<const FGltfDiagnostic>{}; }

bool FGltfSource::DecodeMesh(uint32 Mesh, uint32 PrimitiveIndex, FMeshAssetData& Out, FGltfDiagnostic& Diagnostic) const noexcept
{
    Diagnostic = {};
    const FElementSelector Selector("meshes", Mesh, PrimitiveIndex);
    if (!State || Mesh >= State->ReachableMeshes.Size() || State->ReachableMeshes[Mesh] == 0 ||
        PrimitiveIndex >= State->Data->meshes[Mesh].primitives_count)
    {
        return Fail(Diagnostic, State ? State->RelativeFile.View() : StringView{}, Selector.Text,
            "unbound_selector", "mesh primitive is outside the selected scene");
    }
    const auto& Primitive = State->Data->meshes[Mesh].primitives[PrimitiveIndex];
    FMeshAccessors Accessors;
    if (!ReadMeshAccessors(Primitive, false, Accessors))
    {
        return Fail(Diagnostic, State->RelativeFile, Selector.Text, "invalid_accessor", "mesh source accessors are invalid");
    }
    const auto PositionBytes = State->MakeAccessor(*Accessors.Positions, 12);
    const auto NormalBytes = State->MakeAccessor(*Accessors.Normals, 12);
    const auto UVBytes = Accessors.Uvs != nullptr ? State->MakeAccessor(*Accessors.Uvs, 8) : FAccessorBytes{};
    const std::size_t IndexWidth = IndexSize(Accessors.Indices->component_type);
    const auto Indices = State->MakeAccessor(*Accessors.Indices, IndexWidth);
    FMeshAssetData Candidate(Out.Positions.GetAllocator());
    const std::size_t VertexCount = Accessors.Positions->count;
    if (!Candidate.Positions.TryResize(VertexCount) || !Candidate.Normals.TryResize(VertexCount) ||
        (Accessors.Uvs != nullptr && !Candidate.UVs.TryResize(VertexCount)) ||
        !Candidate.Indices.TryResize(Accessors.Indices->count))
    {
        return Fail(Diagnostic, State->RelativeFile, Selector.Text, "out_of_memory", "could not allocate imported mesh geometry");
    }
    for (std::size_t Index = 0; Index < VertexCount; ++Index)
    {
        const auto Position = ReadVector(PositionBytes[Index]);
        auto Normal = ReadVector(NormalBytes[Index]);
        if (!IsFiniteVector(Position) || !NormalizeNormal(Normal))
        {
            return Fail(Diagnostic, State->RelativeFile, Selector.Text, "invalid_vertex", "positions must be finite and normals finite and nonzero");
        }
        Candidate.Positions[Index] = Position;
        Candidate.Normals[Index] = Normal;
        if (Accessors.Uvs != nullptr)
        {
            const Math::Vector2f UV{ReadFloat(UVBytes[Index]), ReadFloat(UVBytes[Index] + 4)};
            if (!std::isfinite(UV[0]) || !std::isfinite(UV[1]))
            {
                return Fail(Diagnostic, State->RelativeFile, Selector.Text, "invalid_uv", "texture coordinates must be finite");
            }
            Candidate.UVs[Index] = UV;
        }
        if (Index == 0)
        {
            Candidate.BoundsMin = Position;
            Candidate.BoundsMax = Position;
        }
        for (std::size_t Axis = 0; Axis < 3; ++Axis)
        {
            if (Position[Axis] < Candidate.BoundsMin[Axis])
            {
                Candidate.BoundsMin[Axis] = Position[Axis];
            }
            if (Position[Axis] > Candidate.BoundsMax[Axis])
            {
                Candidate.BoundsMax[Axis] = Position[Axis];
            }
        }
    }
    const uint32 RestartIndex = IndexWidth == 1 ? 0xFFu : (IndexWidth == 2 ? 0xFFFFu : 0xFFFFFFFFu);
    for (std::size_t Index = 0; Index < Candidate.Indices.Size(); ++Index)
    {
        const uint32 Value = ReadIndex(Indices[Index], IndexWidth);
        if (Value >= VertexCount || Value == RestartIndex)
        {
            return Fail(Diagnostic, State->RelativeFile, Selector.Text, "index_out_of_range", "triangle index is invalid or reserved");
        }
        Candidate.Indices[Index] = Value;
    }
    Out = std::move(Candidate);
    return true;
}

bool FGltfSource::DecodeMaterial(uint32 Material, FAssetId Texture, FMaterialAssetData& Out, FGltfDiagnostic& Diagnostic) const noexcept
{
    Diagnostic = {};
    const FElementSelector Selector("materials", Material);
    if (!State || (Material == GltfNoIndex && !State->bDefaultMaterial) ||
        (Material != GltfNoIndex && (Material >= State->ReachableMaterials.Size() || State->ReachableMaterials[Material] == 0)))
    {
        return Fail(Diagnostic, State ? State->RelativeFile.View() : StringView{}, Selector.Text,
            "unbound_selector", "material is outside the selected scene");
    }
    FMaterialAssetData Candidate;
    if (Material != GltfNoIndex)
    {
        const auto& Pbr = State->Data->materials[Material].pbr_metallic_roughness;
        const auto& Factor = Pbr.base_color_factor;
        Candidate.BaseColor = Math::Vector3f{Factor[0], Factor[1], Factor[2]};
        if (Pbr.base_color_texture.texture != nullptr)
        {
            if (!Texture.IsValid())
            {
                return Fail(Diagnostic, State->RelativeFile, Selector.Text, "missing_texture_binding", "Base Color texture lacks an asset UUID");
            }
            Candidate.BaseColorTexture = Texture;
        }
    }
    if (Texture.IsValid() && !Candidate.BaseColorTexture.IsValid())
    {
        return Fail(Diagnostic, State->RelativeFile, Selector.Text, "extra_texture_binding", "untextured material received a texture UUID");
    }
    Out = Candidate;
    return true;
}

bool FGltfSource::DecodeTexture(uint32 Image, FCpuTextureAsset& Out, FGltfDiagnostic& Diagnostic) const noexcept
{
    Diagnostic = {};
    const FElementSelector Selector("images", Image);
    if (!State || Image >= State->ReachableImages.Size() || State->ReachableImages[Image] == 0)
    {
        return Fail(Diagnostic, State ? State->RelativeFile.View() : StringView{}, Selector.Text,
            "unbound_selector", "image is outside the selected scene's Base Color dependencies");
    }
    String Uri;
    if (!CopyName(State->Data->images[Image].uri, Uri))
    {
        return Fail(Diagnostic, State->RelativeFile, Selector.Text, "unsupported_uri", "Base Color image URI is invalid");
    }
    String RelativePath;
    String AbsolutePath;
    if (!State->ResolveSidecar(Uri, ".png", Selector.Text, RelativePath, AbsolutePath, Diagnostic))
    {
        return false;
    }
    Array<uint8> Encoded;
    if (const auto Result = ReadContentFile(AbsolutePath, MaxPngBytes, Encoded); Result != EFileSystemResult::Success)
    {
        return Fail(Diagnostic, RelativePath, Selector.Text, "sidecar_read", DescribeFileSystemResult(Result));
    }
    FPngDecode Decoder;
    unsigned Width = 0;
    unsigned Height = 0;
    if (const unsigned Error = lodepng_inspect(&Width, &Height, &Decoder.State, Encoded.Data(), Encoded.Size()); Error != 0)
    {
        return Fail(Diagnostic, RelativePath, Selector.Text, "invalid_png", lodepng_error_text(Error));
    }
    if (Width == 0 || Height == 0 || Width > MaxAssetTextureDimension || Height > MaxAssetTextureDimension)
    {
        return Fail(Diagnostic, RelativePath, Selector.Text, "texture_limit", "Base Color PNG dimensions exceed the asset limit");
    }
    const std::size_t PixelCount = static_cast<std::size_t>(Width) * Height;
    Decoder.State.info_raw.colortype = LCT_RGBA;
    Decoder.State.info_raw.bitdepth = 8;
    // Allow the PNG's filtered scanlines and Adam7 pass overhead, including
    // 16-bit source channels. The output is always exactly four bytes/pixel.
    Decoder.State.decoder.zlibsettings.max_output_size = PixelCount * 8 + static_cast<std::size_t>(Height) * 7 + 7;
    if (const unsigned Error = lodepng_decode(&Decoder.Pixels, &Width, &Height, &Decoder.State, Encoded.Data(), Encoded.Size()); Error != 0)
    {
        return Fail(Diagnostic, RelativePath, Selector.Text, "invalid_png", lodepng_error_text(Error));
    }
    FCpuTextureAsset Candidate(Out.Pixels.GetAllocator());
    Candidate.Width = Width;
    Candidate.Height = Height;
    if (Decoder.Pixels == nullptr || !Candidate.Pixels.TryResize(PixelCount * 4))
    {
        return Fail(Diagnostic, RelativePath, Selector.Text, "out_of_memory", "could not allocate the Base Color pixels");
    }
    std::memcpy(Candidate.Pixels.Data(), Decoder.Pixels, Candidate.Pixels.Size());
    Out = std::move(Candidate);
    return true;
}

} // namespace LE::Detail
