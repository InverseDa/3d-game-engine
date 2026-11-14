#include "AssetImport.h"

#include "Containers/Array.h"
#include "Containers/String.h"

#include <cstdio>
#include <utility>

#if PLATFORM_WINDOWS
#include <Windows.h>
#endif

namespace LE
{
namespace
{

#if PLATFORM_WINDOWS
int RunWindowsAssetImport(const int ArgumentCount, wchar_t* const* Arguments) noexcept
{
    Array<String> Text;
    Array<StringView> Views;
    const std::size_t Count = ArgumentCount > 0 ? static_cast<std::size_t>(ArgumentCount - 1) : 0;
    if (!Text.TryReserve(Count) || !Views.TryReserve(Count))
    {
        std::fputs("AssetImport: could not allocate command arguments.\n", stderr);
        return 1;
    }
    Array<char> Buffer;
    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        const int ByteCount = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, Arguments[Index], -1, nullptr, 0, nullptr, nullptr);
        if (ByteCount <= 0 || !Buffer.TryResize(static_cast<std::size_t>(ByteCount)) ||
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Arguments[Index], -1,
                Buffer.Data(), ByteCount, nullptr, nullptr) != ByteCount)
        {
            std::fputs("AssetImport: could not convert a command argument to UTF-8.\n", stderr);
            return 1;
        }
        String Argument;
        if (!Argument.TryAssign(StringView(Buffer.Data(), static_cast<std::size_t>(ByteCount - 1))) ||
            !Text.TryPushBack(std::move(Argument)) || !Views.TryPushBack(Text.Back().View()))
        {
            std::fputs("AssetImport: could not allocate a command argument.\n", stderr);
            return 1;
        }
    }
    return RunAssetImport(Views.AsSpan());
}
#else
int RunPosixAssetImport(const int ArgumentCount, char* const* Arguments) noexcept
{
    Array<StringView> Views;
    const std::size_t Count = ArgumentCount > 0 ? static_cast<std::size_t>(ArgumentCount - 1) : 0;
    if (!Views.TryReserve(Count))
    {
        std::fputs("AssetImport: could not allocate command arguments.\n", stderr);
        return 1;
    }
    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        if (!Views.TryEmplaceBack(Arguments[Index]))
        {
            std::fputs("AssetImport: could not allocate a command argument.\n", stderr);
            return 1;
        }
    }
    return RunAssetImport(Views.AsSpan());
}
#endif

} // namespace
} // namespace LE

#if PLATFORM_WINDOWS
int wmain(int ArgumentCount, wchar_t** Arguments)
{
    return LE::RunWindowsAssetImport(ArgumentCount, Arguments);
}
#else
int main(int ArgumentCount, char** Arguments)
{
    return LE::RunPosixAssetImport(ArgumentCount, Arguments);
}
#endif
