#include "AssetImport.h"

#include "Asset/SoftAssetPath.h"
#include "AssetFormat/AssetFormat.h"
#include "ContentIO/ContentIO.h"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <locale.h>

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

    StringView Next() noexcept
    {
        while (Offset < Text.Size())
        {
            if (IsWhitespace(Text[Offset]))
            {
                ++Offset;
            }
            else if (Text[Offset] == '#')
            {
                while (Offset < Text.Size() && Text[Offset] != '\n') { ++Offset; }
            }
            else
            {
                break;
            }
        }

        const std::size_t Start = Offset;
        while (Offset < Text.Size() && !IsWhitespace(Text[Offset]) && Text[Offset] != '#') { ++Offset; }
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
    static bool IsWhitespace(const char Value) noexcept
    {
        return Value == ' ' || Value == '\t' || Value == '\r' ||
            Value == '\n' || Value == '\v' || Value == '\f';
    }

    StringView Text;
    std::size_t Offset = 0;
};

class FDecimalParser
{
public:
#if PLATFORM_WINDOWS
    FDecimalParser() noexcept : Locale(_create_locale(LC_NUMERIC, "C")) {}
    ~FDecimalParser() { if (Locale != nullptr) { _free_locale(Locale); } }
#else
    FDecimalParser() noexcept : Locale(newlocale(LC_NUMERIC_MASK, "C", nullptr)) {}
    ~FDecimalParser() { if (Locale != nullptr) { freelocale(Locale); } }
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
            if ((Value < '0' || Value > '9') && Value != '.' && Value != '+' &&
                Value != '-' && Value != 'e' && Value != 'E')
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

bool ParseVector(FTokenCursor& Tokens, const FDecimalParser& Decimal, Math::Vector3f& Out) noexcept
{
    return Decimal.Parse(Tokens.Next(), Out[0]) && Decimal.Parse(Tokens.Next(), Out[1]) &&
        Decimal.Parse(Tokens.Next(), Out[2]) && Tokens.Next().IsEmpty();
}

StringView TextWithoutBom(const StringView Source) noexcept
{
    constexpr StringView Bom("\xef\xbb\xbf", 3);
    return Source.Substr(0, Bom.Size()) == Bom ? Source.Substr(Bom.Size(), Source.Size()) : Source;
}

FSourceError ParseMesh(const StringView Source, FMeshAssetData& Mesh) noexcept
{
    const FDecimalParser Decimal;
    if (!Decimal.IsValid())
    {
        return {"could not initialize decimal parser"};
    }

    const StringView Text = TextWithoutBom(Source);
    uint32 MaxIndex = 0;
    std::size_t MaxIndexLine = 0;
    for (std::size_t Offset = 0, Line = 1; Offset < Text.Size(); ++Line)
    {
        const std::size_t Start = Offset;
        while (Offset < Text.Size() && Text[Offset] != '\n') { ++Offset; }
        FTokenCursor Tokens(Text.Substr(Start, Offset - Start));
        if (Offset < Text.Size()) { ++Offset; }

        const StringView Kind = Tokens.Next();
        if (Kind == StringView("v"))
        {
            Math::Vector3f Position;
            if (!ParseVector(Tokens, Decimal, Position))
            {
                return {"expected three finite decimal vertex coordinates", Line};
            }
            if (Mesh.Positions.Size() >= MaxAssetMeshVertices)
            {
                return {"vertex limit exceeded", Line};
            }
            if (!Mesh.Positions.TryPushBack(Position))
            {
                return {"could not allocate a mesh vertex", Line};
            }
        }
        else if (Kind == StringView("f"))
        {
            if (Mesh.Indices.Size() > MaxAssetMeshIndices - 3)
            {
                return {"index limit exceeded", Line};
            }
            for (int Component = 0; Component < 3; ++Component)
            {
                uint32 Index = 0;
                if (!ParseUnsigned(Tokens.Next(), Index) || Index == 0 || Index > MaxAssetMeshVertices)
                {
                    return {"expected three positive position indices within v1 limits", Line};
                }
                if (Index > MaxIndex)
                {
                    MaxIndex = Index;
                    MaxIndexLine = Line;
                }
                if (!Mesh.Indices.TryPushBack(Index - 1))
                {
                    return {"could not allocate a mesh index", Line};
                }
            }
            if (!Tokens.Next().IsEmpty())
            {
                return {"only triangular faces are supported", Line};
            }
        }
        else if (!Kind.IsEmpty())
        {
            return {"unsupported OBJ record; expected v or f", Line};
        }
    }
    if (Mesh.Indices.IsEmpty())
    {
        return {"expected at least one triangle"};
    }
    return MaxIndex > Mesh.Positions.Size()
        ? FSourceError{"face index exceeds vertex count", MaxIndexLine} : FSourceError{};
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
    if (!ParseUnsigned(Tokens.Next(), Texture.Width) || !ParseUnsigned(Tokens.Next(), Texture.Height) ||
        !ParseUnsigned(Tokens.Next(), MaxValue) || MaxValue != 255)
    {
        return {"expected width, height and a maximum sample of 255"};
    }
    if (Texture.Width == 0 || Texture.Height == 0 ||
        Texture.Width > MaxAssetTextureDimension || Texture.Height > MaxAssetTextureDimension)
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
    return bTextRaster && !Tokens.Next().IsEmpty()
        ? FSourceError{"unexpected data after final RGB sample"} : FSourceError{};
}

FSourceError ParseMaterial(const StringView Source, FMaterialAssetData& Material) noexcept
{
    const FDecimalParser Decimal;
    if (!Decimal.IsValid())
    {
        return {"could not initialize decimal parser"};
    }

    FTokenCursor Tokens(TextWithoutBom(Source));
    if (Tokens.Next() != StringView("base-color") || !ParseVector(Tokens, Decimal, Material.BaseColor) ||
        Material.BaseColor[0] < 0.0f || Material.BaseColor[1] < 0.0f || Material.BaseColor[2] < 0.0f)
    {
        return {"expected base-color followed by three finite non-negative decimal components"};
    }
    return {};
}

bool ParseArguments(const Span<const StringView> Arguments, FImportArguments& Out) noexcept
{
    if (Arguments.IsEmpty()) { return false; }
    if (Arguments[0] == StringView("mesh")) { Out.Kind = EImportKind::Mesh; }
    else if (Arguments[0] == StringView("texture")) { Out.Kind = EImportKind::Texture; }
    else if (Arguments[0] == StringView("material")) { Out.Kind = EImportKind::Material; }
    else { return false; }

    for (std::size_t Index = 1; Index < Arguments.Size(); ++Index)
    {
        const StringView Flag = Arguments[Index];
        if (Flag == StringView("--check"))
        {
            if (Out.bCheckOnly) { return false; }
            Out.bCheckOnly = true;
            continue;
        }
        StringView* Destination = nullptr;
        if (Flag == StringView("--source")) { Destination = &Out.Source; }
        else if (Flag == StringView("--id")) { Destination = &Out.Id; }
        else if (Flag == StringView("--path")) { Destination = &Out.Path; }
        else if (Flag == StringView("--output")) { Destination = &Out.Output; }
        if (Destination == nullptr || !Destination->IsEmpty() || ++Index == Arguments.Size() ||
            Arguments[Index].IsEmpty() || Arguments[Index].Substr(0, 2) == StringView("--"))
        {
            return false;
        }
        *Destination = Arguments[Index];
    }
    return !Out.Source.IsEmpty() && !Out.Id.IsEmpty() && !Out.Path.IsEmpty() &&
        (Out.bCheckOnly == Out.Output.IsEmpty());
}

const char* DescribeFormatError(const EAssetFormatResult Result) noexcept
{
    switch (Result)
    {
    case EAssetFormatResult::Success: return "success";
    case EAssetFormatResult::InvalidHeader: return "invalid resource header";
    case EAssetFormatResult::UnsupportedVersion: return "unsupported resource version";
    case EAssetFormatResult::InvalidIdentity: return "invalid resource identity";
    case EAssetFormatResult::InvalidPath: return "invalid or non-canonical virtual path";
    case EAssetFormatResult::TypeMismatch: return "resource type mismatch";
    case EAssetFormatResult::LimitExceeded: return "resource exceeds v1 size limits";
    case EAssetFormatResult::InvalidPayload: return "invalid resource data";
    case EAssetFormatResult::AllocationFailed: return "could not allocate resource data";
    }
    return "unexpected resource format failure";
}

int ReportError(const StringView File, const char* Message, const std::size_t Line = 0) noexcept
{
    std::fputs("AssetImport: ", stderr);
    std::fwrite(File.Data(), 1, File.Size(), stderr);
    if (Line != 0) { std::fprintf(stderr, ":%zu", Line); }
    std::fprintf(stderr, ": %s\n", Message);
    return 1;
}

template <typename T>
int ConvertSource(
    const FImportArguments& Args,
    const FAssetId Id,
    const StringView Source,
    FSourceError (*Parse)(StringView, T&) noexcept,
    Array<uint8>& OutBytes) noexcept
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
    const EContentIOResult ReadResult =
        ReadContentFilePrefix(Args.Output, MaxAssetFileHeaderBytes, Prefix, ExistingSize);
    if (ReadResult == EContentIOResult::Success)
    {
        if (ExistingSize > MaxAssetFileBytes)
        {
            return ReportError(Args.Output, "existing resource exceeds v1 size limits");
        }
        FAssetFileHeaderView Existing;
        const EAssetFormatResult ExistingResult =
            ReadAssetFileHeader(Prefix.AsSpan(), static_cast<std::size_t>(ExistingSize), Existing);
        if (ExistingResult != EAssetFormatResult::Success)
        {
            return ReportError(Args.Output, DescribeFormatError(ExistingResult));
        }
        FAssetFileHeaderView Replacement;
        if (ReadAssetFileHeader(Bytes.AsSpan(), Replacement) != EAssetFormatResult::Success)
        {
            return ReportError(Args.Output, "could not validate replacement header");
        }
        if (Existing.Id != Replacement.Id || Existing.TypeId != Replacement.TypeId ||
            Existing.Path != Replacement.Path)
        {
            return ReportError(Args.Output, "existing resource has a different ID, type or virtual path");
        }
    }
    else if (ReadResult != EContentIOResult::NotFound)
    {
        return ReportError(Args.Output, DescribeContentIOResult(ReadResult));
    }

    const EContentIOResult WriteResult = WriteContentFileAtomically(Args.Output, Bytes.AsSpan());
    return WriteResult == EContentIOResult::Success ? 0 : ReportError(Args.Output, DescribeContentIOResult(WriteResult));
}

void PrintUsage(FILE* Stream) noexcept
{
    std::fputs(
        "Usage: AssetImport <mesh|texture|material> --source FILE --id UUID --path VIRTUAL "
        "(--output FILE.leasset | --check)\n", Stream);
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
    if (Args.Path.Size() > MaxAssetPathBytes ||
        FSoftAssetPath::TryParse(Args.Path, Path) != ESoftAssetPathResult::Success ||
        Path.View() != Args.Path)
    {
        return ReportError(Args.Source, "expected a canonical virtual asset path");
    }
    if (!Args.bCheckOnly &&
        (Args.Output.Size() <= OutputExtension.Size() ||
            Args.Output.Substr(Args.Output.Size() - OutputExtension.Size(), OutputExtension.Size()) != OutputExtension))
    {
        return ReportError(Args.Output, "expected a .leasset output file");
    }

    Array<uint8> SourceBytes;
    const std::size_t MaxSourceBytes = Args.Kind == EImportKind::Material ? MaxMaterialSourceBytes : MaxAssetFileBytes;
    const EContentIOResult ReadResult = ReadContentFile(Args.Source, MaxSourceBytes, SourceBytes);
    if (ReadResult != EContentIOResult::Success)
    {
        return ReportError(Args.Source, DescribeContentIOResult(ReadResult));
    }

    const StringView Source(reinterpret_cast<const char*>(SourceBytes.Data()), SourceBytes.Size());
    Array<uint8> Bytes;
    int Result = 0;
    switch (Args.Kind)
    {
    case EImportKind::Mesh: Result = ConvertSource(Args, Id, Source, &ParseMesh, Bytes); break;
    case EImportKind::Texture: Result = ConvertSource(Args, Id, Source, &ParseTexture, Bytes); break;
    case EImportKind::Material: Result = ConvertSource(Args, Id, Source, &ParseMaterial, Bytes); break;
    }
    if (Result != 0 || Args.bCheckOnly)
    {
        return Result;
    }
    return PublishAsset(Args, Bytes);
}

} // namespace LE
