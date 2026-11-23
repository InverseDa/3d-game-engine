#include "AssetImport.h"

#include "Asset/SoftAssetPath.h"
#include "AssetFormat/AssetFormat.h"
#include "Containers/HashMap.h"
#include "FileSystem/FileSystem.h"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <locale.h>
#include <utility>

#if PLATFORM_MAC
#include <xlocale.h>
#endif

namespace LE
{
namespace
{

constexpr std::size_t MaxMaterialSourceBytes = 4 * 1024;
constexpr StringView OutputExtension(".leasset", 8);

enum class EImportKind : uint8
{
    Mesh,
    Texture,
    Material,
};

struct FImportArguments
{
    EImportKind Kind = EImportKind::Mesh;
    StringView Source;
    StringView Id;
    StringView Path;
    StringView Output;
    bool bCheckOnly = false;
};

struct FSourceError
{
    const char* Message = nullptr;
    std::size_t Line = 0;
};

class FTokenCursor
{
public:
    explicit FTokenCursor(const StringView InText) noexcept : Text(InText) {}

    StringView Next(std::size_t* OutLine = nullptr) noexcept
    {
        while (Offset < Text.Size())
        {
            if (IsWhitespace(Text[Offset]))
            {
                if (Text[Offset] == '\n')
                {
                    ++Line;
                }
                ++Offset;
            }
            else if (Text[Offset] == '#')
            {
                while (Offset < Text.Size() && Text[Offset] != '\n')
                {
                    ++Offset;
                }
            }
            else
            {
                break;
            }
        }

        if (OutLine != nullptr)
        {
            *OutLine = Line;
        }
        const std::size_t Start = Offset;
        while (Offset < Text.Size() && !IsWhitespace(Text[Offset]) && Text[Offset] != '#')
        {
            ++Offset;
        }
        return Text.Substr(Start, Offset - Start);
    }

    // P6 separates its ASCII header from binary pixels with one whitespace byte.
    bool TryReadBinaryRaster(StringView& Out) noexcept
    {
        if (Offset == Text.Size() || !IsWhitespace(Text[Offset]))
        {
            return false;
        }
        ++Offset;
        Out = Text.Substr(Offset, Text.Size() - Offset);
        return true;
    }

private:
    static bool IsWhitespace(const char Value) noexcept { return Value == ' ' || Value == '\t' || Value == '\r' || Value == '\n' || Value == '\v' || Value == '\f'; }

    StringView Text;
    std::size_t Offset = 0;
    std::size_t Line = 1;
};

class FDecimalParser
{
public:
#if PLATFORM_WINDOWS
    FDecimalParser() noexcept : Locale(_create_locale(LC_NUMERIC, "C")) {}
    ~FDecimalParser()
    {
        if (Locale != nullptr)
        {
            _free_locale(Locale);
        }
    }
#else
    FDecimalParser() noexcept : Locale(newlocale(LC_NUMERIC_MASK, "C", nullptr)) {}
    ~FDecimalParser()
    {
        if (Locale != nullptr)
        {
            freelocale(Locale);
        }
    }
#endif

    FDecimalParser(const FDecimalParser&) = delete;
    FDecimalParser& operator=(const FDecimalParser&) = delete;

    bool IsValid() const noexcept { return Locale != nullptr; }

    bool Parse(const StringView Token, float& OutValue) const noexcept
    {
        char Buffer[64]{};
        if (Token.IsEmpty() || Token.Size() >= sizeof(Buffer))
        {
            return false;
        }
        for (std::size_t Index = 0; Index < Token.Size(); ++Index)
        {
            const char Value = Token[Index];
            if ((Value < '0' || Value > '9') && Value != '.' && Value != '+' && Value != '-' && Value != 'e' && Value != 'E')
            {
                return false;
            }
            Buffer[Index] = Value;
        }
        char* End = nullptr;
        errno = 0;
#if PLATFORM_WINDOWS
        const float Value = _strtof_l(Buffer, &End, Locale);
#else
        const float Value = strtof_l(Buffer, &End, Locale);
#endif
        if (End != Buffer + Token.Size() || !std::isfinite(Value) || (errno == ERANGE && Value == 0.0f))
        {
            return false;
        }
        OutValue = Value;
        return true;
    }

private:
#if PLATFORM_WINDOWS
    _locale_t Locale;
#else
    locale_t Locale;
#endif
};

bool ParseUnsigned(const StringView Token, uint32& OutValue) noexcept
{
    if (Token.IsEmpty())
    {
        return false;
    }
    const char* const End = Token.Data() + Token.Size();
    const auto Result = std::from_chars(Token.Data(), End, OutValue);
    return Result.ec == std::errc{} && Result.ptr == End;
}

template <typename Vector> bool ParseVector(FTokenCursor& Tokens, const FDecimalParser& Decimal, Vector& Out) noexcept
{
    for (float& Component : Out.Values)
    {
        if (!Decimal.Parse(Tokens.Next(), Component))
        {
            return false;
        }
    }
    return true;
}

StringView TextWithoutBom(const StringView Source) noexcept
{
    constexpr StringView Bom("\xef\xbb\xbf", 3);
    return Source.Substr(0, Bom.Size()) == Bom ? Source.Substr(Bom.Size(), Source.Size()) : Source;
}

StringView TakeLine(const StringView Text, std::size_t& Offset) noexcept
{
    const std::size_t Start = Offset;
    while (Offset < Text.Size() && Text[Offset] != '\n')
    {
        ++Offset;
    }
    const StringView Line = Text.Substr(Start, Offset - Start);
    if (Offset < Text.Size())
    {
        ++Offset;
    }
    return Line;
}

struct FMeshCorner
{
    uint32 Position = 0;
    uint32 UV = 0;
    uint32 Normal = 0;

    bool HasSameAttributes(const FMeshCorner& Other) const noexcept { return (UV != 0) == (Other.UV != 0) && (Normal != 0) == (Other.Normal != 0); }

    uint64 VertexKey() const noexcept
    {
        constexpr uint32 IndexBits = 20;
        static_assert(MaxAssetMeshVertices < (1u << IndexBits));
        return (static_cast<uint64>(Position) << (2u * IndexBits)) | (static_cast<uint64>(UV) << IndexBits) | Normal;
    }
};

struct FMeshTriangle
{
    FMeshCorner Corners[3];
    std::size_t Line = 0;
};

bool ParseMeshIndex(const StringView Token, uint32& Index) noexcept
{
    return ParseUnsigned(Token, Index) && Index != 0 && Index <= MaxAssetMeshVertices;
}

std::size_t MeshIndexSeparator(const StringView Token, std::size_t Offset) noexcept
{
    while (Offset < Token.Size() && Token[Offset] != '/')
    {
        ++Offset;
    }
    return Offset;
}

bool ParseCorner(const StringView Token, FMeshCorner& Corner) noexcept
{
    const std::size_t First = MeshIndexSeparator(Token, 0);
    if (!ParseMeshIndex(Token.Substr(0, First), Corner.Position))
    {
        return false;
    }
    if (First == Token.Size())
    {
        return true;
    }
    const std::size_t Second = MeshIndexSeparator(Token, First + 1);
    const StringView UV = Token.Substr(First + 1, Second - First - 1);
    if (Second == Token.Size())
    {
        return ParseMeshIndex(UV, Corner.UV);
    }
    return (UV.IsEmpty() || ParseMeshIndex(UV, Corner.UV)) && ParseMeshIndex(Token.Substr(Second + 1, Token.Size()), Corner.Normal);
}

bool NormalizeMeshNormal(Math::Vector3f& Normal) noexcept
{
    const double X = Normal.Values[0];
    const double Y = Normal.Values[1];
    const double Z = Normal.Values[2];
    const double Length = std::sqrt(X * X + Y * Y + Z * Z);
    if (Length == 0.0)
    {
        return false;
    }
    for (float& Component : Normal.Values)
    {
        Component = static_cast<float>(static_cast<double>(Component) / Length);
    }
    return true;
}

FSourceError ParseMesh(const StringView Source, FMeshAssetData& Mesh) noexcept
{
    const FDecimalParser Decimal;
    if (!Decimal.IsValid())
    {
        return {"could not initialize decimal parser"};
    }

    const StringView Text = TextWithoutBom(Source);
    Array<Math::Vector3f> SourcePositions;
    Array<Math::Vector2f> SourceUVs;
    Array<Math::Vector3f> SourceNormals;
    Array<FMeshTriangle> Triangles;
    for (std::size_t Offset = 0, Line = 1; Offset < Text.Size(); ++Line)
    {
        FTokenCursor Tokens(TakeLine(Text, Offset));
        const StringView Kind = Tokens.Next();
        if (Kind == StringView("v"))
        {
            Math::Vector3f Position;
            if (!ParseVector(Tokens, Decimal, Position) || !Tokens.Next().IsEmpty())
            {
                return {"expected three finite decimal vertex coordinates", Line};
            }
            if (SourcePositions.Size() >= MaxAssetMeshVertices)
            {
                return {"vertex limit exceeded", Line};
            }
            if (!SourcePositions.TryPushBack(Position))
            {
                return {"could not allocate a mesh vertex", Line};
            }
        }
        else if (Kind == StringView("vt"))
        {
            Math::Vector2f UV;
            if (!ParseVector(Tokens, Decimal, UV) || !Tokens.Next().IsEmpty())
            {
                return {"expected two finite decimal UV coordinates", Line};
            }
            if (SourceUVs.Size() >= MaxAssetMeshVertices)
            {
                return {"UV coordinate limit exceeded", Line};
            }
            UV[1] = 1.0f - UV[1];
            if (!SourceUVs.TryPushBack(UV))
            {
                return {"could not allocate a UV coordinate", Line};
            }
        }
        else if (Kind == StringView("vn"))
        {
            Math::Vector3f Normal;
            if (!ParseVector(Tokens, Decimal, Normal) || !Tokens.Next().IsEmpty() || !NormalizeMeshNormal(Normal))
            {
                return {"expected three finite decimal normal coordinates with nonzero length", Line};
            }
            if (SourceNormals.Size() >= MaxAssetMeshVertices)
            {
                return {"normal coordinate limit exceeded", Line};
            }
            if (!SourceNormals.TryPushBack(Normal))
            {
                return {"could not allocate a normal coordinate", Line};
            }
        }
        else if (Kind == StringView("f"))
        {
            if (Triangles.Size() >= MaxAssetMeshIndices / 3)
            {
                return {"index limit exceeded", Line};
            }
            FMeshTriangle Triangle;
            Triangle.Line = Line;
            for (FMeshCorner& Corner : Triangle.Corners)
            {
                if (!ParseCorner(Tokens.Next(), Corner))
                {
                    return {"expected three positive OBJ corners using v, v/vt, v//vn or v/vt/vn", Line};
                }
            }
            if (!Tokens.Next().IsEmpty())
            {
                return {"only triangular faces are supported", Line};
            }
            const FMeshCorner& FirstCorner = Triangles.IsEmpty() ? Triangle.Corners[0] : Triangles[0].Corners[0];
            for (const FMeshCorner& Corner : Triangle.Corners)
            {
                if (!Corner.HasSameAttributes(FirstCorner))
                {
                    return {"cannot mix OBJ face formats", Line};
                }
            }
            if (!Triangles.TryPushBack(Triangle))
            {
                return {"could not allocate a mesh triangle", Line};
            }
        }
        else if (!Kind.IsEmpty())
        {
            return {"unsupported OBJ record; expected v, vt, vn or f", Line};
        }
    }
    if (Triangles.IsEmpty())
    {
        return {"expected at least one triangle"};
    }
    const bool bHasUVs = Triangles[0].Corners[0].UV != 0;
    const bool bHasNormals = Triangles[0].Corners[0].Normal != 0;
    if (!SourceNormals.IsEmpty() && !bHasNormals)
    {
        return {"every face corner must reference a normal when vn records are present", Triangles[0].Line};
    }
    if (!Mesh.Indices.TryReserve(Triangles.Size() * 3))
    {
        return {"could not allocate mesh indices"};
    }
    HashMap<uint64, uint32> ExpandedVertices;
    for (const FMeshTriangle& Triangle : Triangles)
    {
        for (const FMeshCorner& Corner : Triangle.Corners)
        {
            if (Corner.Position > SourcePositions.Size())
            {
                return {"face index exceeds vertex count", Triangle.Line};
            }
            uint32 Index = Corner.Position - 1;
            if (bHasUVs || bHasNormals)
            {
                if (Corner.UV > SourceUVs.Size())
                {
                    return {"face UV index exceeds UV coordinate count", Triangle.Line};
                }
                if (Corner.Normal > SourceNormals.Size())
                {
                    return {"face normal index exceeds normal coordinate count", Triangle.Line};
                }
                const uint64 Key = Corner.VertexKey();
                const uint32* const Existing = ExpandedVertices.Find(Key);
                if (Existing != nullptr)
                {
                    Index = *Existing;
                }
                else
                {
                    if (Mesh.Positions.Size() >= MaxAssetMeshVertices)
                    {
                        return {"expanded vertex limit exceeded", Triangle.Line};
                    }
                    Index = static_cast<uint32>(Mesh.Positions.Size());
                    if (!Mesh.Positions.TryPushBack(SourcePositions[Corner.Position - 1]) || (bHasUVs && !Mesh.UVs.TryPushBack(SourceUVs[Corner.UV - 1])) || (bHasNormals && !Mesh.Normals.TryPushBack(SourceNormals[Corner.Normal - 1])))
                    {
                        return {"could not allocate an expanded mesh vertex", Triangle.Line};
                    }
                    if (ExpandedVertices.TryInsert(Key, Index) != EHashInsertResult::Inserted)
                    {
                        return {"could not index an expanded mesh vertex", Triangle.Line};
                    }
                }
            }
            if (!Mesh.Indices.TryPushBack(Index))
            {
                return {"could not append a mesh index", Triangle.Line};
            }
        }
    }
    if (!bHasUVs && !bHasNormals)
    {
        Mesh.Positions = std::move(SourcePositions);
    }
    return {};
}

FSourceError ParseTexture(const StringView Source, FCpuTextureAsset& Texture) noexcept
{
    FTokenCursor Tokens(Source);
    const StringView Magic = Tokens.Next();
    const bool bTextRaster = Magic == StringView("P3");
    if (!bTextRaster && Magic != StringView("P6"))
    {
        return {"expected PPM P3 or P6"};
    }
    uint32 MaxValue = 0;
    if (!ParseUnsigned(Tokens.Next(), Texture.Width) || !ParseUnsigned(Tokens.Next(), Texture.Height) || !ParseUnsigned(Tokens.Next(), MaxValue) || MaxValue != 255)
    {
        return {"expected width, height and a maximum sample of 255"};
    }
    if (Texture.Width == 0 || Texture.Height == 0 || Texture.Width > MaxAssetTextureDimension || Texture.Height > MaxAssetTextureDimension)
    {
        return {"image dimensions exceed v1 limits"};
    }

    const std::size_t PixelCount = static_cast<std::size_t>(Texture.Width) * Texture.Height;
    StringView Raster;
    if (!bTextRaster && (!Tokens.TryReadBinaryRaster(Raster) || Raster.Size() != PixelCount * 3))
    {
        return {"expected one separator byte and exactly width × height × 3 raster bytes"};
    }
    if (bTextRaster && Source.Size() < PixelCount * 6)
    {
        return {"not enough decimal RGB samples for the declared size"};
    }
    if (!Texture.Pixels.TryResize(PixelCount * 4))
    {
        return {"could not allocate image pixels"};
    }
    for (std::size_t Index = 0; Index < PixelCount; ++Index)
    {
        for (std::size_t Channel = 0; Channel < 3; ++Channel)
        {
            uint32 Sample = 0;
            if (bTextRaster)
            {
                if (!ParseUnsigned(Tokens.Next(), Sample) || Sample > 255)
                {
                    return {"expected exactly width × height × 3 decimal samples from 0 to 255"};
                }
            }
            else
            {
                Sample = static_cast<uint8>(Raster[Index * 3 + Channel]);
            }
            Texture.Pixels[Index * 4 + Channel] = static_cast<uint8>(Sample);
        }
        Texture.Pixels[Index * 4 + 3] = 255;
    }
    return bTextRaster && !Tokens.Next().IsEmpty() ? FSourceError{"unexpected data after final RGB sample"} : FSourceError{};
}

FSourceError ParseMaterial(const StringView Source, FMaterialAssetData& Material) noexcept
{
    const FDecimalParser Decimal;
    if (!Decimal.IsValid())
    {
        return {"could not initialize decimal parser"};
    }

    FTokenCursor Tokens(TextWithoutBom(Source));
    bool bHasColor = false;
    std::size_t Line = 0;
    for (StringView Kind = Tokens.Next(&Line); !Kind.IsEmpty(); Kind = Tokens.Next(&Line))
    {
        if (Kind == StringView("base-color"))
        {
            if (bHasColor)
            {
                return {"duplicate base-color directive", Line};
            }
            if (!ParseVector(Tokens, Decimal, Material.BaseColor) || Material.BaseColor[0] < 0.0f || Material.BaseColor[1] < 0.0f || Material.BaseColor[2] < 0.0f)
            {
                return {"expected base-color followed by three finite non-negative decimal components", Line};
            }
            bHasColor = true;
        }
        else if (Kind == StringView("base-color-texture"))
        {
            if (Material.BaseColorTexture.IsValid())
            {
                return {"duplicate base-color-texture directive", Line};
            }
            if (!FAssetId::TryParse(Tokens.Next(), Material.BaseColorTexture))
            {
                return {"expected base-color-texture followed by a non-nil lowercase canonical UUID", Line};
            }
        }
        else
        {
            return {"unsupported material directive; expected base-color or base-color-texture", Line};
        }
    }
    return bHasColor ? FSourceError{} : FSourceError{"material requires a base-color directive"};
}

bool ParseArguments(const Span<const StringView> Arguments, FImportArguments& Out) noexcept
{
    if (Arguments.IsEmpty())
    {
        return false;
    }
    if (Arguments[0] == StringView("mesh"))
    {
        Out.Kind = EImportKind::Mesh;
    }
    else if (Arguments[0] == StringView("texture"))
    {
        Out.Kind = EImportKind::Texture;
    }
    else if (Arguments[0] == StringView("material"))
    {
        Out.Kind = EImportKind::Material;
    }
    else
    {
        return false;
    }

    for (std::size_t Index = 1; Index < Arguments.Size(); ++Index)
    {
        const StringView Flag = Arguments[Index];
        if (Flag == StringView("--check"))
        {
            if (Out.bCheckOnly)
            {
                return false;
            }
            Out.bCheckOnly = true;
            continue;
        }
        StringView* Destination = nullptr;
        if (Flag == StringView("--source"))
        {
            Destination = &Out.Source;
        }
        else if (Flag == StringView("--id"))
        {
            Destination = &Out.Id;
        }
        else if (Flag == StringView("--path"))
        {
            Destination = &Out.Path;
        }
        else if (Flag == StringView("--output"))
        {
            Destination = &Out.Output;
        }
        if (Destination == nullptr || !Destination->IsEmpty() || ++Index == Arguments.Size() || Arguments[Index].IsEmpty() || Arguments[Index].Substr(0, 2) == StringView("--"))
        {
            return false;
        }
        *Destination = Arguments[Index];
    }
    return !Out.Source.IsEmpty() && !Out.Id.IsEmpty() && !Out.Path.IsEmpty() && (Out.bCheckOnly == Out.Output.IsEmpty());
}

const char* DescribeFormatError(const EAssetFormatResult Result) noexcept
{
    switch (Result)
    {
    case EAssetFormatResult::Success:
        return "success";
    case EAssetFormatResult::InvalidHeader:
        return "invalid resource header";
    case EAssetFormatResult::UnsupportedVersion:
        return "unsupported resource version";
    case EAssetFormatResult::InvalidIdentity:
        return "invalid resource identity";
    case EAssetFormatResult::InvalidPath:
        return "invalid or non-canonical virtual path";
    case EAssetFormatResult::TypeMismatch:
        return "resource type mismatch";
    case EAssetFormatResult::LimitExceeded:
        return "resource exceeds v1 size limits";
    case EAssetFormatResult::InvalidPayload:
        return "invalid resource data";
    case EAssetFormatResult::AllocationFailed:
        return "could not allocate resource data";
    }
    return "unexpected resource format failure";
}

int ReportError(const StringView File, const char* Message, const std::size_t Line = 0) noexcept
{
    std::fputs("AssetImport: ", stderr);
    std::fwrite(File.Data(), 1, File.Size(), stderr);
    if (Line != 0)
    {
        std::fprintf(stderr, ":%zu", Line);
    }
    std::fprintf(stderr, ": %s\n", Message);
    return 1;
}

template <typename T> int ConvertSource(const FImportArguments& Args, const FAssetId Id, const StringView Source, FSourceError (*Parse)(StringView, T&) noexcept, Array<uint8>& OutBytes) noexcept
{
    T Data;
    const FSourceError Error = Parse(Source, Data);
    if (Error.Message != nullptr)
    {
        return ReportError(Args.Source, Error.Message, Error.Line);
    }
    const EAssetFormatResult Result = EncodeAssetFile(Id, Args.Path, Data, OutBytes);
    return Result == EAssetFormatResult::Success ? 0 : ReportError(Args.Source, DescribeFormatError(Result));
}

int PublishAsset(const FImportArguments& Args, const Array<uint8>& Bytes) noexcept
{
    Array<uint8> Prefix;
    uint64 ExistingSize = 0;
    const EFileSystemResult ReadResult = ReadContentFilePrefix(Args.Output, MaxAssetFileHeaderBytes, Prefix, ExistingSize);
    if (ReadResult == EFileSystemResult::Success)
    {
        if (ExistingSize > MaxAssetFileBytes)
        {
            return ReportError(Args.Output, "existing resource exceeds v1 size limits");
        }
        FAssetFileHeaderView Existing;
        const EAssetFormatResult ExistingResult = ReadAssetFileHeader(Prefix.AsSpan(), static_cast<std::size_t>(ExistingSize), Existing);
        if (ExistingResult != EAssetFormatResult::Success)
        {
            return ReportError(Args.Output, DescribeFormatError(ExistingResult));
        }
        FAssetFileHeaderView Replacement;
        if (ReadAssetFileHeader(Bytes.AsSpan(), Replacement) != EAssetFormatResult::Success)
        {
            return ReportError(Args.Output, "could not validate replacement header");
        }
        if (Existing.Id != Replacement.Id || Existing.TypeId != Replacement.TypeId || Existing.Path != Replacement.Path)
        {
            return ReportError(Args.Output, "existing resource has a different ID, type or virtual path");
        }
    }
    else if (ReadResult != EFileSystemResult::NotFound)
    {
        return ReportError(Args.Output, DescribeFileSystemResult(ReadResult));
    }

    const EFileSystemResult WriteResult = WriteContentFileAtomically(Args.Output, Bytes.AsSpan());
    return WriteResult == EFileSystemResult::Success ? 0 : ReportError(Args.Output, DescribeFileSystemResult(WriteResult));
}

void PrintUsage(FILE* Stream) noexcept
{
    std::fputs("Usage: AssetImport <mesh|texture|material> --source FILE --id UUID --path VIRTUAL "
               "(--output FILE.leasset | --check)\n",
        Stream);
}

} // namespace

int RunAssetImport(const Span<const StringView> Arguments) noexcept
{
    if (Arguments.Size() == 1 && Arguments[0] == StringView("--help"))
    {
        PrintUsage(stdout);
        return 0;
    }
    FImportArguments Args;
    if (!ParseArguments(Arguments, Args))
    {
        PrintUsage(stderr);
        return 2;
    }

    FAssetId Id;
    if (!FAssetId::TryParse(Args.Id, Id))
    {
        return ReportError(Args.Source, "expected a non-nil lowercase canonical UUID");
    }
    FSoftAssetPath Path;
    if (Args.Path.Size() > MaxAssetPathBytes || FSoftAssetPath::TryParse(Args.Path, Path) != ESoftAssetPathResult::Success || Path.View() != Args.Path)
    {
        return ReportError(Args.Source, "expected a canonical virtual asset path");
    }
    if (!Args.bCheckOnly && (Args.Output.Size() <= OutputExtension.Size() || Args.Output.Substr(Args.Output.Size() - OutputExtension.Size(), OutputExtension.Size()) != OutputExtension))
    {
        return ReportError(Args.Output, "expected a .leasset output file");
    }

    Array<uint8> SourceBytes;
    const std::size_t MaxSourceBytes = Args.Kind == EImportKind::Material ? MaxMaterialSourceBytes : MaxAssetFileBytes;
    const EFileSystemResult ReadResult = ReadContentFile(Args.Source, MaxSourceBytes, SourceBytes);
    if (ReadResult != EFileSystemResult::Success)
    {
        return ReportError(Args.Source, DescribeFileSystemResult(ReadResult));
    }

    const StringView Source(reinterpret_cast<const char*>(SourceBytes.Data()), SourceBytes.Size());
    Array<uint8> Bytes;
    int Result = 0;
    switch (Args.Kind)
    {
    case EImportKind::Mesh:
        Result = ConvertSource(Args, Id, Source, &ParseMesh, Bytes);
        break;
    case EImportKind::Texture:
        Result = ConvertSource(Args, Id, Source, &ParseTexture, Bytes);
        break;
    case EImportKind::Material:
        Result = ConvertSource(Args, Id, Source, &ParseMaterial, Bytes);
        break;
    }
    if (Result != 0 || Args.bCheckOnly)
    {
        return Result;
    }
    return PublishAsset(Args, Bytes);
}

} // namespace LE
