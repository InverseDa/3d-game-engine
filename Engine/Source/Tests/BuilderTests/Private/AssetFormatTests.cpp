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
    if (!ExpectFormat(EncodeAssetFile(Id, Path, Material, Bytes) == EAssetFormatResult::Success
        && EncodeAssetFile(Id, Path, Material, Repeat) == EAssetFormatResult::Success,
        "material encodes"))
    {
        return false;
    }
    const std::size_t HeaderSize = 48 + Path.Size();
    FAssetFileHeaderView Header;
    bool Result = ExpectFormat(Bytes.Size() == Repeat.Size()
        && std::memcmp(Bytes.Data(), Repeat.Data(), Bytes.Size()) == 0,
        "fixed inputs produce the same bytes");
    Result &= ExpectFormat(ReadAssetFileHeader(Bytes.AsSpan().First(HeaderSize), Bytes.Size(), Header)
        == EAssetFormatResult::Success && Header.Id == Id && Header.TypeId == GetMaterialAssetTypeId()
        && Header.Path == Path && Header.PayloadOffset == HeaderSize && Header.PayloadSize == 12,
        "catalog parses a prefix without reading the payload");
    constexpr uint8 ExpectedPrefix[] = {'L', 'E', 'A', 'S', 1, 0, 0, 0};
    constexpr uint8 ExpectedRgb[] = {0, 0, 0x80, 0x3E, 0, 0, 0, 0, 0, 0, 0, 0x40};
    Result &= ExpectFormat(std::memcmp(Bytes.Data(), ExpectedPrefix, sizeof(ExpectedPrefix)) == 0
        && std::memcmp(Bytes.Data() + HeaderSize, ExpectedRgb, sizeof(ExpectedRgb)) == 0,
        "golden bytes use little endian version and IEEE-754 RGB");
    FMaterialAssetData Decoded;
    Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), Decoded) == EAssetFormatResult::Success
        && Decoded.BaseColor == Material.BaseColor, "material round trip");
    FMeshAssetData WrongType;
    Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), WrongType) == EAssetFormatResult::TypeMismatch,
        "typed payload rejects an unrelated asset");
    Result &= ExpectFormat(EncodeAssetFile(Id, "Game//Materials/Blue.leasset", Material, Repeat)
        == EAssetFormatResult::InvalidPath && Repeat.Size() == Bytes.Size()
        && std::memcmp(Bytes.Data(), Repeat.Data(), Bytes.Size()) == 0,
        "noncanonical paths are rejected without changing output");
    Result &= ExpectFormat(EncodeAssetFile(Id, "Game/Materials/\xc3\xa9.leasset", Material, Repeat)
        == EAssetFormatResult::InvalidPath, "v1 logical names use portable ASCII");
    for (const StringView ReservedPath : {"Game/Con.leasset", "Game/AuX/Test.leasset",
        "Game/NUL.leasset", "Game/pRn.leasset", "Game/Com9.leasset", "Game/lpt1.leasset",
        "Game/Materials./Blue.leasset"})
    {
        Result &= ExpectFormat(EncodeAssetFile(Id, ReservedPath, Material, Repeat)
            == EAssetFormatResult::InvalidPath, "v1 rejects reserved names and trailing dots");
    }
    Result &= ExpectFormat(EncodeAssetFile(Id, "Game/Console/Com10.leasset", Material, Repeat)
        == EAssetFormatResult::Success, "ordinary names with reserved prefixes remain usable");
    char LongPath[MaxAssetPathBytes + 1];
    std::memset(LongPath, 'a', sizeof(LongPath));
    LongPath[4] = '/';
    Result &= ExpectFormat(EncodeAssetFile(Id, StringView(LongPath, MaxAssetPathBytes), Material, Repeat)
        == EAssetFormatResult::Success && ReadAssetFileHeader(Repeat.AsSpan().First(MaxAssetFileHeaderBytes),
            Repeat.Size(), Header) == EAssetFormatResult::Success
        && Header.Path.Size() == MaxAssetPathBytes, "maximum path fits the catalog prefix");
    Result &= ExpectFormat(EncodeAssetFile(Id, StringView(LongPath, sizeof(LongPath)), Material, Repeat)
        == EAssetFormatResult::LimitExceeded, "overlength path fails before copying");
    Material.BaseColor.Values[0] = -1.0f;
    Result &= ExpectFormat(EncodeAssetFile(Id, Path, Material, Repeat) == EAssetFormatResult::InvalidPayload,
        "negative colors are rejected");

    Bytes[4] = 2;
    Result &= ExpectFormat(ReadAssetFileHeader(Bytes.AsSpan(), Header) == EAssetFormatResult::UnsupportedVersion
        && Header.Path.Size() == MaxAssetPathBytes, "unknown version preserves header output");
    Bytes[4] = 1;
    Result &= ExpectFormat(ReadAssetFileHeader(Bytes.AsSpan().First(HeaderSize - 1), Bytes.Size(), Header)
        == EAssetFormatResult::InvalidHeader, "truncated path is rejected");
    Result &= ExpectFormat(ReadAssetFileHeader(Bytes.AsSpan().First(HeaderSize), MaxAssetFileBytes + 1, Header)
        == EAssetFormatResult::LimitExceeded, "file size cap applies to prefix reads");
    Bytes[44] = 0xff;
    Result &= ExpectFormat(ReadAssetFileHeader(Bytes.AsSpan(), Header) == EAssetFormatResult::InvalidHeader,
        "incorrect declared payload length is rejected");
    Bytes[44] = 12;
    std::memset(Bytes.Data() + 24, 0, FUuid::ByteCount);
    Result &= ExpectFormat(ReadAssetFileHeader(Bytes.AsSpan(), Header) == EAssetFormatResult::InvalidIdentity,
        "nil asset identity is rejected");
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
    if (!ExpectFormat(EncodeAssetFile(Id, "Game/Meshes/Test.leasset", Mesh, Bytes)
        == EAssetFormatResult::Success, "mesh encodes"))
    {
        return false;
    }
    FMeshAssetData Decoded;
    bool Result = ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), Decoded) == EAssetFormatResult::Success
        && Decoded.Positions.Size() == 3 && Decoded.Positions[1] == Mesh.Positions[1]
        && Decoded.Indices.Size() == 3 && Decoded.Indices[0] == 2 && Decoded.Indices[1] == 0
        && Decoded.BoundsMin == Math::Vector3f{-5.0f, -4.0f, 2.0f}
        && Decoded.BoundsMax == Math::Vector3f{3.0f, 6.0f, 7.0f},
        "mesh round trip preserves winding and derives local bounds");
    FAssetFileHeaderView Header;
    if (!ExpectFormat(ReadAssetFileHeader(Bytes.AsSpan(), Header) == EAssetFormatResult::Success,
        "mesh header is readable"))
    {
        return false;
    }
    std::memset(Bytes.Data() + Header.PayloadOffset, 0xff, 4);
    Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), Decoded) == EAssetFormatResult::LimitExceeded,
        "hostile vertex count is rejected without count arithmetic overflow");
    uint8* const Payload = Bytes.Data() + Header.PayloadOffset;
    Payload[0] = 3;
    std::memset(Payload + 1, 0, 3);
    Bytes[Bytes.Size() - 4] = 3;
    Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), Decoded) == EAssetFormatResult::InvalidPayload
        && Decoded.Positions.Size() == 3 && Decoded.Indices[2] == 1, "out-of-range index preserves output");
    Bytes[Bytes.Size() - 4] = 1;
    constexpr uint8 Infinity[] = {0, 0, 0x80, 0x7f};
    std::memcpy(Payload + 8, Infinity, sizeof(Infinity));
    Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), Decoded) == EAssetFormatResult::InvalidPayload,
        "non-finite position from disk is rejected");
    Mesh.Positions[0].Values[0] = (std::numeric_limits<float>::quiet_NaN)();
    Result &= ExpectFormat(EncodeAssetFile(Id, "Game/Meshes/Test.leasset", Mesh, Bytes)
        == EAssetFormatResult::InvalidPayload, "non-finite authored position is rejected");
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
    if (!ExpectFormat(EncodeAssetFile(Id, Path, Texture, Bytes) == EAssetFormatResult::Success,
        "CPU texture encodes"))
    {
        return false;
    }
    FCpuTextureAsset Decoded;
    bool Result = ExpectFormat(GetAssetTypeId<FCpuTextureAsset>() == GetCpuTextureAssetTypeId()
        && GetCpuTextureAssetTypeId() != GetMeshAssetTypeId()
        && GetCpuTextureAssetTypeId() != GetMaterialAssetTypeId(),
        "texture has a distinct, stable registry type");
    Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), Decoded) == EAssetFormatResult::Success
        && Decoded.Width == Texture.Width && Decoded.Height == Texture.Height
        && Decoded.Pixels.Size() == Texture.Pixels.Size()
        && std::memcmp(Decoded.Pixels.Data(), Texture.Pixels.Data(), Texture.Pixels.Size()) == 0,
        "CPU texture preserves RGBA bytes");
    Texture.Width = MaxAssetTextureDimension + 1;
    Result &= ExpectFormat(EncodeAssetFile(Id, Path, Texture, Bytes) == EAssetFormatResult::LimitExceeded,
        "texture encoder caps dimensions");
    Texture.Width = 1;
    Result &= ExpectFormat(EncodeAssetFile(Id, Path, Texture, Bytes) == EAssetFormatResult::InvalidPayload,
        "texture encoder rejects excess pixel data");
    FAssetFileHeaderView Header;
    if (!ExpectFormat(ReadAssetFileHeader(Bytes.AsSpan(), Header) == EAssetFormatResult::Success,
        "texture header is readable"))
    {
        return false;
    }
    std::memset(Bytes.Data() + Header.PayloadOffset, 0xff, 8);
    Result &= ExpectFormat(DecodeAssetFile(Bytes.AsSpan(), Decoded) == EAssetFormatResult::LimitExceeded
        && Decoded.Width == 2 && Decoded.Height == 1 && Decoded.Pixels.Size() == 8,
        "oversized disk dimensions preserve previous texture");
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
    const StringView Path = "Game/LongerThanSmallStringAssetPath/Oom.leasset";
    Array<uint8> Bytes;
    if (!ExpectFormat(EncodeAssetFile(Id, Path, Mesh, Bytes) == EAssetFormatResult::Success,
        "valid input for allocator probes"))
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
            Result &= ExpectFormat(Status == EAssetFormatResult::AllocationFailed
                && Encoded.Size() == 1 && Encoded[0] == 17 && Allocator.Live == OriginalLive,
                "encode allocation failure keeps original bytes and releases candidate");
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
            Result &= ExpectFormat(Status == EAssetFormatResult::AllocationFailed
                && Decoded.Positions.IsEmpty() && Decoded.Indices.IsEmpty()
                && Decoded.BoundsMin == Math::Vector3f{19.0f} && Allocator.Live == 0,
                "decode allocation failure preserves payload and releases candidate");
        }
        Result &= ExpectFormat(ReachedSuccess, "decode succeeds after allocation failures");
    }
    return ExpectFormat(Allocator.Live == 0, "all codec allocations freed") && Result;
}

} // namespace

bool RunAssetFormatTests()
{
    FAssetId Id;
    if (!ExpectFormat(FAssetId::TryParse("e8000000-0000-4000-8000-000000000001", Id),
        "test asset ID parses"))
    {
        return false;
    }
    bool Result = TestMaterial(Id);
    Result &= TestMesh(Id);
    Result &= TestTexture(Id);
    Result &= TestAllocationFailure(Id);
    return Result;
}

} // namespace LE
