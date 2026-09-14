#pragma once

#include "Containers/HashMap.h"
#include "Containers/String.h"
#include "Memory/Allocator.h"
#include "Types/EngineTypes.h"

#include <cstddef>

namespace LE
{

enum class ESoftAssetPathResult : uint8
{
    Success = 0,
    Empty,
    AbsolutePath,
    InvalidUtf8,
    InvalidCharacter,
    RootEscape,
    InvalidStructure,
    AllocationFailed,
};

// An owned, canonical virtual asset path. This value is never an operating-
// system path and deliberately performs neither Unicode normalization nor case
// folding.
class ASSET_API FSoftAssetPath final
{
public:
    FSoftAssetPath() noexcept = default;
    explicit FSoftAssetPath(IAllocator& Allocator) noexcept : Value(Allocator) {}

    bool IsValid() const noexcept { return !Value.IsEmpty(); }
    StringView View() const noexcept { return Value.View(); }
    IAllocator& GetAllocator() const noexcept { return Value.GetAllocator(); }

    ESoftAssetPathResult TryAssign(StringView Source) noexcept;
    static ESoftAssetPathResult TryParse(
        StringView Source,
        FSoftAssetPath& OutPath) noexcept
    {
        return OutPath.TryAssign(Source);
    }

    void Reset() noexcept { Value.Clear(); }

    friend bool operator==(const FSoftAssetPath& Left, const FSoftAssetPath& Right) noexcept
    {
        return Left.Value == Right.Value;
    }
    friend bool operator!=(const FSoftAssetPath& Left, const FSoftAssetPath& Right) noexcept
    {
        return !(Left == Right);
    }
    friend bool operator<(const FSoftAssetPath& Left, const FSoftAssetPath& Right) noexcept
    {
        return Left.Value < Right.Value;
    }

private:
    String Value;
};

template <>
struct DefaultHash<FSoftAssetPath>
{
    std::size_t operator()(const FSoftAssetPath& Path) const noexcept
    {
        return StringViewHash{}(Path.View());
    }
};

} // namespace LE
