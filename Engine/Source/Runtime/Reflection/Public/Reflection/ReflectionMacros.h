#pragma once

#include <cstddef>

namespace LE
{
namespace Detail
{

template <typename T>
struct TReflectionGeneratedAccess;

constexpr bool IsReflectionStableIdHexDigit(const char Character) noexcept
{
    return (Character >= '0' && Character <= '9') ||
        (Character >= 'a' && Character <= 'f');
}

template <std::size_t Length>
constexpr bool IsValidReflectionStableIdLiteral(const char (&Text)[Length]) noexcept
{
    if (Length != 37 || Text[36] != '\0')
    {
        return false;
    }

    bool bHasNonZeroDigit = false;
    for (std::size_t Index = 0; Index < 36; ++Index)
    {
        const bool bSeparator = Index == 8 || Index == 13 || Index == 18 || Index == 23;
        if (bSeparator)
        {
            if (Text[Index] != '-')
            {
                return false;
            }
            continue;
        }

        if (!IsReflectionStableIdHexDigit(Text[Index]))
        {
            return false;
        }
        bHasNonZeroDigit = bHasNonZeroDigit || Text[Index] != '0';
    }
    return bHasNonZeroDigit;
}

} // namespace Detail
} // namespace LE

// These macros are the stable engine-owned declaration surface. They validate
// persistent identity but intentionally emit no metadata or registration.
#define LE_STRUCT(StableId) \
    static_assert(::LE::Detail::IsValidReflectionStableIdLiteral(StableId), \
        "LE_STRUCT requires one lowercase canonical non-nil UUID string literal");

#define LE_CLASS(StableId) \
    static_assert(::LE::Detail::IsValidReflectionStableIdLiteral(StableId), \
        "LE_CLASS requires one lowercase canonical non-nil UUID string literal");

#define LE_ENUM(StableId) \
    static_assert(::LE::Detail::IsValidReflectionStableIdLiteral(StableId), \
        "LE_ENUM requires one lowercase canonical non-nil UUID string literal");

#define LE_PROPERTY(StableId) \
    static_assert(::LE::Detail::IsValidReflectionStableIdLiteral(StableId), \
        "LE_PROPERTY requires one lowercase canonical non-nil UUID string literal");

// The generator specializes this access template in Task 13. Friendship adds
// no storage, initialization, or registration and does not change access mode.
#define LE_GENERATED_BODY() \
    template <typename> friend struct ::LE::Detail::TReflectionGeneratedAccess;
