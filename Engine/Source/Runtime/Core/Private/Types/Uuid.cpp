#include "Types/Uuid.h"

#if PLATFORM_WINDOWS
#include <Windows.h>
#include <bcrypt.h>
#elif PLATFORM_MAC
#include <Security/SecRandom.h>
#endif

namespace LE
{
namespace
{

constexpr char HexDigits[] = "0123456789abcdef";

int HexValue(const char Character) noexcept
{
    if (Character >= '0' && Character <= '9')
    {
        return Character - '0';
    }
    if (Character >= 'a' && Character <= 'f')
    {
        return Character - 'a' + 10;
    }
    return -1;
}

bool FillRandomBytes(uint8* const Bytes, const std::size_t Size) noexcept
{
#if PLATFORM_WINDOWS
    return BCryptGenRandom(
        nullptr,
        reinterpret_cast<PUCHAR>(Bytes),
        static_cast<ULONG>(Size),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
#elif PLATFORM_MAC
    return SecRandomCopyBytes(kSecRandomDefault, Size, Bytes) == errSecSuccess;
#else
    static_cast<void>(Bytes);
    static_cast<void>(Size);
    return false;
#endif
}

} // namespace

bool FUuid::IsValid() const noexcept
{
    for (const uint8 Byte : Bytes)
    {
        if (Byte != 0)
        {
            return true;
        }
    }
    return false;
}

String FUuid::ToString() const
{
    char Text[CanonicalStringLength];
    std::size_t TextIndex = 0;
    for (std::size_t ByteIndex = 0; ByteIndex < ByteCount; ++ByteIndex)
    {
        if (ByteIndex == 4 || ByteIndex == 6 || ByteIndex == 8 || ByteIndex == 10)
        {
            Text[TextIndex++] = '-';
        }
        const uint8 Byte = Bytes[ByteIndex];
        Text[TextIndex++] = HexDigits[Byte >> 4];
        Text[TextIndex++] = HexDigits[Byte & 0x0Fu];
    }
    return String(Text, CanonicalStringLength);
}

bool FUuid::TryParse(const StringView Text, FUuid& OutUuid) noexcept
{
    if (Text.Size() != CanonicalStringLength)
    {
        return false;
    }

    FUuid Parsed;
    std::size_t ByteIndex = 0;
    for (std::size_t TextIndex = 0; TextIndex < Text.Size();)
    {
        if (TextIndex == 8 || TextIndex == 13 || TextIndex == 18 || TextIndex == 23)
        {
            if (Text[TextIndex] != '-')
            {
                return false;
            }
            ++TextIndex;
            continue;
        }

        const int High = HexValue(Text[TextIndex]);
        const int Low = HexValue(Text[TextIndex + 1]);
        if (High < 0 || Low < 0)
        {
            return false;
        }
        Parsed.Bytes[ByteIndex++] = static_cast<uint8>((High << 4) | Low);
        TextIndex += 2;
    }

    OutUuid = Parsed;
    return true;
}

bool FUuid::TryGenerate(FUuid& OutUuid) noexcept
{
    FUuid Generated;
    if (!FillRandomBytes(Generated.Bytes, ByteCount))
    {
        return false;
    }

    Generated.Bytes[6] = static_cast<uint8>((Generated.Bytes[6] & 0x0Fu) | 0x40u);
    Generated.Bytes[8] = static_cast<uint8>((Generated.Bytes[8] & 0x3Fu) | 0x80u);
    OutUuid = Generated;
    return true;
}

bool operator==(const FUuid& Left, const FUuid& Right) noexcept
{
    for (std::size_t Index = 0; Index < FUuid::ByteCount; ++Index)
    {
        if (Left.Bytes[Index] != Right.Bytes[Index])
        {
            return false;
        }
    }
    return true;
}

bool operator<(const FUuid& Left, const FUuid& Right) noexcept
{
    for (std::size_t Index = 0; Index < FUuid::ByteCount; ++Index)
    {
        if (Left.Bytes[Index] < Right.Bytes[Index])
        {
            return true;
        }
        if (Left.Bytes[Index] > Right.Bytes[Index])
        {
            return false;
        }
    }
    return false;
}

std::size_t FUuidHash::operator()(const FUuid& Value) const noexcept
{
    std::size_t Hash = sizeof(std::size_t) > 4
        ? static_cast<std::size_t>(14695981039346656037ULL)
        : static_cast<std::size_t>(2166136261U);
    const std::size_t Prime = sizeof(std::size_t) > 4
        ? static_cast<std::size_t>(1099511628211ULL)
        : static_cast<std::size_t>(16777619U);
    for (const uint8 Byte : Value.GetBytes())
    {
        Hash ^= Byte;
        Hash *= Prime;
    }
    return Hash;
}

} // namespace LE
