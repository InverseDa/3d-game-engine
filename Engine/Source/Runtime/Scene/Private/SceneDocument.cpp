#include "Scene/SceneDocument.h"

#include "Containers/HashMap.h"
#include "World/DirectionalLight.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <locale.h>
#include <utility>

#if PLATFORM_MAC
#include <xlocale.h>
#endif

namespace LE
{
namespace
{

constexpr StringView EntityKeys[] = {
    {"id", 2}, {"parent", 6}, {"transform", 9}, {"mesh", 4}, {"camera", 6}, {"directionalLight", 16},
};
constexpr StringView TransformKeys[] = {
    {"translation", 11}, {"rotation", 8}, {"scale", 5},
};
constexpr StringView MeshKeys[] = {
    {"mesh", 4}, {"material", 8}, {"debugName", 9}, {"sortKey", 7},
};
constexpr StringView CameraKeys[] = {
    {"debugName", 9}, {"verticalFieldOfViewRadians", 26},
    {"nearPlane", 9}, {"farPlane", 8}, {"priority", 8}, {"active", 6},
};
constexpr StringView DirectionalLightKeys[] = {
    {"direction", 9}, {"color", 5}, {"intensity", 9}, {"active", 6},
};
constexpr StringView DocumentKeys[] = {
    {"version", 7}, {"entities", 8},
};
constexpr uint32 LegacySceneDocumentVersion = 1;
constexpr std::size_t MaxNumberBytes = 128;

bool IsDigit(const char Byte) noexcept { return Byte >= '0' && Byte <= '9'; }

// Returns the width of one valid Unicode scalar, or zero for ill-formed UTF-8.
std::size_t ScalarWidth(const StringView Text, const std::size_t Offset) noexcept
{
    const auto First = static_cast<uint8>(Text[Offset]);
    if (First < 0x80) { return 1; }
    std::size_t Width = 0;
    uint32 Scalar = 0;
    uint32 Minimum = 0;
    if (First >= 0xc2 && First <= 0xdf)
    {
        Width = 2;
        Scalar = First & 0x1f;
        Minimum = 0x80;
    }
    else if (First >= 0xe0 && First <= 0xef)
    {
        Width = 3;
        Scalar = First & 0x0f;
        Minimum = 0x800;
    }
    else if (First >= 0xf0 && First <= 0xf4)
    {
        Width = 4;
        Scalar = First & 0x07;
        Minimum = 0x10000;
    }
    if (Width == 0 || Width > Text.Size() - Offset)
    {
        return 0;
    }
    for (std::size_t Index = 1; Index < Width; ++Index)
    {
        const auto Byte = static_cast<uint8>(Text[Offset + Index]);
        if ((Byte & 0xc0) != 0x80) { return 0; }
        Scalar = (Scalar << 6) | (Byte & 0x3f);
    }
    return Scalar >= Minimum && Scalar <= 0x10ffff && (Scalar < 0xd800 || Scalar > 0xdfff)
        ? Width : 0;
}

bool IsValidName(const StringView Text) noexcept
{
    for (std::size_t Offset = 0; Offset < Text.Size();)
    {
        const std::size_t Width = ScalarWidth(Text, Offset);
        if (Width == 0 || Text[Offset] == '\0')
        {
            return false;
        }
        Offset += Width;
    }
    return true;
}

std::size_t EncodeScalar(const uint32 Scalar, char (&Bytes)[4]) noexcept
{
    if (Scalar < 0x80)
    {
        Bytes[0] = static_cast<char>(Scalar);
        return 1;
    }
    if (Scalar < 0x800)
    {
        Bytes[0] = static_cast<char>(0xc0 | (Scalar >> 6));
        Bytes[1] = static_cast<char>(0x80 | (Scalar & 0x3f));
        return 2;
    }
    if (Scalar < 0x10000)
    {
        Bytes[0] = static_cast<char>(0xe0 | (Scalar >> 12));
        Bytes[1] = static_cast<char>(0x80 | ((Scalar >> 6) & 0x3f));
        Bytes[2] = static_cast<char>(0x80 | (Scalar & 0x3f));
        return 3;
    }
    Bytes[0] = static_cast<char>(0xf0 | (Scalar >> 18));
    Bytes[1] = static_cast<char>(0x80 | ((Scalar >> 12) & 0x3f));
    Bytes[2] = static_cast<char>(0x80 | ((Scalar >> 6) & 0x3f));
    Bytes[3] = static_cast<char>(0x80 | (Scalar & 0x3f));
    return 4;
}

class FSceneNumericLocale
{
public:
#if PLATFORM_WINDOWS
    FSceneNumericLocale() noexcept : Locale(_create_locale(LC_NUMERIC, "C")) {}
    ~FSceneNumericLocale() { if (Locale != nullptr) { _free_locale(Locale); } }
#else
    FSceneNumericLocale() noexcept : Locale(newlocale(LC_NUMERIC_MASK, "C", nullptr)) {}
    ~FSceneNumericLocale() { if (Locale != nullptr) { freelocale(Locale); } }
#endif

    FSceneNumericLocale(const FSceneNumericLocale&) = delete;
    FSceneNumericLocale& operator=(const FSceneNumericLocale&) = delete;
    bool IsValid() const noexcept { return Locale != nullptr; }

    float Parse(const char* Buffer, char** End) const noexcept
    {
#if PLATFORM_WINDOWS
        return _strtof_l(Buffer, End, Locale);
#else
        return strtof_l(Buffer, End, Locale);
#endif
    }

    int Format(char* Buffer, const std::size_t Size, const float Value) const noexcept
    {
        constexpr int Precision = std::numeric_limits<float>::max_digits10;
#if PLATFORM_WINDOWS
        return _snprintf_l(Buffer, Size, "%.*g", Locale, Precision, static_cast<double>(Value));
#else
        return snprintf_l(Buffer, Size, Locale, "%.*g", Precision, static_cast<double>(Value));
#endif
    }

private:
#if PLATFORM_WINDOWS
    _locale_t Locale;
#else
    locale_t Locale;
#endif
};

using EResult = ESceneDocumentResult;

EResult Report(FSceneDocumentDiagnostic* Diagnostic, const EResult Result,
    const StringView Field = {}, const FSceneEntityId Entity = {}) noexcept
{
    if (Diagnostic != nullptr)
    {
        *Diagnostic = {Result, 0, 0, 0, Entity, Field};
    }
    return Result;
}

void SetLocation(FSceneDocumentDiagnostic& Diagnostic, const StringView Source,
    const std::size_t Offset) noexcept
{
    Diagnostic.Offset = Offset;
    Diagnostic.Line = 1;
    Diagnostic.Column = 1;
    for (std::size_t Index = 0; Index < Offset; ++Index)
    {
        if (Source[Index] == '\n')
        {
            ++Diagnostic.Line;
            Diagnostic.Column = 1;
        }
        else
        {
            ++Diagnostic.Column;
        }
    }
}

struct FEntitySourceLocation
{
    std::size_t Record = 0;
    std::size_t Entity[sizeof(EntityKeys) / sizeof(EntityKeys[0])]{};
    std::size_t Transform[sizeof(TransformKeys) / sizeof(TransformKeys[0])]{};
    std::size_t Mesh[sizeof(MeshKeys) / sizeof(MeshKeys[0])]{};
    std::size_t Camera[sizeof(CameraKeys) / sizeof(CameraKeys[0])]{};
    std::size_t DirectionalLight[sizeof(DirectionalLightKeys) / sizeof(DirectionalLightKeys[0])]{};

    Span<std::size_t> ForObject(const StringView* Keys) noexcept
    {
        if (Keys == EntityKeys) { return Entity; }
        if (Keys == TransformKeys) { return Transform; }
        if (Keys == MeshKeys) { return Mesh; }
        if (Keys == CameraKeys) { return Camera; }
        if (Keys == DirectionalLightKeys) { return DirectionalLight; }
        return {};
    }

    std::size_t ForField(const StringView* Field) const noexcept
    {
        for (std::size_t Index = 0; Index < sizeof(Entity) / sizeof(Entity[0]); ++Index)
        {
            if (Field == &EntityKeys[Index] && Entity[Index] != 0) { return Entity[Index]; }
        }
        for (std::size_t Index = 0; Index < sizeof(Transform) / sizeof(Transform[0]); ++Index)
        {
            if (Field == &TransformKeys[Index] && Transform[Index] != 0) { return Transform[Index]; }
        }
        for (std::size_t Index = 0; Index < sizeof(Mesh) / sizeof(Mesh[0]); ++Index)
        {
            if (Field == &MeshKeys[Index] && Mesh[Index] != 0) { return Mesh[Index]; }
        }
        for (std::size_t Index = 0; Index < sizeof(Camera) / sizeof(Camera[0]); ++Index)
        {
            if (Field == &CameraKeys[Index] && Camera[Index] != 0) { return Camera[Index]; }
        }
        for (std::size_t Index = 0; Index < sizeof(DirectionalLight) / sizeof(DirectionalLight[0]); ++Index)
        {
            if (Field == &DirectionalLightKeys[Index] && DirectionalLight[Index] != 0)
            {
                return DirectionalLight[Index];
            }
        }
        return Record;
    }
};

class FSceneReader
{
public:
    FSceneReader(const StringView InSource, IAllocator& InAllocator, FSceneDocumentDiagnostic* InDiagnostic) noexcept
        : Source(InSource), Allocator(InAllocator), Diagnostic(InDiagnostic), Key(InAllocator) {}

    EResult Read(FSceneDocument& Candidate, Array<FEntitySourceLocation>& Locations, uint32& Version) noexcept;

private:
    bool Fail(EResult Result, std::size_t At) noexcept;
    bool Fail(const EResult Result) noexcept { return Fail(Result, Offset); }
    void SkipWhitespace() noexcept;
    bool Consume(char Byte) noexcept;
    bool Require(const char Byte) noexcept { return Consume(Byte) || Fail(EResult::InvalidJson); }
    bool ReadObjectField(Span<const StringView> Keys, uint32& Seen, bool& HasField, std::size_t& Slot) noexcept;
    bool RequireFields(Span<const StringView> Keys, uint32 Seen, uint32 Required) noexcept;
    bool ReadString(String& Out, std::size_t Limit) noexcept;
    bool ReadEscape(char (&Bytes)[4], std::size_t& Width) noexcept;
    bool ReadHexQuad(uint32& Out) noexcept;
    bool ReadNumber(StringView& Out) noexcept;
    bool ReadFloat(float& Out) noexcept;
    bool ReadBoolean(bool& Out) noexcept;
    bool ReadSortKey(uint64& Out) noexcept;

    template <typename Integer> bool ReadInteger(Integer& Out) noexcept;
    template <typename Id> bool ReadId(Id& Out) noexcept;

    bool ReadEntity(FSceneEntityRecord& Entity) noexcept;
    bool ReadTransform(FTransform& Out) noexcept;
    bool ReadMesh(FSceneMeshRecord& Out) noexcept;
    bool ReadCamera(FSceneCameraRecord& Out) noexcept;
    bool ReadDirectionalLight(FSceneDirectionalLightRecord& Out, uint32& Seen) noexcept;
    bool ReadVector(Span<float> Values) noexcept;
    bool ReadEntities(FSceneDocument& Candidate, Array<FEntitySourceLocation>& Locations) noexcept;

    StringView Source;
    IAllocator& Allocator;
    FSceneDocumentDiagnostic* Diagnostic;
    FSceneNumericLocale Locale;
    String Key;
    FEntitySourceLocation* EntityLocation = nullptr;
    FSceneEntityId CurrentEntity;
    StringView Field;
    std::size_t Offset = 0;
    EResult Error = EResult::Success;
};

bool FSceneReader::Fail(const EResult Result, const std::size_t At) noexcept
{
    if (Error == EResult::Success)
    {
        Error = Result;
        Report(Diagnostic, Result, Field, CurrentEntity);
        if (Diagnostic != nullptr)
        {
            SetLocation(*Diagnostic, Source, At);
        }
    }
    return false;
}

void FSceneReader::SkipWhitespace() noexcept
{
    while (Offset < Source.Size())
    {
        const char Byte = Source[Offset];
        if (Byte != ' ' && Byte != '\t' && Byte != '\r' && Byte != '\n') { break; }
        ++Offset;
    }
}

bool FSceneReader::Consume(const char Byte) noexcept
{
    SkipWhitespace();
    if (Offset == Source.Size() || Source[Offset] != Byte) { return false; }
    ++Offset;
    return true;
}

bool FSceneReader::ReadObjectField(const Span<const StringView> Keys,
    uint32& Seen, bool& HasField, std::size_t& Slot) noexcept
{
    if (Consume('}'))
    {
        HasField = false;
        return true;
    }
    Field = {};
    if (Seen != 0 && !Require(',')) { return false; }
    SkipWhitespace();
    const std::size_t KeyOffset = Offset;
    if (!ReadString(Key, MaxSceneNameBytes)) { return false; }
    Slot = 0;
    while (Slot < Keys.Size() && Key.View() != Keys[Slot]) { ++Slot; }
    if (Slot == Keys.Size())
    {
        if (Keys.Data() == DirectionalLightKeys) { Field = EntityKeys[5]; }
        return Fail(EResult::UnknownField, KeyOffset);
    }
    Field = Keys[Slot];
    const uint32 Bit = 1u << Slot;
    if ((Seen & Bit) != 0) { return Fail(EResult::DuplicateField, KeyOffset); }
    if (EntityLocation != nullptr)
    {
        const auto Locations = EntityLocation->ForObject(Keys.Data());
        if (!Locations.IsEmpty()) { Locations[Slot] = KeyOffset; }
    }
    Seen |= Bit;
    HasField = true;
    return Require(':');
}

bool FSceneReader::RequireFields(
    const Span<const StringView> Keys, const uint32 Seen, const uint32 Required) noexcept
{
    for (std::size_t Index = 0; Index < Keys.Size(); ++Index)
    {
        const uint32 Bit = 1u << Index;
        if ((Required & Bit) != 0 && (Seen & Bit) == 0)
        {
            Field = Keys[Index];
            return Fail(EResult::MissingField, Offset - 1);
        }
    }
    return true;
}

bool FSceneReader::ReadHexQuad(uint32& Out) noexcept
{
    if (Source.Size() - Offset < 4) { return Fail(EResult::InvalidJson); }
    Out = 0;
    for (int Index = 0; Index < 4; ++Index)
    {
        const char Byte = Source[Offset++];
        uint32 Digit = 0;
        if (IsDigit(Byte)) { Digit = static_cast<uint32>(Byte - '0'); }
        else if (Byte >= 'a' && Byte <= 'f') { Digit = static_cast<uint32>(Byte - 'a' + 10); }
        else if (Byte >= 'A' && Byte <= 'F') { Digit = static_cast<uint32>(Byte - 'A' + 10); }
        else { return Fail(EResult::InvalidJson, Offset - 1); }
        Out = (Out << 4) | Digit;
    }
    return true;
}

bool FSceneReader::ReadEscape(char (&Bytes)[4], std::size_t& Width) noexcept
{
    if (Offset == Source.Size()) { return Fail(EResult::InvalidJson); }
    Width = 1;
    const char Byte = Source[Offset++];
    switch (Byte)
    {
    case '"':
    case '/':
    case '\\': Bytes[0] = Byte; return true;
    case 'b': Bytes[0] = '\b'; return true;
    case 'f': Bytes[0] = '\f'; return true;
    case 'n': Bytes[0] = '\n'; return true;
    case 'r': Bytes[0] = '\r'; return true;
    case 't': Bytes[0] = '\t'; return true;
    case 'u': break;
    default: return Fail(EResult::InvalidJson, Offset - 1);
    }
    const std::size_t ScalarOffset = Offset;
    uint32 Scalar = 0;
    if (!ReadHexQuad(Scalar)) { return false; }
    if (Scalar >= 0xd800 && Scalar <= 0xdbff)
    {
        if (Source.Substr(Offset, 2) != StringView("\\u", 2))
        {
            return Fail(EResult::InvalidJson, ScalarOffset);
        }
        Offset += 2;
        uint32 Low = 0;
        if (!ReadHexQuad(Low)) { return false; }
        if (Low < 0xdc00 || Low > 0xdfff) { return Fail(EResult::InvalidJson, Offset - 4); }
        Scalar = 0x10000 + ((Scalar - 0xd800) << 10) + (Low - 0xdc00);
    }
    else if (Scalar >= 0xdc00 && Scalar <= 0xdfff)
    {
        return Fail(EResult::InvalidJson, ScalarOffset);
    }
    if (Scalar == 0) { return Fail(EResult::InvalidValue, ScalarOffset); }
    Width = EncodeScalar(Scalar, Bytes);
    return true;
}

bool FSceneReader::ReadString(String& Out, const std::size_t Limit) noexcept
{
    if (!Require('"')) { return false; }
    Out.Clear();
    while (Offset < Source.Size())
    {
        if (Source[Offset] == '"')
        {
            ++Offset;
            return true;
        }
        const std::size_t Start = Offset;
        char Decoded[4];
        const char* Bytes = Source.Data() + Start;
        std::size_t Width = 0;
        if (Source[Offset] == '\\')
        {
            ++Offset;
            if (!ReadEscape(Decoded, Width)) { return false; }
            Bytes = Decoded;
        }
        else
        {
            Width = ScalarWidth(Source, Offset);
            if (Width == 0 || static_cast<uint8>(Source[Offset]) < 0x20)
            {
                return Fail(EResult::InvalidJson);
            }
            Offset += Width;
        }
        if (Width > Limit - Out.Size()) { return Fail(EResult::LimitExceeded, Start); }
        if (!Out.TryAppend(StringView(Bytes, Width))) { return Fail(EResult::AllocationFailed, Start); }
    }
    return Fail(EResult::InvalidJson);
}

bool FSceneReader::ReadNumber(StringView& Out) noexcept
{
    SkipWhitespace();
    const std::size_t Start = Offset;
    const auto At = [this](const char Byte) { return Offset < Source.Size() && Source[Offset] == Byte; };
    const auto HasDigit = [this]() { return Offset < Source.Size() && IsDigit(Source[Offset]); };
    if (At('-')) { ++Offset; }
    if (At('0'))
    {
        ++Offset;
        if (HasDigit()) { return Fail(EResult::InvalidJson); }
    }
    else
    {
        if (!HasDigit()) { return Fail(EResult::InvalidJson); }
        while (HasDigit()) { ++Offset; }
    }
    if (At('.'))
    {
        ++Offset;
        if (!HasDigit()) { return Fail(EResult::InvalidJson); }
        while (HasDigit()) { ++Offset; }
    }
    if (At('e') || At('E'))
    {
        ++Offset;
        if (At('+') || At('-')) { ++Offset; }
        if (!HasDigit()) { return Fail(EResult::InvalidJson); }
        while (HasDigit()) { ++Offset; }
    }
    if (Offset - Start >= MaxNumberBytes) { return Fail(EResult::LimitExceeded, Start); }
    Out = Source.Substr(Start, Offset - Start);
    return true;
}

bool FSceneReader::ReadFloat(float& Out) noexcept
{
    StringView Number;
    if (!ReadNumber(Number)) { return false; }
    char Buffer[MaxNumberBytes]{};
    std::memcpy(Buffer, Number.Data(), Number.Size());
    char* End = nullptr;
    errno = 0;
    const float Parsed = Locale.Parse(Buffer, &End);
    if (End != Buffer + Number.Size() || !std::isfinite(Parsed) || (errno == ERANGE && Parsed == 0.0f))
    {
        return Fail(EResult::InvalidValue, Offset - Number.Size());
    }
    Out = Parsed;
    return true;
}

template <typename Integer>
bool FSceneReader::ReadInteger(Integer& Out) noexcept
{
    StringView Number;
    if (!ReadNumber(Number)) { return false; }
    const auto* End = Number.Data() + Number.Size();
    const auto Parsed = std::from_chars(Number.Data(), End, Out);
    return (Parsed.ec == std::errc{} && Parsed.ptr == End) ||
        Fail(EResult::InvalidValue, Offset - Number.Size());
}

bool FSceneReader::ReadBoolean(bool& Out) noexcept
{
    SkipWhitespace();
    if (Source.Substr(Offset, 4) == StringView("true", 4))
    {
        Offset += 4;
        Out = true;
        return true;
    }
    if (Source.Substr(Offset, 5) == StringView("false", 5))
    {
        Offset += 5;
        Out = false;
        return true;
    }
    return Fail(EResult::InvalidJson);
}

bool FSceneReader::ReadSortKey(uint64& Out) noexcept
{
    SkipWhitespace();
    const std::size_t Start = Offset;
    String Value(Allocator);
    if (!ReadString(Value, 20)) { return false; }
    if (Value.IsEmpty() || (Value.Size() > 1 && Value.View()[0] == '0'))
    {
        return Fail(EResult::InvalidValue, Start);
    }
    const auto* End = Value.Data() + Value.Size();
    const auto Parsed = std::from_chars(Value.Data(), End, Out);
    return (Parsed.ec == std::errc{} && Parsed.ptr == End) || Fail(EResult::InvalidValue, Start);
}

template <typename Id>
bool FSceneReader::ReadId(Id& Out) noexcept
{
    SkipWhitespace();
    const std::size_t Start = Offset;
    String Value(Allocator);
    return ReadString(Value, FUuid::CanonicalStringLength) &&
        (Id::TryParse(Value.View(), Out) || Fail(EResult::InvalidValue, Start));
}

bool FSceneReader::ReadVector(const Span<float> Values) noexcept
{
    if (!Require('[')) { return false; }
    for (std::size_t Index = 0; Index < Values.Size(); ++Index)
    {
        if ((Index != 0 && !Require(',')) || !ReadFloat(Values[Index])) { return false; }
    }
    return Require(']');
}

bool FSceneReader::ReadTransform(FTransform& Out) noexcept
{
    if (!Require('{')) { return false; }
    uint32 Seen = 0;
    bool HasField = true;
    std::size_t Slot = 0;
    while (ReadObjectField(TransformKeys, Seen, HasField, Slot) && HasField)
    {
        if (Slot == 0 && !ReadVector(Out.Translation.Values)) { return false; }
        if (Slot == 2 && !ReadVector(Out.Scale.Values)) { return false; }
        if (Slot == 1)
        {
            float Quaternion[4];
            if (!ReadVector(Quaternion)) { return false; }
            Out.Rotation = Math::Quaternionf(Quaternion[0], Quaternion[1], Quaternion[2], Quaternion[3]);
        }
    }
    return Error == EResult::Success && RequireFields(TransformKeys, Seen, 0b111);
}

bool FSceneReader::ReadMesh(FSceneMeshRecord& Out) noexcept
{
    if (!Require('{')) { return false; }
    uint32 Seen = 0;
    bool HasField = true;
    std::size_t Slot = 0;
    while (ReadObjectField(MeshKeys, Seen, HasField, Slot) && HasField)
    {
        if (Slot == 0 && !ReadId(Out.Mesh)) { return false; }
        if (Slot == 1 && !ReadId(Out.Material)) { return false; }
        if (Slot == 2 && !ReadString(Out.DebugName, MaxSceneNameBytes)) { return false; }
        if (Slot == 3 && !ReadSortKey(Out.SortKey)) { return false; }
    }
    return Error == EResult::Success && RequireFields(MeshKeys, Seen, 0b1111);
}

bool FSceneReader::ReadCamera(FSceneCameraRecord& Out) noexcept
{
    if (!Require('{')) { return false; }
    uint32 Seen = 0;
    bool HasField = true;
    std::size_t Slot = 0;
    while (ReadObjectField(CameraKeys, Seen, HasField, Slot) && HasField)
    {
        if (Slot == 0 && !ReadString(Out.DebugName, MaxSceneNameBytes)) { return false; }
        if (Slot == 1 && !ReadFloat(Out.VerticalFieldOfViewRadians)) { return false; }
        if (Slot == 2 && !ReadFloat(Out.NearPlane)) { return false; }
        if (Slot == 3 && !ReadFloat(Out.FarPlane)) { return false; }
        if (Slot == 4 && !ReadInteger(Out.Priority)) { return false; }
        if (Slot == 5 && !ReadBoolean(Out.bActive)) { return false; }
    }
    return Error == EResult::Success && RequireFields(CameraKeys, Seen, 0b111111);
}

bool FSceneReader::ReadDirectionalLight(FSceneDirectionalLightRecord& Out, uint32& Seen) noexcept
{
    if (!Require('{')) { return false; }
    bool HasField = true;
    std::size_t Slot = 0;
    while (ReadObjectField(DirectionalLightKeys, Seen, HasField, Slot) && HasField)
    {
        if (Slot == 0 && !ReadVector(Out.Direction.Values)) { return false; }
        if (Slot == 1 && !ReadVector(Out.Color.Values)) { return false; }
        if (Slot == 2 && !ReadFloat(Out.Intensity)) { return false; }
        if (Slot == 3 && !ReadBoolean(Out.bActive)) { return false; }
    }
    return Error == EResult::Success;
}

bool FSceneReader::ReadEntity(FSceneEntityRecord& Entity) noexcept
{
    if (!Require('{')) { return false; }
    uint32 Seen = 0;
    uint32 SeenLightFields = 0;
    std::size_t LightEnd = 0;
    bool HasField = true;
    std::size_t Slot = 0;
    CurrentEntity = {};
    while (ReadObjectField(EntityKeys, Seen, HasField, Slot) && HasField)
    {
        switch (Slot)
        {
        case 0:
            if (!ReadId(Entity.Id)) { return false; }
            CurrentEntity = Entity.Id;
            break;
        case 1:
            if (!ReadId(Entity.Parent)) { return false; }
            break;
        case 2:
            if (!ReadTransform(Entity.Transform)) { return false; }
            Entity.bHasTransform = true;
            break;
        case 3:
            if (!ReadMesh(Entity.Mesh)) { return false; }
            Entity.bHasMesh = true;
            break;
        case 4:
            if (!ReadCamera(Entity.Camera)) { return false; }
            Entity.bHasCamera = true;
            break;
        case 5:
            if (!ReadDirectionalLight(Entity.DirectionalLight, SeenLightFields)) { return false; }
            LightEnd = Offset;
            Entity.bHasDirectionalLight = true;
            break;
        }
    }
    if (Error != EResult::Success || !RequireFields(EntityKeys, Seen, 1)) { return false; }
    if (Entity.bHasDirectionalLight && SeenLightFields != 0b1111)
    {
        Offset = LightEnd;
        return RequireFields(DirectionalLightKeys, SeenLightFields, 0b1111);
    }
    return true;
}

bool FSceneReader::ReadEntities(FSceneDocument& Candidate, Array<FEntitySourceLocation>& Locations) noexcept
{
    if (!Require('[')) { return false; }
    if (Consume(']')) { return true; }
    do
    {
        SkipWhitespace();
        const std::size_t At = Offset;
        if (Candidate.Entities.Size() >= MaxSceneEntities)
        {
            return Fail(EResult::LimitExceeded);
        }
        FSceneEntityRecord Entity(Allocator);
        FEntitySourceLocation Location;
        Location.Record = At;
        EntityLocation = Diagnostic != nullptr ? &Location : nullptr;
        const bool bParsed = ReadEntity(Entity);
        EntityLocation = nullptr;
        if (!bParsed) { return false; }
        if (!Candidate.Entities.TryPushBack(std::move(Entity)) ||
            (Diagnostic != nullptr && !Locations.TryPushBack(Location)))
        {
            return Fail(EResult::AllocationFailed, At);
        }
        CurrentEntity = {};
        Field = DocumentKeys[1];
        if (Consume(']')) { return true; }
    }
    while (Require(','));
    return false;
}

EResult FSceneReader::Read(FSceneDocument& Candidate, Array<FEntitySourceLocation>& Locations,
    uint32& Version) noexcept
{
    if (!Locale.IsValid()) { return Report(Diagnostic, EResult::AllocationFailed); }
    if (Source.Substr(0, 3) == StringView("\xef\xbb\xbf", 3)) { Offset = 3; }
    if (!Require('{')) { return Error; }
    uint32 Seen = 0;
    bool HasField = true;
    std::size_t Slot = 0;
    while (ReadObjectField(DocumentKeys, Seen, HasField, Slot) && HasField)
    {
        if (Slot == 0)
        {
            SkipWhitespace();
            const std::size_t At = Offset;
            if (!ReadInteger(Version)) { return Error; }
            if (Version != LegacySceneDocumentVersion && Version != SceneDocumentVersion)
            {
                Fail(EResult::UnsupportedVersion, At);
                return Error;
            }
        }
        else if (!ReadEntities(Candidate, Locations))
        {
            return Error;
        }
    }
    if (Error == EResult::Success && RequireFields(DocumentKeys, Seen, 0b11))
    {
        SkipWhitespace();
        if (Offset != Source.Size()) { Fail(EResult::InvalidJson); }
    }
    return Error;
}

struct FValidationResult
{
    EResult Result = EResult::Success;
    const StringView* Field = nullptr;
    std::size_t RecordIndex = MaxSceneEntities;
};

bool IsFiniteVector(const Math::Vector3f& Values) noexcept
{
    return std::isfinite(Values[0]) && std::isfinite(Values[1]) && std::isfinite(Values[2]);
}

bool IsValidRotation(const Math::Quaternionf& Value) noexcept
{
    return std::isfinite(Value.x) && std::isfinite(Value.y) && std::isfinite(Value.z) &&
        std::isfinite(Value.w) && (Value.x != 0.0f || Value.y != 0.0f || Value.z != 0.0f || Value.w != 0.0f);
}

FValidationResult Validate(const FSceneDocument& Document, IAllocator& Allocator,
    const uint32 Version = SceneDocumentVersion) noexcept
{
    const auto& Entities = Document.Entities;
    if (Entities.Size() > MaxSceneEntities) { return {EResult::LimitExceeded, &DocumentKeys[1]}; }

    HashMap<FSceneEntityId, std::size_t> Indices(Allocator);
    Array<std::size_t> Parents(Allocator);
    Array<uint8> Colors(Allocator);
    if (!Indices.TryReserve(Entities.Size()) || !Parents.TryResize(Entities.Size(), Entities.Size()) ||
        !Colors.TryResize(Entities.Size(), uint8{}))
    {
        return {EResult::AllocationFailed, &DocumentKeys[1]};
    }
    bool bFoundActiveDirectionalLight = false;
    for (std::size_t Index = 0; Index < Entities.Size(); ++Index)
    {
        const FSceneEntityRecord& Entity = Entities[Index];
        if (!Entity.Id.IsValid()) { return {EResult::InvalidValue, &EntityKeys[0], Index}; }
        const auto Insert = Indices.TryInsert(Entity.Id, Index);
        if (Insert == EHashInsertResult::AlreadyExists)
        {
            return {EResult::DuplicateEntityId, &EntityKeys[0], Index};
        }
        if (Insert != EHashInsertResult::Inserted)
        {
            return {EResult::AllocationFailed, &EntityKeys[0], Index};
        }
        if (Entity.bHasTransform)
        {
            if (!IsFiniteVector(Entity.Transform.Translation)) { return {EResult::InvalidValue, &TransformKeys[0], Index}; }
            if (!IsValidRotation(Entity.Transform.Rotation)) { return {EResult::InvalidValue, &TransformKeys[1], Index}; }
            if (!IsFiniteVector(Entity.Transform.Scale)) { return {EResult::InvalidValue, &TransformKeys[2], Index}; }
        }
        if (Entity.bHasMesh)
        {
            if (!Entity.Mesh.Mesh.IsValid()) { return {EResult::InvalidValue, &MeshKeys[0], Index}; }
            if (!Entity.Mesh.Material.IsValid()) { return {EResult::InvalidValue, &MeshKeys[1], Index}; }
            if (Entity.Mesh.DebugName.Size() > MaxSceneNameBytes)
            {
                return {EResult::LimitExceeded, &MeshKeys[2], Index};
            }
            if (!IsValidName(Entity.Mesh.DebugName.View())) { return {EResult::InvalidValue, &MeshKeys[2], Index}; }
        }
        if (Entity.bHasCamera)
        {
            const auto& Camera = Entity.Camera;
            if (Camera.DebugName.Size() > MaxSceneNameBytes)
            {
                return {EResult::LimitExceeded, &CameraKeys[0], Index};
            }
            if (!IsValidName(Camera.DebugName.View())) { return {EResult::InvalidValue, &CameraKeys[0], Index}; }
            constexpr float Pi = 3.14159265f;
            if (!std::isfinite(Camera.VerticalFieldOfViewRadians) ||
                Camera.VerticalFieldOfViewRadians <= 0.0f || Camera.VerticalFieldOfViewRadians >= Pi)
            {
                return {EResult::InvalidValue, &CameraKeys[1], Index};
            }
            if (!std::isfinite(Camera.NearPlane) || Camera.NearPlane <= 0.0f)
            {
                return {EResult::InvalidValue, &CameraKeys[2], Index};
            }
            if (!std::isfinite(Camera.FarPlane) || Camera.FarPlane <= Camera.NearPlane)
            {
                return {EResult::InvalidValue, &CameraKeys[3], Index};
            }
        }
        if (Entity.bHasDirectionalLight)
        {
            if (Version == LegacySceneDocumentVersion) { return {EResult::UnknownField, &EntityKeys[5], Index}; }
            const FSceneDirectionalLightRecord& Light = Entity.DirectionalLight;
            switch (ValidateDirectionalLightParameters(Light.Direction, Light.Color, Light.Intensity))
            {
            case EDirectionalLightParameterError::Direction:
                return {EResult::InvalidValue, &DirectionalLightKeys[0], Index};
            case EDirectionalLightParameterError::Color:
                return {EResult::InvalidValue, &DirectionalLightKeys[1], Index};
            case EDirectionalLightParameterError::Intensity:
                return {EResult::InvalidValue, &DirectionalLightKeys[2], Index};
            case EDirectionalLightParameterError::None:
                break;
            }
            if (Light.bActive)
            {
                if (bFoundActiveDirectionalLight)
                {
                    return {EResult::MultipleActiveDirectionalLights, &DirectionalLightKeys[3], Index};
                }
                bFoundActiveDirectionalLight = true;
            }
        }
    }

    for (std::size_t Index = 0; Index < Entities.Size(); ++Index)
    {
        const FSceneEntityRecord& Entity = Entities[Index];
        if (Entity.Parent.IsValid())
        {
            const auto* Parent = Indices.Find(Entity.Parent);
            if (Parent == nullptr || !Entity.bHasTransform || !Entities[*Parent].bHasTransform)
            {
                return {EResult::InvalidParent, &EntityKeys[1], Index};
            }
            Parents[Index] = *Parent;
        }
    }

    // Each entity has at most one parent. Walk each unvisited chain once and
    // unwind its visited prefix iteratively to avoid scene-sized recursion.
    constexpr uint8 Visiting = 1;
    constexpr uint8 Done = 2;
    for (std::size_t Index = 0; Index < Entities.Size(); ++Index)
    {
        std::size_t Current = Index;
        while (Current != Entities.Size() && Colors[Current] == 0)
        {
            Colors[Current] = Visiting;
            Current = Parents[Current];
        }
        if (Current != Entities.Size() && Colors[Current] == Visiting)
        {
            return {EResult::ParentCycle, &EntityKeys[1], Current};
        }
        Current = Index;
        while (Current != Entities.Size() && Colors[Current] == Visiting)
        {
            Colors[Current] = Done;
            Current = Parents[Current];
        }
    }
    return {};
}

EResult ReportValidation(FSceneDocumentDiagnostic* Diagnostic, const FSceneDocument& Document,
    const FValidationResult& Validation) noexcept
{
    const auto Entity = Validation.RecordIndex < Document.Entities.Size()
        ? Document.Entities[Validation.RecordIndex].Id : FSceneEntityId{};
    return Report(Diagnostic, Validation.Result, Validation.Field != nullptr ? *Validation.Field : StringView{}, Entity);
}

class FSceneWriter
{
public:
    explicit FSceneWriter(String& InOutput) noexcept : Output(InOutput) {}
    EResult Write(Span<const FSceneEntityRecord* const> Entities, uint32 Version) noexcept;

private:
    bool Append(StringView Text) noexcept;
    bool WriteString(StringView Text) noexcept;
    bool WriteId(const FUuid& Uuid) noexcept;
    bool WriteFloat(float Value) noexcept;
    template <typename Integer> bool WriteInteger(Integer Value) noexcept;
    bool WriteVector(Span<const float> Values) noexcept;
    bool WriteTransform(const FTransform& Value) noexcept;
    bool WriteMesh(const FSceneMeshRecord& Value) noexcept;
    bool WriteCamera(const FSceneCameraRecord& Value) noexcept;
    bool WriteDirectionalLight(const FSceneDirectionalLightRecord& Value) noexcept;
    bool WriteEntity(const FSceneEntityRecord& Entity) noexcept;

    String& Output;
    FSceneNumericLocale Locale;
    EResult Error = EResult::Success;
};

bool FSceneWriter::Append(const StringView Text) noexcept
{
    if (Text.Size() > MaxSceneDocumentBytes - Output.Size())
    {
        Error = EResult::LimitExceeded;
        return false;
    }
    if (!Output.TryAppend(Text))
    {
        Error = EResult::AllocationFailed;
        return false;
    }
    return true;
}

bool FSceneWriter::WriteString(const StringView Text) noexcept
{
    constexpr char Hex[] = "0123456789abcdef";
    if (!Append("\"")) { return false; }
    std::size_t Chunk = 0;
    for (std::size_t Index = 0; Index < Text.Size(); ++Index)
    {
        const auto Byte = static_cast<uint8>(Text[Index]);
        if (Byte >= 0x20 && Byte != '"' && Byte != '\\') { continue; }
        if (!Append(Text.Substr(Chunk, Index - Chunk))) { return false; }
        if (Byte == '"' || Byte == '\\')
        {
            const char Escape[] = {'\\', static_cast<char>(Byte)};
            if (!Append(StringView(Escape, sizeof(Escape)))) { return false; }
        }
        else
        {
            const char Escape[] = {'\\', 'u', '0', '0', Hex[Byte >> 4], Hex[Byte & 0xf]};
            if (!Append(StringView(Escape, sizeof(Escape)))) { return false; }
        }
        Chunk = Index + 1;
    }
    return Append(Text.Substr(Chunk, Text.Size() - Chunk)) && Append("\"");
}

bool FSceneWriter::WriteId(const FUuid& Uuid) noexcept
{
    constexpr char Hex[] = "0123456789abcdef";
    char Text[FUuid::CanonicalStringLength + 2];
    std::size_t Offset = 0;
    Text[Offset++] = '"';
    const auto Bytes = Uuid.GetBytes();
    for (std::size_t Index = 0; Index < Bytes.Size(); ++Index)
    {
        if (Index == 4 || Index == 6 || Index == 8 || Index == 10) { Text[Offset++] = '-'; }
        Text[Offset++] = Hex[Bytes[Index] >> 4];
        Text[Offset++] = Hex[Bytes[Index] & 0xf];
    }
    Text[Offset++] = '"';
    return Append(StringView(Text, Offset));
}

bool FSceneWriter::WriteFloat(const float Value) noexcept
{
    char Buffer[MaxNumberBytes];
    const int Length = Locale.Format(Buffer, sizeof(Buffer), Value);
    if (Length <= 0 || static_cast<std::size_t>(Length) >= sizeof(Buffer))
    {
        Error = EResult::InvalidValue;
        return false;
    }
    return Append(StringView(Buffer, static_cast<std::size_t>(Length)));
}

template <typename Integer>
bool FSceneWriter::WriteInteger(const Integer Value) noexcept
{
    char Buffer[MaxNumberBytes];
    const auto Converted = std::to_chars(Buffer, Buffer + sizeof(Buffer), Value);
    if (Converted.ec != std::errc{})
    {
        Error = EResult::InvalidValue;
        return false;
    }
    return Append(StringView(Buffer, static_cast<std::size_t>(Converted.ptr - Buffer)));
}

bool FSceneWriter::WriteVector(const Span<const float> Values) noexcept
{
    if (!Append("[")) { return false; }
    for (std::size_t Index = 0; Index < Values.Size(); ++Index)
    {
        if ((Index != 0 && !Append(", ")) || !WriteFloat(Values[Index])) { return false; }
    }
    return Append("]");
}

bool FSceneWriter::WriteTransform(const FTransform& Value) noexcept
{
    const float Rotation[] = {Value.Rotation.x, Value.Rotation.y, Value.Rotation.z, Value.Rotation.w};
    return Append(",\n      \"transform\": {\n        \"translation\": ") && WriteVector(Value.Translation.Values) &&
        Append(",\n        \"rotation\": ") && WriteVector(Rotation) &&
        Append(",\n        \"scale\": ") && WriteVector(Value.Scale.Values) && Append("\n      }");
}

bool FSceneWriter::WriteMesh(const FSceneMeshRecord& Value) noexcept
{
    return Append(",\n      \"mesh\": {\n        \"mesh\": ") && WriteId(Value.Mesh.GetUuid()) &&
        Append(",\n        \"material\": ") && WriteId(Value.Material.GetUuid()) &&
        Append(",\n        \"debugName\": ") && WriteString(Value.DebugName.View()) &&
        Append(",\n        \"sortKey\": \"") && WriteInteger(Value.SortKey) && Append("\"\n      }");
}

bool FSceneWriter::WriteCamera(const FSceneCameraRecord& Value) noexcept
{
    return Append(",\n      \"camera\": {\n        \"debugName\": ") && WriteString(Value.DebugName.View()) &&
        Append(",\n        \"verticalFieldOfViewRadians\": ") && WriteFloat(Value.VerticalFieldOfViewRadians) &&
        Append(",\n        \"nearPlane\": ") && WriteFloat(Value.NearPlane) &&
        Append(",\n        \"farPlane\": ") && WriteFloat(Value.FarPlane) &&
        Append(",\n        \"priority\": ") && WriteInteger(Value.Priority) &&
        Append(",\n        \"active\": ") && Append(Value.bActive ? "true" : "false") && Append("\n      }");
}

bool FSceneWriter::WriteDirectionalLight(const FSceneDirectionalLightRecord& Value) noexcept
{
    return Append(",\n      \"directionalLight\": {\n        \"direction\": ") && WriteVector(Value.Direction.Values) &&
        Append(",\n        \"color\": ") && WriteVector(Value.Color.Values) &&
        Append(",\n        \"intensity\": ") && WriteFloat(Value.Intensity) &&
        Append(",\n        \"active\": ") && Append(Value.bActive ? "true" : "false") && Append("\n      }");
}

bool FSceneWriter::WriteEntity(const FSceneEntityRecord& Entity) noexcept
{
    return Append("    {\n      \"id\": ") && WriteId(Entity.Id.GetUuid()) &&
        (!Entity.Parent.IsValid() || (Append(",\n      \"parent\": ") && WriteId(Entity.Parent.GetUuid()))) &&
        (!Entity.bHasTransform || WriteTransform(Entity.Transform)) &&
        (!Entity.bHasMesh || WriteMesh(Entity.Mesh)) &&
        (!Entity.bHasCamera || WriteCamera(Entity.Camera)) &&
        (!Entity.bHasDirectionalLight || WriteDirectionalLight(Entity.DirectionalLight)) && Append("\n    }");
}

EResult FSceneWriter::Write(const Span<const FSceneEntityRecord* const> Entities, const uint32 Version) noexcept
{
    if (!Locale.IsValid()) { return EResult::AllocationFailed; }
    if (!Append("{\n  \"version\": ") || !WriteInteger(Version) || !Append(",\n  \"entities\": ["))
    {
        return Error;
    }
    for (std::size_t Index = 0; Index < Entities.Size(); ++Index)
    {
        if (!Append(Index == 0 ? "\n" : ",\n") || !WriteEntity(*Entities[Index])) { return Error; }
    }
    Append(Entities.IsEmpty() ? "]\n}\n" : "\n  ]\n}\n");
    return Error;
}

} // namespace

ESceneDocumentResult DecodeSceneDocument(const StringView Source, FSceneDocument& Out,
    FSceneDocumentDiagnostic* Diagnostic) noexcept
{
    if (Source.Size() > MaxSceneDocumentBytes) { return Report(Diagnostic, EResult::LimitExceeded); }
    IAllocator& Allocator = Out.Entities.GetAllocator();
    FSceneDocument Candidate(Allocator);
    Array<FEntitySourceLocation> Locations(Allocator);
    FSceneReader Reader(Source, Allocator, Diagnostic);
    uint32 Version = 0;
    const EResult ParseResult = Reader.Read(Candidate, Locations, Version);
    if (ParseResult != EResult::Success) { return ParseResult; }

    const FValidationResult Validation = Validate(Candidate, Allocator, Version);
    if (Validation.Result != EResult::Success)
    {
        ReportValidation(Diagnostic, Candidate, Validation);
        if (Diagnostic != nullptr && Validation.RecordIndex < Locations.Size())
        {
            SetLocation(*Diagnostic, Source, Locations[Validation.RecordIndex].ForField(Validation.Field));
        }
        return Validation.Result;
    }
    auto& Entities = Candidate.Entities;
    if (Entities.Size() > 1)
    {
        std::sort(Entities.Data(), Entities.Data() + Entities.Size(),
            [](const FSceneEntityRecord& Left, const FSceneEntityRecord& Right) { return Left.Id < Right.Id; });
    }
    Out = std::move(Candidate);
    return Report(Diagnostic, EResult::Success);
}

ESceneDocumentResult EncodeSceneDocument(const FSceneDocument& Document, String& Out,
    FSceneDocumentDiagnostic* Diagnostic) noexcept
{
    IAllocator& Allocator = Out.GetAllocator();
    const FValidationResult Validation = Validate(Document, Allocator);
    if (Validation.Result != EResult::Success)
    {
        return ReportValidation(Diagnostic, Document, Validation);
    }
    Array<const FSceneEntityRecord*> Ordered(Allocator);
    if (!Ordered.TryReserve(Document.Entities.Size())) { return Report(Diagnostic, EResult::AllocationFailed); }
    uint32 Version = LegacySceneDocumentVersion;
    for (const FSceneEntityRecord& Entity : Document.Entities)
    {
        if (!Ordered.TryPushBack(&Entity)) { return Report(Diagnostic, EResult::AllocationFailed); }
        if (Entity.bHasDirectionalLight) { Version = SceneDocumentVersion; }
    }
    if (Ordered.Size() > 1)
    {
        std::sort(Ordered.Data(), Ordered.Data() + Ordered.Size(),
            [](const FSceneEntityRecord* Left, const FSceneEntityRecord* Right) { return Left->Id < Right->Id; });
    }
    String Candidate(Allocator);
    FSceneWriter Writer(Candidate);
    const EResult Result = Writer.Write(Ordered.AsSpan(), Version);
    if (Result == EResult::Success) { Out = std::move(Candidate); }
    return Report(Diagnostic, Result);
}

ESceneDocumentResult ValidateSceneDocument(const FSceneDocument& Document,
    FSceneDocumentDiagnostic* Diagnostic) noexcept
{
    return ReportValidation(Diagnostic, Document, Validate(Document, Document.Entities.GetAllocator()));
}

const char* DescribeSceneDocumentResult(const ESceneDocumentResult Result) noexcept
{
    switch (Result)
    {
    case EResult::Success: return "success";
    case EResult::InvalidJson: return "invalid JSON";
    case EResult::UnsupportedVersion: return "unsupported scene version";
    case EResult::DuplicateField: return "duplicate field";
    case EResult::UnknownField: return "unknown field";
    case EResult::MissingField: return "missing required field";
    case EResult::InvalidValue: return "invalid field value";
    case EResult::LimitExceeded: return "scene limit exceeded";
    case EResult::AllocationFailed: return "out of memory";
    case EResult::DuplicateEntityId: return "duplicate entity ID";
    case EResult::InvalidParent: return "parent is missing or parent and child require transforms";
    case EResult::ParentCycle: return "parent cycle";
    case EResult::MultipleActiveDirectionalLights: return "scene has more than one active directional light";
    }
    return "unexpected scene error";
}

} // namespace LE
