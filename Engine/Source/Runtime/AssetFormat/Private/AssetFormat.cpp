#include "AssetFormat/AssetFormat.h"

#include "Asset/SoftAssetPath.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace LE
{
namespace
{

constexpr uint8 Magic[] = {'L', 'E', 'A', 'S'};
constexpr std::size_t VersionOffset = sizeof(Magic);
constexpr std::size_t TypeOffset = VersionOffset + sizeof(uint32);
constexpr std::size_t IdOffset = TypeOffset + FUuid::ByteCount;
constexpr std::size_t PathLengthOffset = IdOffset + FUuid::ByteCount;
constexpr std::size_t PayloadLengthOffset = PathLengthOffset + sizeof(uint32);
constexpr std::size_t PathOffset = PayloadLengthOffset + sizeof(uint32);
constexpr std::size_t PairSize = 2 * sizeof(uint32);
constexpr std::size_t VectorSize = 3 * sizeof(uint32);

static_assert(PathOffset + MaxAssetPathBytes == MaxAssetFileHeaderBytes);
static_assert(sizeof(float) == sizeof(uint32) && std::numeric_limits<float>::is_iec559);
static_assert(MaxAssetFileBytes <= (std::numeric_limits<uint32>::max)());

uint32 ReadUInt32(const uint8* Bytes) noexcept
{
    return static_cast<uint32>(Bytes[0])
        | (static_cast<uint32>(Bytes[1]) << 8)
        | (static_cast<uint32>(Bytes[2]) << 16)
        | (static_cast<uint32>(Bytes[3]) << 24);
}

void WriteUInt32(uint8* Bytes, const uint32 Value) noexcept
{
    for (uint32 Index = 0; Index < sizeof(uint32); ++Index)
    {
        Bytes[Index] = static_cast<uint8>(Value >> (8 * Index));
    }
}

template <typename Vector>
Vector ReadVector(const uint8* Bytes) noexcept
{
    Vector Result;
    for (std::size_t Index = 0; Index < Vector::Dimension; ++Index)
    {
        const uint32 Bits = ReadUInt32(Bytes + Index * sizeof(uint32));
        std::memcpy(&Result.Values[Index], &Bits, sizeof(Bits));
    }
    return Result;
}

template <typename Vector>
void WriteVector(uint8* Bytes, const Vector& Value) noexcept
{
    for (std::size_t Index = 0; Index < Vector::Dimension; ++Index)
    {
        uint32 Bits = 0;
        std::memcpy(&Bits, &Value.Values[Index], sizeof(Bits));
        WriteUInt32(Bytes + Index * sizeof(uint32), Bits);
    }
}

template <typename IdType>
bool ReadId(const uint8* Bytes, IdType& Out) noexcept
{
    constexpr char Hex[] = "0123456789abcdef";
    char Text[FUuid::CanonicalStringLength];
    std::size_t Offset = 0;
    for (std::size_t Index = 0; Index < FUuid::ByteCount; ++Index)
    {
        if (Index == 4 || Index == 6 || Index == 8 || Index == 10)
        {
            Text[Offset++] = '-';
        }
        Text[Offset++] = Hex[Bytes[Index] >> 4];
        Text[Offset++] = Hex[Bytes[Index] & 15u];
    }
    return IdType::TryParse(StringView(Text, sizeof(Text)), Out);
}

bool HasReservedStem(const StringView Segment) noexcept
{
    char Upper[4]{};
    std::size_t Index = 0;
    while (Index < Segment.Size() && Segment[Index] != '.')
    {
        if (Index == sizeof(Upper))
        {
            return false;
        }
        const char Character = Segment[Index];
        Upper[Index] = Character >= 'a' && Character <= 'z'
            ? static_cast<char>(Character - 'a' + 'A') : Character;
        ++Index;
    }
    const StringView Stem(Upper, Index);
    if (Stem == StringView("CON") || Stem == StringView("PRN")
        || Stem == StringView("AUX") || Stem == StringView("NUL"))
    {
        return true;
    }
    if (Index == sizeof(Upper) && Upper[3] >= '1' && Upper[3] <= '9')
    {
        const StringView Prefix(Upper, 3);
        return Prefix == StringView("COM") || Prefix == StringView("LPT");
    }
    return false;
}

EAssetFormatResult ValidatePath(const StringView Path, IAllocator& Allocator) noexcept
{
    if (Path.Size() > MaxAssetPathBytes)
    {
        return EAssetFormatResult::LimitExceeded;
    }
    for (std::size_t Index = 0; Index < Path.Size(); ++Index)
    {
        if (static_cast<unsigned char>(Path[Index]) > 0x7fu)
        {
            return EAssetFormatResult::InvalidPath;
        }
    }
    FSoftAssetPath Canonical(Allocator);
    const ESoftAssetPathResult Result = Canonical.TryAssign(Path);
    if (Result == ESoftAssetPathResult::AllocationFailed)
    {
        return EAssetFormatResult::AllocationFailed;
    }
    if (Result != ESoftAssetPathResult::Success || Canonical.View() != Path)
    {
        return EAssetFormatResult::InvalidPath;
    }
    std::size_t Start = 0;
    for (std::size_t Index = 0; Index <= Path.Size(); ++Index)
    {
        if (Index == Path.Size() || Path[Index] == '/')
        {
            const StringView Segment = Path.Substr(Start, Index - Start);
            if (Segment[Segment.Size() - 1] == '.' || HasReservedStem(Segment))
            {
                return EAssetFormatResult::InvalidPath;
            }
            Start = Index + 1;
        }
    }
    return EAssetFormatResult::Success;
}

EAssetFormatResult BeginEncoding(
    const FAssetId Id, const FAssetTypeId TypeId, const StringView Path,
    const uint32 Version, const std::size_t PayloadSize, Array<uint8>& Candidate) noexcept
{
    if (!Id.IsValid())
    {
        return EAssetFormatResult::InvalidIdentity;
    }
    const EAssetFormatResult PathResult = ValidatePath(Path, Candidate.GetAllocator());
    if (PathResult != EAssetFormatResult::Success)
    {
        return PathResult;
    }
    const std::size_t HeaderSize = PathOffset + Path.Size();
    if (PayloadSize > MaxAssetFileBytes - HeaderSize)
    {
        return EAssetFormatResult::LimitExceeded;
    }
    const std::size_t TotalSize = HeaderSize + PayloadSize;
    if (!Candidate.TryReserve(TotalSize) || !Candidate.TryResize(TotalSize))
    {
        return EAssetFormatResult::AllocationFailed;
    }

    uint8* const Bytes = Candidate.Data();
    std::memcpy(Bytes, Magic, sizeof(Magic));
    WriteUInt32(Bytes + VersionOffset, Version);
    std::memcpy(Bytes + TypeOffset, TypeId.GetUuid().GetBytes().Data(), FUuid::ByteCount);
    std::memcpy(Bytes + IdOffset, Id.GetUuid().GetBytes().Data(), FUuid::ByteCount);
    WriteUInt32(Bytes + PathLengthOffset, static_cast<uint32>(Path.Size()));
    WriteUInt32(Bytes + PayloadLengthOffset, static_cast<uint32>(PayloadSize));
    std::memcpy(Bytes + PathOffset, Path.Data(), Path.Size());
    return EAssetFormatResult::Success;
}

EAssetFormatResult ReadTypedPayload(
    const Span<const uint8> Bytes, const FAssetTypeId ExpectedType,
    Span<const uint8>& Payload, IAllocator& Allocator, uint32* Version = nullptr) noexcept
{
    FAssetFileHeaderView Header;
    const EAssetFormatResult Result = ReadAssetFileHeader(Bytes, Header, Allocator);
    if (Result != EAssetFormatResult::Success)
    {
        return Result;
    }
    if (Header.TypeId != ExpectedType)
    {
        return EAssetFormatResult::TypeMismatch;
    }
    Payload = Bytes.Subspan(Header.PayloadOffset, Header.PayloadSize);
    if (Version != nullptr)
    {
        *Version = Header.Version;
    }
    return EAssetFormatResult::Success;
}

template <typename Vector>
bool IsFinite(const Vector& Value) noexcept
{
    for (float Component : Value.Values)
    {
        if (!std::isfinite(Component))
        {
            return false;
        }
    }
    return true;
}

bool IsValidColor(const Math::Vector3f& Color) noexcept
{
    return IsFinite(Color) && Color.Values[0] >= 0.0f && Color.Values[1] >= 0.0f
        && Color.Values[2] >= 0.0f;
}

EAssetFormatResult GetMeshPayloadSize(
    const std::size_t VertexCount, const std::size_t IndexCount,
    const bool bHasUVs, std::size_t& PayloadSize) noexcept
{
    if (VertexCount > MaxAssetMeshVertices || IndexCount > MaxAssetMeshIndices)
    {
        return EAssetFormatResult::LimitExceeded;
    }
    if (VertexCount == 0 || IndexCount == 0 || IndexCount % 3 != 0)
    {
        return EAssetFormatResult::InvalidPayload;
    }
    // With the checked caps this fits in 32 bits, including the count pair.
    const std::size_t VertexSize = VectorSize + (bHasUVs ? PairSize : 0);
    PayloadSize = PairSize + VertexCount * VertexSize + IndexCount * sizeof(uint32);
    return EAssetFormatResult::Success;
}

EAssetFormatResult GetTexturePixelSize(
    const uint32 Width, const uint32 Height, std::size_t& PixelSize) noexcept
{
    if (Width > MaxAssetTextureDimension || Height > MaxAssetTextureDimension)
    {
        return EAssetFormatResult::LimitExceeded;
    }
    if (Width == 0 || Height == 0)
    {
        return EAssetFormatResult::InvalidPayload;
    }
    // The dimension limits keep this product and the payload size below 2^32.
    PixelSize = static_cast<std::size_t>(Width) * Height * 4;
    return EAssetFormatResult::Success;
}

} // namespace

EAssetFormatResult ReadAssetFileHeader(
    const Span<const uint8> Prefix, const std::size_t FileSize,
    FAssetFileHeaderView& Out, IAllocator& Allocator) noexcept
{
    if (FileSize > MaxAssetFileBytes)
    {
        return EAssetFormatResult::LimitExceeded;
    }
    if (Prefix.Size() < PathOffset || Prefix.Size() > FileSize
        || std::memcmp(Prefix.Data(), Magic, sizeof(Magic)) != 0)
    {
        return EAssetFormatResult::InvalidHeader;
    }
    const uint8* const Bytes = Prefix.Data();
    const uint32 Version = ReadUInt32(Bytes + VersionOffset);
    if (Version != AssetFileVersion && Version != TexturedAssetFileVersion)
    {
        return EAssetFormatResult::UnsupportedVersion;
    }
    const std::size_t PathLength = ReadUInt32(Bytes + PathLengthOffset);
    if (PathLength > MaxAssetPathBytes)
    {
        return EAssetFormatResult::LimitExceeded;
    }
    FAssetFileHeaderView Candidate;
    Candidate.Version = Version;
    Candidate.PayloadOffset = PathOffset + PathLength;
    Candidate.PayloadSize = ReadUInt32(Bytes + PayloadLengthOffset);
    if (Candidate.PayloadOffset > Prefix.Size()
        || Candidate.PayloadSize != FileSize - Candidate.PayloadOffset)
    {
        return EAssetFormatResult::InvalidHeader;
    }
    if (!ReadId(Bytes + TypeOffset, Candidate.TypeId) || !ReadId(Bytes + IdOffset, Candidate.Id))
    {
        return EAssetFormatResult::InvalidIdentity;
    }
    if (Version == TexturedAssetFileVersion
        && Candidate.TypeId != GetMeshAssetTypeId() && Candidate.TypeId != GetMaterialAssetTypeId())
    {
        return EAssetFormatResult::UnsupportedVersion;
    }
    Candidate.Path = StringView(reinterpret_cast<const char*>(Bytes + PathOffset), PathLength);
    const EAssetFormatResult Result = ValidatePath(Candidate.Path, Allocator);
    if (Result == EAssetFormatResult::Success)
    {
        Out = Candidate;
    }
    return Result;
}

EAssetFormatResult EncodeAssetFile(
    const FAssetId Id, const StringView Path, const FMeshAssetData& Mesh,
    Array<uint8>& Out) noexcept
{
    const bool bHasUVs = !Mesh.UVs.IsEmpty();
    if (bHasUVs && Mesh.UVs.Size() != Mesh.Positions.Size())
    {
        return EAssetFormatResult::InvalidPayload;
    }
    std::size_t PayloadSize = 0;
    const EAssetFormatResult ShapeResult = GetMeshPayloadSize(
        Mesh.Positions.Size(), Mesh.Indices.Size(), bHasUVs, PayloadSize);
    if (ShapeResult != EAssetFormatResult::Success)
    {
        return ShapeResult;
    }
    for (const Math::Vector3f& Position : Mesh.Positions)
    {
        if (!IsFinite(Position))
        {
            return EAssetFormatResult::InvalidPayload;
        }
    }
    for (const uint32 Index : Mesh.Indices)
    {
        if (Index >= Mesh.Positions.Size())
        {
            return EAssetFormatResult::InvalidPayload;
        }
    }
    for (const Math::Vector2f& UV : Mesh.UVs)
    {
        if (!IsFinite(UV))
        {
            return EAssetFormatResult::InvalidPayload;
        }
    }
    Array<uint8> Candidate(Out.GetAllocator());
    const EAssetFormatResult Result = BeginEncoding(Id, GetMeshAssetTypeId(), Path,
        bHasUVs ? TexturedAssetFileVersion : AssetFileVersion, PayloadSize, Candidate);
    if (Result != EAssetFormatResult::Success)
    {
        return Result;
    }
    uint8* const Payload = Candidate.Data() + PathOffset + Path.Size();
    WriteUInt32(Payload, static_cast<uint32>(Mesh.Positions.Size()));
    WriteUInt32(Payload + sizeof(uint32), static_cast<uint32>(Mesh.Indices.Size()));
    uint8* PositionBytes = Payload + PairSize;
    for (const Math::Vector3f& Position : Mesh.Positions)
    {
        WriteVector(PositionBytes, Position);
        PositionBytes += VectorSize;
    }
    uint8* UVBytes = PositionBytes;
    for (const Math::Vector2f& UV : Mesh.UVs)
    {
        WriteVector(UVBytes, UV);
        UVBytes += PairSize;
    }
    uint8* IndexBytes = UVBytes;
    for (const uint32 Index : Mesh.Indices)
    {
        WriteUInt32(IndexBytes, Index);
        IndexBytes += sizeof(uint32);
    }
    Out = std::move(Candidate);
    return EAssetFormatResult::Success;
}

EAssetFormatResult EncodeAssetFile(
    const FAssetId Id, const StringView Path, const FMaterialAssetData& Material,
    Array<uint8>& Out) noexcept
{
    if (!IsValidColor(Material.BaseColor))
    {
        return EAssetFormatResult::InvalidPayload;
    }
    const bool bHasTexture = Material.BaseColorTexture.IsValid();
    Array<uint8> Candidate(Out.GetAllocator());
    const EAssetFormatResult Result = BeginEncoding(
        Id, GetMaterialAssetTypeId(), Path, bHasTexture ? TexturedAssetFileVersion : AssetFileVersion,
        VectorSize + (bHasTexture ? FUuid::ByteCount : 0), Candidate);
    if (Result != EAssetFormatResult::Success)
    {
        return Result;
    }
    uint8* const Payload = Candidate.Data() + PathOffset + Path.Size();
    WriteVector(Payload, Material.BaseColor);
    if (bHasTexture)
    {
        std::memcpy(Payload + VectorSize, Material.BaseColorTexture.GetUuid().GetBytes().Data(), FUuid::ByteCount);
    }
    Out = std::move(Candidate);
    return EAssetFormatResult::Success;
}

EAssetFormatResult EncodeAssetFile(
    const FAssetId Id, const StringView Path, const FCpuTextureAsset& Texture,
    Array<uint8>& Out) noexcept
{
    std::size_t PixelSize = 0;
    const EAssetFormatResult ShapeResult = GetTexturePixelSize(Texture.Width, Texture.Height, PixelSize);
    if (ShapeResult != EAssetFormatResult::Success)
    {
        return ShapeResult;
    }
    if (Texture.Pixels.Size() != PixelSize)
    {
        return EAssetFormatResult::InvalidPayload;
    }
    Array<uint8> Candidate(Out.GetAllocator());
    const EAssetFormatResult Result = BeginEncoding(
        Id, GetCpuTextureAssetTypeId(), Path, AssetFileVersion, PairSize + PixelSize, Candidate);
    if (Result != EAssetFormatResult::Success)
    {
        return Result;
    }
    uint8* const Payload = Candidate.Data() + PathOffset + Path.Size();
    WriteUInt32(Payload, Texture.Width);
    WriteUInt32(Payload + sizeof(uint32), Texture.Height);
    std::memcpy(Payload + PairSize, Texture.Pixels.Data(), PixelSize);
    Out = std::move(Candidate);
    return EAssetFormatResult::Success;
}

EAssetFormatResult DecodeAssetFile(const Span<const uint8> Bytes, FMeshAssetData& Out) noexcept
{
    Span<const uint8> Payload;
    uint32 Version = 0;
    const EAssetFormatResult Result = ReadTypedPayload(
        Bytes, GetMeshAssetTypeId(), Payload, Out.Positions.GetAllocator(), &Version);
    if (Result != EAssetFormatResult::Success)
    {
        return Result;
    }
    if (Payload.Size() < PairSize)
    {
        return EAssetFormatResult::InvalidPayload;
    }
    const uint32 VertexCount = ReadUInt32(Payload.Data());
    const uint32 IndexCount = ReadUInt32(Payload.Data() + sizeof(uint32));
    const bool bHasUVs = Version == TexturedAssetFileVersion;
    std::size_t PayloadSize = 0;
    const EAssetFormatResult ShapeResult = GetMeshPayloadSize(VertexCount, IndexCount, bHasUVs, PayloadSize);
    if (ShapeResult != EAssetFormatResult::Success)
    {
        return ShapeResult;
    }
    if (Payload.Size() != PayloadSize)
    {
        return EAssetFormatResult::InvalidPayload;
    }

    const uint8* const PositionBytes = Payload.Data() + PairSize;
    const uint8* const UVBytes = PositionBytes + VertexCount * VectorSize;
    const uint8* const IndexBytes = UVBytes + (bHasUVs ? VertexCount * PairSize : 0);
    for (uint32 Index = 0; Index < IndexCount; ++Index)
    {
        if (ReadUInt32(IndexBytes + Index * sizeof(uint32)) >= VertexCount)
        {
            return EAssetFormatResult::InvalidPayload;
        }
    }
    Math::Vector3f BoundsMin = ReadVector<Math::Vector3f>(PositionBytes);
    Math::Vector3f BoundsMax = BoundsMin;
    for (uint32 Vertex = 0; Vertex < VertexCount; ++Vertex)
    {
        const Math::Vector3f Position = ReadVector<Math::Vector3f>(PositionBytes + Vertex * VectorSize);
        if (!IsFinite(Position) || (bHasUVs && !IsFinite(ReadVector<Math::Vector2f>(UVBytes + Vertex * PairSize))))
        {
            return EAssetFormatResult::InvalidPayload;
        }
        for (std::size_t Axis = 0; Axis < Math::Vector3f::Dimension; ++Axis)
        {
            const float Coordinate = Position.Values[Axis];
            if (Coordinate < BoundsMin.Values[Axis])
            {
                BoundsMin.Values[Axis] = Coordinate;
            }
            if (Coordinate > BoundsMax.Values[Axis])
            {
                BoundsMax.Values[Axis] = Coordinate;
            }
        }
    }

    FMeshAssetData Candidate(Out.Positions.GetAllocator());
    Candidate.Indices = Array<uint32>(Out.Indices.GetAllocator());
    Candidate.UVs = Array<Math::Vector2f>(Out.UVs.GetAllocator());
    if (!Candidate.Positions.TryReserve(VertexCount) || !Candidate.Indices.TryReserve(IndexCount)
        || !Candidate.Positions.TryResize(VertexCount) || !Candidate.Indices.TryResize(IndexCount)
        || (bHasUVs && (!Candidate.UVs.TryReserve(VertexCount) || !Candidate.UVs.TryResize(VertexCount))))
    {
        return EAssetFormatResult::AllocationFailed;
    }
    Candidate.BoundsMin = BoundsMin;
    Candidate.BoundsMax = BoundsMax;
    for (uint32 Vertex = 0; Vertex < VertexCount; ++Vertex)
    {
        Candidate.Positions[Vertex] = ReadVector<Math::Vector3f>(PositionBytes + Vertex * VectorSize);
        if (bHasUVs)
        {
            Candidate.UVs[Vertex] = ReadVector<Math::Vector2f>(UVBytes + Vertex * PairSize);
        }
    }
    for (uint32 Index = 0; Index < IndexCount; ++Index)
    {
        Candidate.Indices[Index] = ReadUInt32(IndexBytes + Index * sizeof(uint32));
    }
    Out = std::move(Candidate);
    return EAssetFormatResult::Success;
}

EAssetFormatResult DecodeAssetFile(const Span<const uint8> Bytes, FMaterialAssetData& Out) noexcept
{
    Span<const uint8> Payload;
    uint32 Version = 0;
    const EAssetFormatResult Result = ReadTypedPayload(
        Bytes, GetMaterialAssetTypeId(), Payload, GetDefaultAllocator(), &Version);
    if (Result != EAssetFormatResult::Success)
    {
        return Result;
    }
    const bool bHasTexture = Version == TexturedAssetFileVersion;
    if (Payload.Size() != VectorSize + (bHasTexture ? FUuid::ByteCount : 0))
    {
        return EAssetFormatResult::InvalidPayload;
    }
    FMaterialAssetData Candidate;
    Candidate.BaseColor = ReadVector<Math::Vector3f>(Payload.Data());
    if (!IsValidColor(Candidate.BaseColor)
        || (bHasTexture && !ReadId(Payload.Data() + VectorSize, Candidate.BaseColorTexture)))
    {
        return EAssetFormatResult::InvalidPayload;
    }
    Out = Candidate;
    return EAssetFormatResult::Success;
}

EAssetFormatResult DecodeAssetFile(const Span<const uint8> Bytes, FCpuTextureAsset& Out) noexcept
{
    Span<const uint8> Payload;
    const EAssetFormatResult Result = ReadTypedPayload(
        Bytes, GetCpuTextureAssetTypeId(), Payload, Out.Pixels.GetAllocator());
    if (Result != EAssetFormatResult::Success)
    {
        return Result;
    }
    if (Payload.Size() < PairSize)
    {
        return EAssetFormatResult::InvalidPayload;
    }
    const uint32 Width = ReadUInt32(Payload.Data());
    const uint32 Height = ReadUInt32(Payload.Data() + sizeof(uint32));
    std::size_t PixelSize = 0;
    const EAssetFormatResult ShapeResult = GetTexturePixelSize(Width, Height, PixelSize);
    if (ShapeResult != EAssetFormatResult::Success)
    {
        return ShapeResult;
    }
    if (Payload.Size() - PairSize != PixelSize)
    {
        return EAssetFormatResult::InvalidPayload;
    }
    FCpuTextureAsset Candidate(Out.Pixels.GetAllocator());
    if (!Candidate.Pixels.TryReserve(PixelSize) || !Candidate.Pixels.TryResize(PixelSize))
    {
        return EAssetFormatResult::AllocationFailed;
    }
    Candidate.Width = Width;
    Candidate.Height = Height;
    std::memcpy(Candidate.Pixels.Data(), Payload.Data() + PairSize, PixelSize);
    Out = std::move(Candidate);
    return EAssetFormatResult::Success;
}

} // namespace LE
