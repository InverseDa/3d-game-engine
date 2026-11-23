#include "AssetFormat/AssetFormat.h"

#include <cstdio>
#include <cstring>
#include <limits>

namespace LE
{
namespace
{

bool ExpectFormat(const bool Condition, const char* Description)
{
    if (!Condition)
    {
        std::fprintf(stderr, "FAILED: AssetFormat: %s\n", Description);
    }
    return Condition;
}

class FFormatTestAllocator final : public IAllocator
{
public:
    void* Allocate(std::size_t Size, std::size_t Alignment) noexcept override
    {
        ++Attempts;
        if (Attempts == FailAt)
        {
            return nullptr;
        }
        void* const Address = GetDefaultAllocator().Allocate(Size, Alignment);
        if (Address != nullptr)
        {
            ++Live;
        }
        return Address;
    }

    void Deallocate(void* Address, std::size_t Size, std::size_t Alignment) noexcept override
    {
        if (Address != nullptr)
        {
            --Live;
        }
        GetDefaultAllocator().Deallocate(Address, Size, Alignment);
    }

    std::size_t Attempts = 0;
    std::size_t FailAt = 0;
    std::size_t Live = 0;
};

bool TestMaterial(const FAssetId Id)
{
    const StringView Path = "Game/Materials/Blue.leasset";
    FMaterialAssetData Material;
    Material.BaseColor = Math::Vector3f{0.25f, 0.0f, 2.0f};
    Array<uint8> Bytes;
    Array<uint8> Repeat;
    if (!ExpectFormat(EncodeAssetFile(Id, Path, Material, Bytes) == EAssetFormatResult::Success && EncodeAssetFile(Id, Path, Material, Repeat) == EAssetFormatResult::Success, "material encodes"))
    {
        return false;
    }
    const std::size_t HeaderSize = 48 + Path.Size();
    FAssetFileHeaderView Header;
    bool Result = ExpectFormat(Bytes.Size() == Repeat.Size() && std::memcmp(Bytes.Data(), Repeat.Data(), Bytes.Size()) == 0, "fixed inputs produce the same bytes");
    Result &= ExpectFormat(ReadAssetFileHeader(Bytes.AsSpan().First(HeaderSize), Bytes.Size(), Header) == EAssetFormatResult::Success && Header.Id == Id && Header.TypeId == GetMaterialAssetTypeId() && Header.Path == Path && Header.PayloadOffset == HeaderSize && Header.PayloadSize == 12 && Header.Version == AssetFileVersion, "catalog parses a prefix without reading the payload");
    constexpr uint8 ExpectedPrefix[] = {'L', 'E', 'A', 'S', 1, 0, 0, 0};
    constexpr uint8 ExpectedRgb[] = {0, 0, 0x80, 0x3E, 0, 0, 0, 0, 0, 0, 0, 0x40};
    Result &= ExpectFormat(std::memcmp(Bytes.Data(), ExpectedPrefix, sizeof(ExpectedPrefix)) == 0 && std::memcmp(Bytes.Data() + HeaderSize, ExpectedRgb, sizeof(ExpectedRgb)) == 0, "golden bytes use little endian version and IEEE-754 RGB");
    FMaterialAssetData Decoded;
    Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), Decoded) == EAssetFormatResult::Success && Decoded.BaseColor == Material.BaseColor, "material round trip");
    FMeshAssetData WrongType;
    Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), WrongType) == EAssetFormatResult::TypeMismatch, "typed payload rejects an unrelated asset");
    Result &= ExpectFormat(EncodeAssetFile(Id, "Game//Materials/Blue.leasset", Material, Repeat) == EAssetFormatResult::InvalidPath && Repeat.Size() == Bytes.Size() && std::memcmp(Bytes.Data(), Repeat.Data(), Bytes.Size()) == 0, "noncanonical paths are rejected without changing output");
    Result &= ExpectFormat(EncodeAssetFile(Id, "Game/Materials/\xc3\xa9.leasset", Material, Repeat) == EAssetFormatResult::InvalidPath, "v1 logical names use portable ASCII");
    for (const StringView ReservedPath : {"Game/Con.leasset", "Game/AuX/Test.leasset", "Game/NUL.leasset", "Game/pRn.leasset", "Game/Com9.leasset", "Game/lpt1.leasset", "Game/Materials./Blue.leasset"})
    {
        Result &= ExpectFormat(EncodeAssetFile(Id, ReservedPath, Material, Repeat) == EAssetFormatResult::InvalidPath, "v1 rejects reserved names and trailing dots");
    }
    Result &= ExpectFormat(EncodeAssetFile(Id, "Game/Console/Com10.leasset", Material, Repeat) == EAssetFormatResult::Success, "ordinary names with reserved prefixes remain usable");
    char LongPath[MaxAssetPathBytes + 1];
    std::memset(LongPath, 'a', sizeof(LongPath));
    LongPath[4] = '/';
    Result &= ExpectFormat(EncodeAssetFile(Id, StringView(LongPath, MaxAssetPathBytes), Material, Repeat) == EAssetFormatResult::Success && ReadAssetFileHeader(Repeat.AsSpan().First(MaxAssetFileHeaderBytes), Repeat.Size(), Header) == EAssetFormatResult::Success && Header.Path.Size() == MaxAssetPathBytes, "maximum path fits the catalog prefix");
    Result &= ExpectFormat(EncodeAssetFile(Id, StringView(LongPath, sizeof(LongPath)), Material, Repeat) == EAssetFormatResult::LimitExceeded, "overlength path fails before copying");
    Material.BaseColor.Values[0] = -1.0f;
    Result &= ExpectFormat(EncodeAssetFile(Id, Path, Material, Repeat) == EAssetFormatResult::InvalidPayload, "negative colors are rejected");

    Bytes[4] = 4;
    Result &= ExpectFormat(ReadAssetFileHeader(Bytes.AsSpan(), Header) == EAssetFormatResult::UnsupportedVersion && Header.Path.Size() == MaxAssetPathBytes, "unknown version preserves header output");
    Bytes[4] = MeshNormalsAssetFileVersion;
    Result &= ExpectFormat(ReadAssetFileHeader(Bytes.AsSpan(), Header) == EAssetFormatResult::UnsupportedVersion && Header.Path.Size() == MaxAssetPathBytes, "mesh v3 rejects material type");
    Bytes[4] = 1;
    Result &= ExpectFormat(ReadAssetFileHeader(Bytes.AsSpan().First(HeaderSize - 1), Bytes.Size(), Header) == EAssetFormatResult::InvalidHeader, "truncated path is rejected");
    Result &= ExpectFormat(ReadAssetFileHeader(Bytes.AsSpan().First(HeaderSize), MaxAssetFileBytes + 1, Header) == EAssetFormatResult::LimitExceeded, "file size cap applies to prefix reads");
    Bytes[44] = 0xff;
    Result &= ExpectFormat(ReadAssetFileHeader(Bytes.AsSpan(), Header) == EAssetFormatResult::InvalidHeader, "incorrect declared payload length is rejected");
    Bytes[44] = 12;
    std::memset(Bytes.Data() + 24, 0, FUuid::ByteCount);
    Result &= ExpectFormat(ReadAssetFileHeader(Bytes.AsSpan(), Header) == EAssetFormatResult::InvalidIdentity, "nil asset identity is rejected");
    return Result;
}

bool TestMesh(const FAssetId Id)
{
    FMeshAssetData Mesh;
    Mesh.Positions = {
        Math::Vector3f{0.0f, 1.0f, 2.0f},
        Math::Vector3f{3.0f, -4.0f, 2.0f},
        Math::Vector3f{-5.0f, 6.0f, 7.0f},
    };
    Mesh.Indices = {2, 0, 1};
    Array<uint8> Bytes;
    if (!ExpectFormat(EncodeAssetFile(Id, "Game/Meshes/Test.leasset", Mesh, Bytes) == EAssetFormatResult::Success, "mesh encodes"))
    {
        return false;
    }
    FMeshAssetData Decoded;
    bool Result = ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), Decoded) == EAssetFormatResult::Success && Decoded.Positions.Size() == 3 && Decoded.Positions[1] == Mesh.Positions[1] && Decoded.Indices.Size() == 3 && Decoded.Indices[0] == 2 && Decoded.Indices[1] == 0 && Decoded.BoundsMin == Math::Vector3f{-5.0f, -4.0f, 2.0f} && Decoded.BoundsMax == Math::Vector3f{3.0f, 6.0f, 7.0f}, "mesh round trip preserves winding and derives local bounds");
    FAssetFileHeaderView Header;
    if (!ExpectFormat(ReadAssetFileHeader(Bytes.AsSpan(), Header) == EAssetFormatResult::Success, "mesh header is readable"))
    {
        return false;
    }
    std::memset(Bytes.Data() + Header.PayloadOffset, 0xff, 4);
    Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), Decoded) == EAssetFormatResult::LimitExceeded, "hostile vertex count is rejected without count arithmetic overflow");
    uint8* const Payload = Bytes.Data() + Header.PayloadOffset;
    Payload[0] = 3;
    std::memset(Payload + 1, 0, 3);
    Bytes[Bytes.Size() - 4] = 3;
    Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), Decoded) == EAssetFormatResult::InvalidPayload && Decoded.Positions.Size() == 3 && Decoded.Indices[2] == 1, "out-of-range index preserves output");
    Bytes[Bytes.Size() - 4] = 1;
    constexpr uint8 Infinity[] = {0, 0, 0x80, 0x7f};
    std::memcpy(Payload + 8, Infinity, sizeof(Infinity));
    Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), Decoded) == EAssetFormatResult::InvalidPayload, "non-finite position from disk is rejected");
    Mesh.Positions[0].Values[0] = (std::numeric_limits<float>::quiet_NaN)();
    Result &= ExpectFormat(EncodeAssetFile(Id, "Game/Meshes/Test.leasset", Mesh, Bytes) == EAssetFormatResult::InvalidPayload, "non-finite authored position is rejected");
    return Result;
}

bool TestTexturedPayloads(const FAssetId Id)
{
    FAssetId TextureId;
    if (!FAssetId::TryParse("e8000000-0000-4000-8000-000000000002", TextureId))
    {
        return false;
    }
    FMaterialAssetData Material;
    Material.BaseColor = Math::Vector3f{0.5f, 1.0f, 0.25f};
    Material.BaseColorTexture = TextureId;
    Array<uint8> Bytes;
    FAssetFileHeaderView Header;
    FMaterialAssetData DecodedMaterial;
    const StringView MaterialPath = "Game/Materials/Test";
    if (!ExpectFormat(EncodeAssetFile(Id, MaterialPath, Material, Bytes) == EAssetFormatResult::Success && ReadAssetFileHeader(Bytes.AsSpan(), Header) == EAssetFormatResult::Success && Header.Version == TexturedAssetFileVersion && Header.PayloadSize == 28 && DecodeAssetFile(Bytes.AsSpan(), DecodedMaterial) == EAssetFormatResult::Success && DecodedMaterial.BaseColor == Material.BaseColor && DecodedMaterial.BaseColorTexture == TextureId, "v2 material round trips its linear tint and UUID"))
    {
        return false;
    }
    bool Result = ExpectFormat(std::memcmp(Bytes.Data() + Header.PayloadOffset + 12, TextureId.GetUuid().GetBytes().Data(), FUuid::ByteCount) == 0, "material uses canonical UUID byte order");
    std::memset(Bytes.Data() + Header.PayloadOffset + 12, 0, FUuid::ByteCount);
    Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), DecodedMaterial) == EAssetFormatResult::InvalidPayload && DecodedMaterial.BaseColorTexture == TextureId, "nil v2 material dependency preserves output");
    Material.BaseColorTexture = {};
    Result &= ExpectFormat(EncodeAssetFile(Id, MaterialPath, Material, Bytes) == EAssetFormatResult::Success && DecodeAssetFile(Bytes.AsSpan(), DecodedMaterial) == EAssetFormatResult::Success && !DecodedMaterial.BaseColorTexture.IsValid(), "v1 material resets a previous texture dependency");

    FMeshAssetData Mesh;
    Mesh.Positions = {Math::Vector3f{0, 0, 0}, Math::Vector3f{1, 0, 0}, Math::Vector3f{0, 1, 0}};
    Mesh.Indices = {2, 0, 1};
    Mesh.UVs = {Math::Vector2f{0, 0}, Math::Vector2f{1, 0}, Math::Vector2f{0, 1}};
    const StringView MeshPath = "Game/Meshes/Test";
    FMeshAssetData DecodedMesh;
    if (!ExpectFormat(EncodeAssetFile(Id, MeshPath, Mesh, Bytes) == EAssetFormatResult::Success && ReadAssetFileHeader(Bytes.AsSpan(), Header) == EAssetFormatResult::Success && Header.Version == TexturedAssetFileVersion && Header.PayloadSize == 80 && DecodeAssetFile(Bytes.AsSpan(), DecodedMesh) == EAssetFormatResult::Success && DecodedMesh.UVs.Size() == 3 && DecodedMesh.UVs[2] == Mesh.UVs[2] && DecodedMesh.Indices[0] == 2 && DecodedMesh.BoundsMax == Math::Vector3f{1, 1, 0}, "v2 mesh round trips UVs, positions, indices and derived bounds"))
    {
        return false;
    }
    Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan().First(Bytes.Size() - 1), DecodedMesh) == EAssetFormatResult::InvalidHeader && DecodedMesh.UVs.Size() == 3, "truncated v2 file preserves output");
    constexpr uint8 Infinity[] = {0, 0, 0x80, 0x7f};
    std::memcpy(Bytes.Data() + Header.PayloadOffset + 8 + 3 * 12, Infinity, sizeof(Infinity));
    Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), DecodedMesh) == EAssetFormatResult::InvalidPayload && DecodedMesh.UVs[2] == Mesh.UVs[2], "non-finite disk UV preserves output");
    Mesh.UVs[0][1] = (std::numeric_limits<float>::infinity)();
    Result &= ExpectFormat(EncodeAssetFile(Id, MeshPath, Mesh, Bytes) == EAssetFormatResult::InvalidPayload, "non-finite authored UV is rejected");
    Mesh.UVs = {Math::Vector2f{0, 0}};
    Result &= ExpectFormat(EncodeAssetFile(Id, MeshPath, Mesh, Bytes) == EAssetFormatResult::InvalidPayload, "mesh must have an entire UV channel");
    Mesh.UVs.Clear();
    Result &= ExpectFormat(EncodeAssetFile(Id, MeshPath, Mesh, Bytes) == EAssetFormatResult::Success && DecodeAssetFile(Bytes.AsSpan(), DecodedMesh) == EAssetFormatResult::Success && DecodedMesh.UVs.IsEmpty(), "v1 mesh replaces previous UV data with an empty channel");
    return Result;
}

bool TestTexture(const FAssetId Id)
{
    FCpuTextureAsset Texture;
    Texture.Width = 2;
    Texture.Height = 1;
    Texture.Pixels = {32, 3, 7, 255, 9, 11, 13, 255};
    const StringView Path = "Game/Textures/Test.leasset";
    Array<uint8> Bytes;
    if (!ExpectFormat(EncodeAssetFile(Id, Path, Texture, Bytes) == EAssetFormatResult::Success, "CPU texture encodes"))
    {
        return false;
    }
    FCpuTextureAsset Decoded;
    bool Result = ExpectFormat(GetAssetTypeId<FCpuTextureAsset>() == GetCpuTextureAssetTypeId() && GetCpuTextureAssetTypeId() != GetMeshAssetTypeId() && GetCpuTextureAssetTypeId() != GetMaterialAssetTypeId(), "texture has a distinct, stable registry type");
    Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), Decoded) == EAssetFormatResult::Success && Decoded.Width == Texture.Width && Decoded.Height == Texture.Height && Decoded.Pixels.Size() == Texture.Pixels.Size() && std::memcmp(Decoded.Pixels.Data(), Texture.Pixels.Data(), Texture.Pixels.Size()) == 0, "CPU texture preserves RGBA bytes");
    Texture.Width = MaxAssetTextureDimension + 1;
    Result &= ExpectFormat(EncodeAssetFile(Id, Path, Texture, Bytes) == EAssetFormatResult::LimitExceeded, "texture encoder caps dimensions");
    Texture.Width = 1;
    Result &= ExpectFormat(EncodeAssetFile(Id, Path, Texture, Bytes) == EAssetFormatResult::InvalidPayload, "texture encoder rejects excess pixel data");
    FAssetFileHeaderView Header;
    if (!ExpectFormat(ReadAssetFileHeader(Bytes.AsSpan(), Header) == EAssetFormatResult::Success, "texture header is readable"))
    {
        return false;
    }
    Bytes[4] = 2;
    Result &= ExpectFormat(ReadAssetFileHeader(Bytes.AsSpan(), Header) == EAssetFormatResult::UnsupportedVersion && Header.Version == AssetFileVersion, "v2 is unsupported for texture and preserves header output");
    Bytes[4] = 1;
    std::memset(Bytes.Data() + Header.PayloadOffset, 0xff, 8);
    Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), Decoded) == EAssetFormatResult::LimitExceeded && Decoded.Width == 2 && Decoded.Height == 1 && Decoded.Pixels.Size() == 8, "oversized disk dimensions preserve previous texture");
    return Result;
}

bool TestMeshNormals(const FAssetId Id)
{
    const StringView Path = "Game/Meshes/Normals";
    FMeshAssetData Mesh;
    Mesh.Positions = {Math::Vector3f{0, 0, 0}, Math::Vector3f{1, 0, 0}, Math::Vector3f{0, 1, 0}};
    Mesh.Indices = {2, 1, 0};
    const Array<Math::Vector3f> Normals{Math::Vector3f{0, 0, 1}, Math::Vector3f{0, 1, 0}, Math::Vector3f{1, 0, 0}};
    const Array<Math::Vector2f> UVs{Math::Vector2f{0, 0}, Math::Vector2f{1, 0}, Math::Vector2f{0, 1}};
    FMeshAssetData Decoded;
    Array<uint8> Bytes;
    FAssetFileHeaderView Header;
    bool Result = true;
    for (const bool bHasUVs : {false, true})
    {
        Mesh.Normals = Normals;
        if (bHasUVs)
        {
            Mesh.UVs = UVs;
        }
        if (!ExpectFormat(EncodeAssetFile(Id, Path, Mesh, Bytes) == EAssetFormatResult::Success && ReadAssetFileHeader(Bytes.AsSpan(), Header) == EAssetFormatResult::Success && Header.Version == MeshNormalsAssetFileVersion && Header.PayloadSize == (bHasUVs ? 120 : 96) && DecodeAssetFile(Bytes.AsSpan(), Decoded) == EAssetFormatResult::Success && Decoded.UVs.Size() == Mesh.UVs.Size() && Decoded.Normals.Size() == 3 && Decoded.Normals[2] == Normals[2] && Decoded.Indices[0] == 2 && Decoded.BoundsMax == Math::Vector3f{1, 1, 0}, "v3 round trip with optional UVs"))
        {
            return false;
        }
        uint8* const Payload = Bytes.Data() + Header.PayloadOffset;
        Result &= ExpectFormat(Payload[8] == (bHasUVs ? 1 : 0), "v3 mesh emits the UV presence flag");
        Payload[8] |= 0x80;
        Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), Decoded) == EAssetFormatResult::InvalidPayload && Decoded.Normals[2] == Normals[2], "reserved flags preserve prior output");
        Payload[8] = bHasUVs ? 0 : 1;
        Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), Decoded) == EAssetFormatResult::InvalidPayload, "payload length must agree with the UV flag");
        Payload[8] = bHasUVs ? 1 : 0;
        Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan().First(Bytes.Size() - 1), Decoded) == EAssetFormatResult::InvalidHeader && Decoded.Normals.Size() == 3, "truncated v3 mesh preserves output");
        uint8* const FirstIndex = Payload + Header.PayloadSize - 3 * sizeof(uint32);
        FirstIndex[0] = 3;
        Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), Decoded) == EAssetFormatResult::InvalidPayload && Decoded.Indices[0] == 2, "v3 checks index bounds and preserves output");
        FirstIndex[0] = 2;
        uint8* const FirstNormal = FirstIndex - 3 * 12;
        std::memset(FirstNormal, 0, 12);
        Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), Decoded) == EAssetFormatResult::InvalidPayload && Decoded.Normals[0] == Normals[0], "zero normal preserves decoded output");
        constexpr uint8 Infinity[] = {0, 0, 0x80, 0x7f};
        std::memcpy(FirstNormal, Infinity, sizeof(Infinity));
        Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), Decoded) == EAssetFormatResult::InvalidPayload && Decoded.Normals[0] == Normals[0], "nonfinite normal preserves decoded output");

        const Array<uint8> PriorBytes = Bytes;
        Mesh.Normals[0] = Math::Vector3f{0, 0, 2};
        Result &= ExpectFormat(EncodeAssetFile(Id, Path, Mesh, Bytes) == EAssetFormatResult::InvalidPayload && Bytes.Size() == PriorBytes.Size() && std::memcmp(Bytes.Data(), PriorBytes.Data(), Bytes.Size()) == 0, "nonunit normal preserves encoded output");
        Mesh.Normals = {Math::Vector3f{0, 0, 1}};
        Result &= ExpectFormat(EncodeAssetFile(Id, Path, Mesh, Bytes) == EAssetFormatResult::InvalidPayload, "mesh requires an entire normal channel");
        Mesh.Normals.Clear();
        Result &= ExpectFormat(EncodeAssetFile(Id, Path, Mesh, Bytes) == EAssetFormatResult::Success && ReadAssetFileHeader(Bytes.AsSpan(), Header) == EAssetFormatResult::Success && Header.Version == (bHasUVs ? TexturedAssetFileVersion : AssetFileVersion) && DecodeAssetFile(Bytes.AsSpan(), Decoded) == EAssetFormatResult::Success && Decoded.Normals.IsEmpty() && Decoded.UVs.Size() == Mesh.UVs.Size(), "v1/v2 decode clears prior normals");
    }
    return Result;
}

bool TestAllocationFailure(const FAssetId Id)
{
    FMeshAssetData Mesh;
    Mesh.Positions = {
        Math::Vector3f{0.0f, 0.0f, 0.0f},
        Math::Vector3f{1.0f, 0.0f, 0.0f},
        Math::Vector3f{0.0f, 1.0f, 0.0f},
    };
    Mesh.Indices = {0, 1, 2};
    Mesh.UVs = {Math::Vector2f{0, 0}, Math::Vector2f{1, 0}, Math::Vector2f{0, 1}};
    const StringView Path = "Game/LongerThanSmallStringAssetPath/Oom.leasset";
    Mesh.Normals = {Math::Vector3f{0, 0, 1}, Math::Vector3f{0, 0, 1}, Math::Vector3f{0, 0, 1}};
    Array<uint8> Bytes;
    if (!ExpectFormat(EncodeAssetFile(Id, Path, Mesh, Bytes) == EAssetFormatResult::Success, "valid input for allocator probes"))
    {
        return false;
    }
    FFormatTestAllocator Allocator;
    bool Result = true;
    {
        Array<uint8> Encoded(Allocator);
        if (!ExpectFormat(Encoded.TryPushBack(17), "seed allocator probe"))
        {
            return false;
        }
        const std::size_t OriginalLive = Allocator.Live;
        bool ReachedSuccess = false;
        for (std::size_t Failure = 1; Failure <= 32; ++Failure)
        {
            Allocator.FailAt = Allocator.Attempts + Failure;
            const EAssetFormatResult Status = EncodeAssetFile(Id, Path, Mesh, Encoded);
            if (Status == EAssetFormatResult::Success)
            {
                ReachedSuccess = true;
                break;
            }
            Result &= ExpectFormat(Status == EAssetFormatResult::AllocationFailed && Encoded.Size() == 1 && Encoded[0] == 17 && Allocator.Live == OriginalLive, "encode allocation failure keeps original bytes and releases candidate");
        }
        Result &= ExpectFormat(ReachedSuccess, "encode succeeds after allocation failures");
    }
    Allocator.FailAt = 0;
    {
        FMeshAssetData Decoded(Allocator);
        Decoded.BoundsMin = Math::Vector3f{19.0f};
        bool ReachedSuccess = false;
        for (std::size_t Failure = 1; Failure <= 32; ++Failure)
        {
            Allocator.FailAt = Allocator.Attempts + Failure;
            const EAssetFormatResult Status = DecodeAssetFile(Bytes.AsSpan(), Decoded);
            if (Status == EAssetFormatResult::Success)
            {
                ReachedSuccess = true;
                break;
            }
            Result &= ExpectFormat(Status == EAssetFormatResult::AllocationFailed && Decoded.Positions.IsEmpty() && Decoded.Indices.IsEmpty() && Decoded.UVs.IsEmpty() && Decoded.Normals.IsEmpty() && Decoded.BoundsMin == Math::Vector3f{19.0f} && Allocator.Live == 0, "decode allocation failure preserves payload and releases candidate");
        }
        Result &= ExpectFormat(ReachedSuccess, "decode succeeds after allocation failures");
    }
    return ExpectFormat(Allocator.Live == 0, "all codec allocations freed") && Result;
}

} // namespace

bool RunAssetFormatTests()
{
    FAssetId Id;
    if (!ExpectFormat(FAssetId::TryParse("e8000000-0000-4000-8000-000000000001", Id), "test asset ID parses"))
    {
        return false;
    }
    bool Result = TestMaterial(Id);
    Result &= TestMesh(Id);
    Result &= TestTexturedPayloads(Id);
    Result &= TestMeshNormals(Id);
    Result &= TestTexture(Id);
    Result &= TestAllocationFailure(Id);
    return Result;
}

} // namespace LE
