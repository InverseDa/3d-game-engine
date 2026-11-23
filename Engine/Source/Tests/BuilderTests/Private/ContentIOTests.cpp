#include "ContentIO/ContentIO.h"
#include "Types/Uuid.h"

#include <cstdio>

#if PLATFORM_WINDOWS
#include <Windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace LE
{
namespace
{

using EResult = EContentIOResult;

bool ExpectContentIO(const bool bCondition, const char* Label)
{
    if (!bCondition)
    {
        std::fprintf(stderr, "FAILED: ContentIO: %s\n", Label);
    }
    return bCondition;
}

#if PLATFORM_WINDOWS

Array<wchar_t> ToWidePath(const String& Path)
{
    const int Length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Path.Data(), -1, nullptr, 0);
    Array<wchar_t> Buffer;
    if (Length > 0)
    {
        Buffer.Resize(static_cast<std::size_t>(Length));
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Path.Data(), -1, Buffer.Data(), Length);
    }
    return Buffer;
}

String CurrentDirectory()
{
    wchar_t Directory[MAX_PATH]{};
    const DWORD Length = GetCurrentDirectoryW(MAX_PATH, Directory);
    if (Length == 0 || Length >= MAX_PATH)
    {
        return {};
    }
    char Bytes[MAX_PATH * 4]{};
    const int Count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Directory, -1, Bytes, sizeof(Bytes), nullptr, nullptr);
    return Count > 0 ? String(Bytes) : String{};
}

bool CreateDirectoryForTest(const String& Path)
{
    const Array<wchar_t> Wide = ToWidePath(Path);
    return !Wide.IsEmpty() && CreateDirectoryW(Wide.Data(), nullptr) != FALSE;
}

void RemoveDirectoryForTest(const String& Path)
{
    const Array<wchar_t> Wide = ToWidePath(Path);
    if (!Wide.IsEmpty())
    {
        RemoveDirectoryW(Wide.Data());
    }
}

void RemoveFileForTest(const String& Path)
{
    const Array<wchar_t> Wide = ToWidePath(Path);
    if (!Wide.IsEmpty())
    {
        DeleteFileW(Wide.Data());
    }
}

bool ChangeDirectory(const String& Path)
{
    const Array<wchar_t> Wide = ToWidePath(Path);
    return !Wide.IsEmpty() && SetCurrentDirectoryW(Wide.Data()) != FALSE;
}

bool MakeDirectoryLink(const String& Link, const String& Target)
{
    const Array<wchar_t> WideLink = ToWidePath(Link);
    const Array<wchar_t> WideTarget = ToWidePath(Target);
    return !WideLink.IsEmpty() && !WideTarget.IsEmpty() && CreateSymbolicLinkW(WideLink.Data(), WideTarget.Data(), SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != FALSE;
}

#else

String CurrentDirectory()
{
    char Directory[4096]{};
    return getcwd(Directory, sizeof(Directory)) != nullptr ? String(Directory) : String{};
}

bool CreateDirectoryForTest(const String& Path)
{
    return mkdir(Path.Data(), 0700) == 0;
}
void RemoveDirectoryForTest(const String& Path)
{
    rmdir(Path.Data());
}
void RemoveFileForTest(const String& Path)
{
    unlink(Path.Data());
}
bool ChangeDirectory(const String& Path)
{
    return chdir(Path.Data()) == 0;
}
bool MakeDirectoryLink(const String& Link, const String& Target)
{
    return symlink(Target.Data(), Link.Data()) == 0;
}

#endif

String JoinForTest(const String& Parent, const StringView Child)
{
    String Result(Parent);
    Result.Append("/");
    Result.Append(Child);
    return Result;
}

struct FContentDirectoryFixture
{
    String OriginalDirectory = CurrentDirectory();
    String Root;
    String Child;
    String UnicodeFile;
    String Link;
    String IgnoredFile;
    String RedirectedRoot;

    bool Create()
    {
        FUuid Id;
        if (OriginalDirectory.IsEmpty() || !FUuid::TryGenerate(Id))
        {
            return false;
        }
        const String IdText = Id.ToString();
        Root = JoinForTest(OriginalDirectory, IdText.View());
        Child = JoinForTest(Root, "models");
        UnicodeFile = JoinForTest(Child, u8"模型.LEASSET");
        IgnoredFile = JoinForTest(Root, "ignored.txt");
        Link = JoinForTest(Root, "link");
        RedirectedRoot = JoinForTest(OriginalDirectory, "redirect-");
        RedirectedRoot.Append(IdText.View());
        return CreateDirectoryForTest(Root) && CreateDirectoryForTest(Child);
    }

    ~FContentDirectoryFixture()
    {
        if (!OriginalDirectory.IsEmpty())
        {
            static_cast<void>(ChangeDirectory(OriginalDirectory));
        }
        if (!Root.IsEmpty())
        {
#if PLATFORM_WINDOWS
            RemoveDirectoryForTest(Link);
            RemoveDirectoryForTest(RedirectedRoot);
#else
            RemoveFileForTest(Link);
            RemoveFileForTest(RedirectedRoot);
#endif
            RemoveFileForTest(UnicodeFile);
            RemoveFileForTest(IgnoredFile);
            RemoveDirectoryForTest(Child);
            RemoveDirectoryForTest(Root);
        }
    }
};

bool TestFailedReplacement(const String& Path)
{
#if PLATFORM_WINDOWS
    const Array<wchar_t> Wide = ToWidePath(Path);
    const HANDLE Blocker = CreateFileW(Wide.Data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (Blocker == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    constexpr uint8 Other[] = {7};
    const EResult Result = WriteContentFileAtomically(Path.View(), {Other, sizeof(Other)});
    CloseHandle(Blocker);
    return Result == EResult::ReplaceFailed;
#else
    // An open destination can be replaced on POSIX. A target with a missing
    // parent exercises write failure without touching the existing sibling.
    const String Missing = JoinForTest(Path, "missing");
    constexpr uint8 Other[] = {7};
    return WriteContentFileAtomically(Missing.View(), {Other, sizeof(Other)}) == EResult::WriteFailed;
#endif
}

} // namespace

bool RunContentIOTests()
{
    FContentDirectoryFixture Fixture;
    if (!ExpectContentIO(Fixture.Create(), "create test directory"))
    {
        return false;
    }
    bool bPassed = true;
    constexpr uint8 Initial[] = {1, 2, 3, 4};
    constexpr uint8 Updated[] = {5, 6};
    bPassed &= ExpectContentIO(WriteContentFileAtomically(Fixture.UnicodeFile.View(), {Initial, sizeof(Initial)}) == EResult::Success, "write Unicode path");
    bPassed &= ExpectContentIO(WriteContentFileAtomically(Fixture.IgnoredFile.View(), {}) == EResult::Success, "write empty file");
    Array<uint8> Bytes;
    uint64 FileSize = 0;
    bPassed &= ExpectContentIO(ReadContentFilePrefix(Fixture.UnicodeFile.View(), 2, Bytes, FileSize) == EResult::Success && FileSize == 4 && Bytes.Size() == 2 && Bytes[1] == 2, "bounded prefix");
    bPassed &= ExpectContentIO(ReadContentFile(Fixture.UnicodeFile.View(), 3, Bytes) == EResult::LimitExceeded && Bytes.Size() == 2 && Bytes[1] == 2, "whole read limit preserves bytes");
    bPassed &= ExpectContentIO(ReadContentFile(Fixture.Child.View(), 4, Bytes) == EResult::NotRegularFile && Bytes.Size() == 2, "reject directory reads");
    bPassed &= ExpectContentIO(ReadContentFile(StringView("bad\0path", 8), 4, Bytes) == EResult::InvalidPath, "reject embedded NUL");
    bPassed &= ExpectContentIO(ReadContentFile(StringView("\xc0\xaf", 2), 4, Bytes) == EResult::InvalidPath, "reject overlong UTF-8");

    String Resolved("unchanged");
    bPassed &= ExpectContentIO(ResolveContentFile(Fixture.Root.View(), "models/../ignored.txt", Resolved) == EResult::OutsideRoot && Resolved.View() == StringView("unchanged"), "reject parent traversal");
    bPassed &= ExpectContentIO(ResolveContentFile(Fixture.Root.View(), Fixture.UnicodeFile.View(), Resolved) == EResult::OutsideRoot && Resolved.View() == StringView("unchanged"), "reject absolute second operand");
    bPassed &= ExpectContentIO(ChangeDirectory(Fixture.Child), "change current directory");
    bPassed &= ExpectContentIO(ResolveContentFile(Fixture.Root.View(), u8"models/模型.LEASSET", Resolved) == EResult::Success && Resolved.View() == Fixture.UnicodeFile.View(), "resolve absolute mount from another cwd");
    bPassed &= ExpectContentIO(ReadContentFile(u8"模型.LEASSET", 4, Bytes) == EResult::Success && Bytes.Size() == 4 && Bytes[3] == 4, "standalone relative read");
    bPassed &= ExpectContentIO(ReadContentFile("missing.bin", 4, Bytes) == EResult::NotFound && Bytes.Size() == 4, "missing file leaves old bytes");
    bPassed &= ExpectContentIO(ChangeDirectory(Fixture.OriginalDirectory), "restore current directory");

    Array<String> RelativePaths;
    bPassed &= ExpectContentIO(EnumerateContentFiles(Fixture.Root.View(), ".leasset", 1, RelativePaths) == EResult::Success && RelativePaths.Size() == 1 && RelativePaths[0].View() == StringView(u8"models/模型.LEASSET"), "case-insensitive extension and relative UTF-8 result");
    String FailurePath;
    bPassed &= ExpectContentIO(EnumerateContentFiles(Fixture.Root.View(), ".leasset", 0, RelativePaths, &FailurePath) == EResult::LimitExceeded && RelativePaths.Size() == 1 && FailurePath.View() == StringView(u8"models/模型.LEASSET"), "nested enumeration failure identifies the entry and preserves prior output");
    bPassed &= ExpectContentIO(EnumerateContentFiles(Fixture.Root.View(), ".leasset", 1, RelativePaths, &FailurePath) == EResult::Success && FailurePath.IsEmpty(), "successful enumeration clears diagnostic");

    const bool bMadeLink = MakeDirectoryLink(Fixture.Link, Fixture.OriginalDirectory);
#if !PLATFORM_WINDOWS
    bPassed &= ExpectContentIO(bMadeLink, "create outside directory symlink");
#endif
    if (bMadeLink)
    {
        bPassed &= ExpectContentIO(ResolveContentFile(Fixture.Root.View(), "link/missing.leasset", Resolved) == EResult::RedirectedPath, "reject redirected intermediate segment");
        bPassed &= ExpectContentIO(EnumerateContentFiles(Fixture.Root.View(), ".leasset", 10, RelativePaths, &FailurePath) == EResult::RedirectedPath && RelativePaths.Size() == 1 && FailurePath.View() == StringView("link"), "identify redirected directory in enumeration");
    }
    else
    {
        std::fprintf(stderr, "SKIPPED: ContentIO directory symlink creation unavailable\n");
    }
    if (MakeDirectoryLink(Fixture.RedirectedRoot, Fixture.Root))
    {
        String RootWithSeparator = JoinForTest(Fixture.RedirectedRoot, "");
        bPassed &= ExpectContentIO(ResolveContentFile(RootWithSeparator.View(), u8"models/模型.LEASSET", Resolved) == EResult::RedirectedPath, "reject redirected mount root with trailing separator");
    }

    bPassed &= ExpectContentIO(WriteContentFileAtomically(Fixture.UnicodeFile.View(), {Updated, sizeof(Updated)}) == EResult::Success, "atomically overwrite existing file");
    bPassed &= ExpectContentIO(TestFailedReplacement(Fixture.UnicodeFile), "failure to publish is reported");
    bPassed &= ExpectContentIO(ReadContentFile(Fixture.UnicodeFile.View(), 4, Bytes) == EResult::Success && Bytes.Size() == 2 && Bytes[0] == 5 && Bytes[1] == 6, "failure leaves old file intact");
    return bPassed;
}

} // namespace LE
