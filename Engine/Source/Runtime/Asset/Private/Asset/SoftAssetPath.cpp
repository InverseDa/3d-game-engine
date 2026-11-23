#include "Asset/SoftAssetPath.h"

#include "Containers/Array.h"

#include <limits>

namespace LE
{
namespace
{

bool IsSeparator(const char Character) noexcept
{
    return Character == '/' || Character == '\\';
}

bool IsSafeAsciiPathCharacter(const unsigned char Character) noexcept
{
    return (Character >= 'A' && Character <= 'Z') || (Character >= 'a' && Character <= 'z') || (Character >= '0' && Character <= '9') || Character == '_' || Character == '-' || Character == '.';
}

bool IsContinuation(const unsigned char Character) noexcept
{
    return (Character & 0xc0u) == 0x80u;
}

std::size_t ValidUtf8SequenceLength(const char* const Data, const std::size_t Remaining) noexcept
{
    if (Remaining == 0)
    {
        return 0;
    }

    const unsigned char First = static_cast<unsigned char>(Data[0]);
    if (First <= 0x7fu)
    {
        return 1;
    }
    if (First >= 0xc2u && First <= 0xdfu)
    {
        return Remaining >= 2 && IsContinuation(static_cast<unsigned char>(Data[1])) ? 2 : 0;
    }
    if (First == 0xe0u)
    {
        const unsigned char Second = Remaining >= 2 ? static_cast<unsigned char>(Data[1]) : 0;
        return Remaining >= 3 && Second >= 0xa0u && Second <= 0xbfu && IsContinuation(static_cast<unsigned char>(Data[2])) ? 3 : 0;
    }
    if ((First >= 0xe1u && First <= 0xecu) || (First >= 0xeeu && First <= 0xefu))
    {
        return Remaining >= 3 && IsContinuation(static_cast<unsigned char>(Data[1])) && IsContinuation(static_cast<unsigned char>(Data[2])) ? 3 : 0;
    }
    if (First == 0xedu)
    {
        const unsigned char Second = Remaining >= 2 ? static_cast<unsigned char>(Data[1]) : 0;
        return Remaining >= 3 && Second >= 0x80u && Second <= 0x9fu && IsContinuation(static_cast<unsigned char>(Data[2])) ? 3 : 0;
    }
    if (First == 0xf0u)
    {
        const unsigned char Second = Remaining >= 2 ? static_cast<unsigned char>(Data[1]) : 0;
        return Remaining >= 4 && Second >= 0x90u && Second <= 0xbfu && IsContinuation(static_cast<unsigned char>(Data[2])) && IsContinuation(static_cast<unsigned char>(Data[3])) ? 4 : 0;
    }
    if (First >= 0xf1u && First <= 0xf3u)
    {
        return Remaining >= 4 && IsContinuation(static_cast<unsigned char>(Data[1])) && IsContinuation(static_cast<unsigned char>(Data[2])) && IsContinuation(static_cast<unsigned char>(Data[3])) ? 4 : 0;
    }
    if (First == 0xf4u)
    {
        const unsigned char Second = Remaining >= 2 ? static_cast<unsigned char>(Data[1]) : 0;
        return Remaining >= 4 && Second >= 0x80u && Second <= 0x8fu && IsContinuation(static_cast<unsigned char>(Data[2])) && IsContinuation(static_cast<unsigned char>(Data[3])) ? 4 : 0;
    }
    return 0;
}

ESoftAssetPathResult ValidateSegment(const StringView Segment) noexcept
{
    std::size_t Index = 0;
    while (Index < Segment.Size())
    {
        const unsigned char Character = static_cast<unsigned char>(Segment[Index]);
        if (Character <= 0x7fu)
        {
            if (!IsSafeAsciiPathCharacter(Character))
            {
                return ESoftAssetPathResult::InvalidCharacter;
            }
            ++Index;
            continue;
        }

        const std::size_t Length = ValidUtf8SequenceLength(Segment.Data() + Index, Segment.Size() - Index);
        if (Length == 0)
        {
            return ESoftAssetPathResult::InvalidUtf8;
        }
        Index += Length;
    }
    return ESoftAssetPathResult::Success;
}

} // namespace

ESoftAssetPathResult FSoftAssetPath::TryAssign(const StringView Source) noexcept
{
    if (Source.IsEmpty())
    {
        return ESoftAssetPathResult::Empty;
    }
    if (IsSeparator(Source[0]))
    {
        return ESoftAssetPathResult::AbsolutePath;
    }
    if (IsSeparator(Source[Source.Size() - 1]))
    {
        return ESoftAssetPathResult::InvalidStructure;
    }

    Array<StringView> Segments(GetAllocator());
    std::size_t Cursor = 0;
    while (Cursor < Source.Size())
    {
        while (Cursor < Source.Size() && IsSeparator(Source[Cursor]))
        {
            ++Cursor;
        }
        if (Cursor == Source.Size())
        {
            break;
        }

        const std::size_t Start = Cursor;
        while (Cursor < Source.Size() && !IsSeparator(Source[Cursor]))
        {
            ++Cursor;
        }
        const StringView Segment = Source.Substr(Start, Cursor - Start);
        if (Segment == StringView("."))
        {
            continue;
        }
        if (Segment == StringView(".."))
        {
            if (Segments.Size() <= 1)
            {
                return ESoftAssetPathResult::RootEscape;
            }
            Segments.PopBack();
            continue;
        }

        const ESoftAssetPathResult Validation = ValidateSegment(Segment);
        if (Validation != ESoftAssetPathResult::Success)
        {
            return Validation;
        }
        if (!Segments.TryPushBack(Segment))
        {
            return ESoftAssetPathResult::AllocationFailed;
        }
    }

    if (Segments.Size() < 2)
    {
        return ESoftAssetPathResult::InvalidStructure;
    }

    std::size_t CanonicalSize = Segments.Size() - 1;
    for (const StringView Segment : Segments)
    {
        if (Segment.Size() > (std::numeric_limits<std::size_t>::max)() - CanonicalSize)
        {
            return ESoftAssetPathResult::AllocationFailed;
        }
        CanonicalSize += Segment.Size();
    }

    String Canonical(GetAllocator());
    if (!Canonical.TryReserve(CanonicalSize))
    {
        return ESoftAssetPathResult::AllocationFailed;
    }
    for (std::size_t Index = 0; Index < Segments.Size(); ++Index)
    {
        if (Index != 0 && !Canonical.TryAppend("/"))
        {
            return ESoftAssetPathResult::AllocationFailed;
        }
        if (!Canonical.TryAppend(Segments[Index]))
        {
            return ESoftAssetPathResult::AllocationFailed;
        }
    }

    Value.Swap(Canonical);
    return ESoftAssetPathResult::Success;
}

} // namespace LE
