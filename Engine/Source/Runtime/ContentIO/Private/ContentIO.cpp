#include "ContentIO/ContentIO.h"
#include "Types/Uuid.h"

#include <algorithm>
#include <utility>

#if PLATFORM_WINDOWS
#include <Windows.h>
#else
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace LE
{
namespace
{

using EResult = EContentIOResult;
constexpr std::size_t MaxPathBytes = 32767;
constexpr std::size_t MaxTreeDepth = 64;
constexpr std::size_t MaxTreeEntries = 1000000;
constexpr std::size_t TransferSize = 1024 * 1024;

bool IsPathText(const StringView Path) noexcept
{
    if (Path.IsEmpty() || Path.Size() > MaxPathBytes)
    {
        return false;
    }
    for (std::size_t Offset = 0; Offset < Path.Size();)
    {
        const auto Lead = static_cast<uint8>(Path[Offset++]);
        if (Lead < 0x20 || Lead == 0x7f)
        {
            return false;
        }
        if (Lead < 0x80)
        {
            continue;
        }
        const std::size_t Tail = Lead < 0xe0 ? 1 : Lead < 0xf0 ? 2 : 3;
        if (Lead < 0xc2 || Lead > 0xf4 || Tail > Path.Size() - Offset)
        {
            return false;
        }
        const auto Next = static_cast<uint8>(Path[Offset]);
        if ((Lead == 0xe0 && Next < 0xa0) || (Lead == 0xed && Next >= 0xa0) ||
            (Lead == 0xf0 && Next < 0x90) || (Lead == 0xf4 && Next >= 0x90))
        {
            return false;
        }
        for (std::size_t Index = 0; Index < Tail; ++Index)
        {
            if ((static_cast<uint8>(Path[Offset++]) & 0xc0) != 0x80)
            {
                return false;
            }
        }
    }
    return true;
}

bool IsSeparator(const char Value) noexcept
{
#if PLATFORM_WINDOWS
    return Value == '/' || Value == '\\';
#else
    return Value == '/';
#endif
}

char FoldAscii(const char Value) noexcept
{
    return Value >= 'A' && Value <= 'Z' ? static_cast<char>(Value + ('a' - 'A')) : Value;
}

#if PLATFORM_WINDOWS
bool IsDriveLetter(const char Value) noexcept
{
    return FoldAscii(Value) >= 'a' && FoldAscii(Value) <= 'z';
}
#endif

bool IsAbsoluteRoot(const StringView Path) noexcept
{
#if PLATFORM_WINDOWS
    return (Path.Size() >= 3 && IsDriveLetter(Path[0]) && Path[1] == ':' && IsSeparator(Path[2])) ||
        (Path.Size() >= 3 && IsSeparator(Path[0]) && IsSeparator(Path[1]) &&
            Path[2] != '.' && Path[2] != '?');
#else
    return !Path.IsEmpty() && Path[0] == '/';
#endif
}

bool IsContainedSegment(const StringView Segment) noexcept
{
    if (Segment.IsEmpty() || Segment[Segment.Size() - 1] == '.' ||
        Segment[Segment.Size() - 1] == ' ')
    {
        return false;
    }
    for (std::size_t Index = 0; Index < Segment.Size(); ++Index)
    {
        const char Value = Segment[Index];
        if (Value == ':' || Value == '\\' || Value == '*' || Value == '?' ||
            Value == '"' || Value == '<' || Value == '>' || Value == '|')
        {
            return false;
        }
    }
    return true;
}

bool TryAppendSegment(String& Path, const StringView Segment) noexcept
{
    return (Path.IsEmpty() || IsSeparator(Path.View()[Path.Size() - 1]) || Path.TryAppend("/")) &&
        Path.TryAppend(Segment);
}

bool EndsWithExtension(const StringView Name, const StringView Extension) noexcept
{
    if (Name.Size() < Extension.Size())
    {
        return false;
    }
    const auto Start = Name.Size() - Extension.Size();
    for (std::size_t Index = 0; Index < Extension.Size(); ++Index)
    {
        if (FoldAscii(Name[Start + Index]) != FoldAscii(Extension[Index]))
        {
            return false;
        }
    }
    return true;
}

enum class EPathKind : uint8
{
    RegularFile,
    Directory,
    Other,
};

#if PLATFORM_WINDOWS

using FNativePath = Array<wchar_t>;

EResult ConvertPath(const StringView Path, FNativePath& Out) noexcept
{
    if (!IsPathText(Path) ||
        (Path.Size() > 1 && IsSeparator(Path[0]) && IsSeparator(Path[1]) && !IsAbsoluteRoot(Path)))
    {
        return EResult::InvalidPath;
    }
    for (std::size_t Index = 0; Index < Path.Size(); ++Index)
    {
        // Reject alternate data streams. An initial drive specifier is allowed.
        if (Path[Index] == ':' && !(Index == 1 && IsDriveLetter(Path[0])))
        {
            return EResult::InvalidPath;
        }
    }
    const auto Size = static_cast<int>(Path.Size());
    const int Required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Path.Data(), Size, nullptr, 0);
    if (Required <= 0)
    {
        return EResult::InvalidPath;
    }
    if (!Out.TryResize(static_cast<std::size_t>(Required) + 1))
    {
        return EResult::OutOfMemory;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Path.Data(), Size, Out.Data(), Required) != Required)
    {
        return EResult::InvalidPath;
    }
    // Win32 understands either slash in ordinary DOS paths.
    Out[Required] = L'\0';
    return EResult::Success;
}

EResult FromNativeName(const wchar_t* const Name, String& Out) noexcept
{
    const int Required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Name, -1,
        nullptr, 0, nullptr, nullptr);
    if (Required <= 0)
    {
        return EResult::InvalidPath;
    }
    Array<char> Bytes;
    if (!Bytes.TryResize(static_cast<std::size_t>(Required)))
    {
        return EResult::OutOfMemory;
    }
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Name, -1,
            Bytes.Data(), Required, nullptr, nullptr) != Required)
    {
        return EResult::InvalidPath;
    }
    return Out.TryAssign(StringView(Bytes.Data(), static_cast<std::size_t>(Required) - 1))
        ? EResult::Success : EResult::OutOfMemory;
}

bool IsMissing(const DWORD Error) noexcept
{
    return Error == ERROR_FILE_NOT_FOUND || Error == ERROR_PATH_NOT_FOUND;
}

EResult QueryPath(const FNativePath& Path, EPathKind& Kind) noexcept
{
    const DWORD Attributes = GetFileAttributesW(Path.Data());
    if (Attributes == INVALID_FILE_ATTRIBUTES)
    {
        return IsMissing(GetLastError()) ? EResult::NotFound : EResult::ReadFailed;
    }
    if (Attributes & FILE_ATTRIBUTE_REPARSE_POINT)
    {
        return EResult::RedirectedPath;
    }
    Kind = (Attributes & FILE_ATTRIBUTE_DIRECTORY) ? EPathKind::Directory : EPathKind::RegularFile;
    return EResult::Success;
}

class FFile final
{
public:
    FFile() = default;
    FFile(const FFile&) = delete;
    FFile& operator=(const FFile&) = delete;
    ~FFile() { static_cast<void>(Close()); }

    EResult OpenRead(const FNativePath& Path) noexcept
    {
        Handle = CreateFileW(Path.Data(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        return Handle != INVALID_HANDLE_VALUE ? EResult::Success :
            IsMissing(GetLastError()) ? EResult::NotFound : EResult::ReadFailed;
    }

    bool OpenNew(const FNativePath& Path) noexcept
    {
        Handle = CreateFileW(Path.Data(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        return Handle != INVALID_HANDLE_VALUE;
    }

    EResult Size(uint64& OutSize) const noexcept
    {
        BY_HANDLE_FILE_INFORMATION Info{};
        if (GetFileType(Handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(Handle, &Info))
        {
            return EResult::ReadFailed;
        }
        if (Info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        {
            return EResult::RedirectedPath;
        }
        if (Info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            return EResult::NotRegularFile;
        }
        OutSize = (static_cast<uint64>(Info.nFileSizeHigh) << 32) | Info.nFileSizeLow;
        return EResult::Success;
    }

    bool ReadAll(Span<uint8> Bytes) const noexcept
    {
        for (std::size_t Offset = 0; Offset < Bytes.Size();)
        {
            const auto Count = static_cast<DWORD>((std::min)(TransferSize, Bytes.Size() - Offset));
            DWORD Read = 0;
            if (!ReadFile(Handle, Bytes.Data() + Offset, Count, &Read, nullptr) || Read == 0)
            {
                return false;
            }
            Offset += Read;
        }
        return true;
    }

    bool WriteAll(Span<const uint8> Bytes) const noexcept
    {
        for (std::size_t Offset = 0; Offset < Bytes.Size();)
        {
            const auto Count = static_cast<DWORD>((std::min)(TransferSize, Bytes.Size() - Offset));
            DWORD Written = 0;
            if (!WriteFile(Handle, Bytes.Data() + Offset, Count, &Written, nullptr) || Written == 0)
            {
                return false;
            }
            Offset += Written;
        }
        return true;
    }

    bool Close() noexcept
    {
        return Handle == INVALID_HANDLE_VALUE ||
            CloseHandle(std::exchange(Handle, INVALID_HANDLE_VALUE)) != FALSE;
    }

private:
    HANDLE Handle = INVALID_HANDLE_VALUE;
};

bool Replace(const FNativePath& Temporary, const FNativePath& Destination) noexcept
{
    return MoveFileExW(Temporary.Data(), Destination.Data(), MOVEFILE_REPLACE_EXISTING) != FALSE;
}

void DeleteTemporary(const FNativePath& Path) noexcept
{
    static_cast<void>(DeleteFileW(Path.Data()));
}

#else

using FNativePath = String;

EResult ConvertPath(const StringView Path, FNativePath& Out) noexcept
{
    if (!IsPathText(Path))
    {
        return EResult::InvalidPath;
    }
    return Out.TryAssign(Path) ? EResult::Success : EResult::OutOfMemory;
}

EResult QueryPath(const FNativePath& Path, EPathKind& Kind) noexcept
{
    struct stat Info{};
    if (lstat(Path.Data(), &Info) != 0)
    {
        return errno == ENOENT || errno == ENOTDIR ? EResult::NotFound : EResult::ReadFailed;
    }
    if (S_ISLNK(Info.st_mode))
    {
        return EResult::RedirectedPath;
    }
    Kind = S_ISDIR(Info.st_mode) ? EPathKind::Directory :
        S_ISREG(Info.st_mode) ? EPathKind::RegularFile : EPathKind::Other;
    return EResult::Success;
}

class FFile final
{
public:
    FFile() = default;
    FFile(const FFile&) = delete;
    FFile& operator=(const FFile&) = delete;
    ~FFile() { static_cast<void>(Close()); }

    EResult OpenRead(const FNativePath& Path) noexcept
    {
        Descriptor = open(Path.Data(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        if (Descriptor >= 0)
        {
            return EResult::Success;
        }
        if (errno == ELOOP)
        {
            return EResult::RedirectedPath;
        }
        return errno == ENOENT || errno == ENOTDIR ? EResult::NotFound : EResult::ReadFailed;
    }

    bool OpenNew(const FNativePath& Path) noexcept
    {
        Descriptor = open(Path.Data(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
        return Descriptor >= 0;
    }

    EResult Size(uint64& OutSize) const noexcept
    {
        struct stat Info{};
        if (fstat(Descriptor, &Info) != 0 || Info.st_size < 0)
        {
            return EResult::ReadFailed;
        }
        if (!S_ISREG(Info.st_mode))
        {
            return EResult::NotRegularFile;
        }
        OutSize = static_cast<uint64>(Info.st_size);
        return EResult::Success;
    }

    bool ReadAll(Span<uint8> Bytes) const noexcept
    {
        for (std::size_t Offset = 0; Offset < Bytes.Size();)
        {
            const auto Count = (std::min)(TransferSize, Bytes.Size() - Offset);
            const auto Read = read(Descriptor, Bytes.Data() + Offset, Count);
            if (Read < 0 && errno == EINTR)
            {
                continue;
            }
            if (Read <= 0)
            {
                return false;
            }
            Offset += static_cast<std::size_t>(Read);
        }
        return true;
    }

    bool WriteAll(Span<const uint8> Bytes) const noexcept
    {
        for (std::size_t Offset = 0; Offset < Bytes.Size();)
        {
            const auto Count = (std::min)(TransferSize, Bytes.Size() - Offset);
            const auto Written = write(Descriptor, Bytes.Data() + Offset, Count);
            if (Written < 0 && errno == EINTR)
            {
                continue;
            }
            if (Written <= 0)
            {
                return false;
            }
            Offset += static_cast<std::size_t>(Written);
        }
        return true;
    }

    bool Close() noexcept
    {
        return Descriptor < 0 || close(std::exchange(Descriptor, -1)) == 0;
    }

private:
    int Descriptor = -1;
};

bool Replace(const FNativePath& Temporary, const FNativePath& Destination) noexcept
{
    return std::rename(Temporary.Data(), Destination.Data()) == 0;
}

void DeleteTemporary(const FNativePath& Path) noexcept
{
    static_cast<void>(unlink(Path.Data()));
}

#endif

EResult QueryPath(const StringView Path, EPathKind& Kind) noexcept
{
    FNativePath Native;
    const EResult Converted = ConvertPath(Path, Native);
    return Converted == EResult::Success ? QueryPath(Native, Kind) : Converted;
}

EResult RequireRoot(StringView& Path) noexcept
{
    if (!IsPathText(Path) || !IsAbsoluteRoot(Path))
    {
        return EResult::InvalidPath;
    }
#if PLATFORM_WINDOWS
    constexpr std::size_t MinRootSize = 3;
#else
    constexpr std::size_t MinRootSize = 1;
#endif
    while (Path.Size() > MinRootSize && IsSeparator(Path[Path.Size() - 1]))
    {
        Path = Path.Substr(0, Path.Size() - 1);
    }
    for (std::size_t Start = 0, End = 0; End <= Path.Size(); ++End)
    {
        if (End == Path.Size() || IsSeparator(Path[End]))
        {
            const StringView Segment = Path.Substr(Start, End - Start);
            if (Segment == StringView(".") || Segment == StringView(".."))
            {
                return EResult::InvalidPath;
            }
            Start = End + 1;
        }
    }
    EPathKind Kind{};
    const EResult Result = QueryPath(Path, Kind);
    return Result == EResult::Success && Kind != EPathKind::Directory ? EResult::InvalidPath : Result;
}

EResult ReadFileBytes(const StringView Path, const std::size_t MaxBytes,
    const bool bPrefix, Array<uint8>& OutBytes, uint64& OutFileSize) noexcept
{
    FNativePath Native;
    if (const EResult Result = ConvertPath(Path, Native); Result != EResult::Success)
    {
        return Result;
    }
    EPathKind Kind{};
    if (const EResult Result = QueryPath(Native, Kind); Result != EResult::Success)
    {
        return Result;
    }
    if (Kind != EPathKind::RegularFile)
    {
        return EResult::NotRegularFile;
    }

    FFile File;
    if (const EResult Result = File.OpenRead(Native); Result != EResult::Success)
    {
        return Result;
    }
    uint64 Size = 0;
    if (const EResult Result = File.Size(Size); Result != EResult::Success)
    {
        return Result;
    }
    if (!bPrefix && Size > MaxBytes)
    {
        return EResult::LimitExceeded;
    }
    const auto ReadSize = static_cast<std::size_t>((std::min)(Size, static_cast<uint64>(MaxBytes)));
    Array<uint8> Candidate(OutBytes.GetAllocator());
    if (!Candidate.TryResize(ReadSize))
    {
        return EResult::OutOfMemory;
    }
    uint64 FinalSize = 0;
    if (!File.ReadAll(Candidate.AsSpan()) ||
        File.Size(FinalSize) != EResult::Success || Size != FinalSize || !File.Close())
    {
        return EResult::ReadFailed;
    }
    OutBytes = std::move(Candidate);
    OutFileSize = Size;
    return EResult::Success;
}

EResult MakeTemporaryPath(const StringView Destination, String& OutPath) noexcept
{
    std::size_t ParentSize = Destination.Size();
    while (ParentSize != 0 && !IsSeparator(Destination[ParentSize - 1]))
    {
        --ParentSize;
    }
#if PLATFORM_WINDOWS
    // C:foo is relative to C's current directory. Keep the drive prefix.
    if (ParentSize == 0 && Destination.Size() > 2 && Destination[1] == ':')
    {
        ParentSize = 2;
    }
#endif
    if (ParentSize == Destination.Size())
    {
        return EResult::InvalidPath;
    }
    FUuid Id;
    if (!FUuid::TryGenerate(Id))
    {
        return EResult::WriteFailed;
    }
    constexpr char Hex[] = "0123456789abcdef";
    char Suffix[FUuid::ByteCount * 2];
    std::size_t Offset = 0;
    for (const uint8 Byte : Id.GetBytes())
    {
        Suffix[Offset++] = Hex[Byte >> 4];
        Suffix[Offset++] = Hex[Byte & 0xf];
    }
    return OutPath.TryAssign(Destination.Substr(0, ParentSize)) &&
        OutPath.TryAppend(".le-tmp-") && OutPath.TryAppend(StringView(Suffix, sizeof(Suffix)))
        ? EResult::Success : EResult::OutOfMemory;
}

struct FFileEnumeration
{
    StringView Root;
    StringView Extension;
    std::size_t MaxFiles;
    Array<String>& Paths;
    String* FailureRelativePath;
    std::size_t VisitedEntries = 0;

    EResult VisitDirectory(StringView Relative, std::size_t Depth) noexcept;

    EResult Fail(const EResult Result, const StringView Relative) noexcept
    {
        if (FailureRelativePath != nullptr)
        {
            // Preserve the underlying error if diagnostic allocation fails.
            static_cast<void>(FailureRelativePath->TryAssign(Relative));
        }
        return Result;
    }

    EResult VisitEntry(const StringView Directory, const StringView Name,
        const std::size_t Depth) noexcept
    {
        if (Name == StringView(".") || Name == StringView(".."))
        {
            return EResult::Success;
        }
        if (!IsPathText(Name))
        {
            return Fail(EResult::InvalidPath, Directory);
        }
        String Relative(Paths.GetAllocator());
        if (!Relative.TryAssign(Directory) || !TryAppendSegment(Relative, Name))
        {
            return Fail(EResult::OutOfMemory, Directory);
        }
        if (++VisitedEntries > MaxTreeEntries)
        {
            return Fail(EResult::LimitExceeded, Relative.View());
        }
        if (!IsContainedSegment(Name))
        {
            return Fail(EResult::InvalidPath, Relative.View());
        }
        String FullPath;
        if (!FullPath.TryAssign(Root) || !TryAppendSegment(FullPath, Relative.View()))
        {
            return Fail(EResult::OutOfMemory, Relative.View());
        }
        EPathKind Kind{};
        const EResult Status = QueryPath(FullPath.View(), Kind);
        if (Status != EResult::Success)
        {
            return Fail(Status, Relative.View());
        }
        if (Kind == EPathKind::Directory)
        {
            return VisitDirectory(Relative.View(), Depth + 1);
        }
        if (Kind != EPathKind::RegularFile || !EndsWithExtension(Name, Extension))
        {
            return EResult::Success;
        }
        if (Paths.Size() >= MaxFiles)
        {
            return Fail(EResult::LimitExceeded, Relative.View());
        }
        return Paths.TryPushBack(std::move(Relative))
            ? EResult::Success : Fail(EResult::OutOfMemory, Relative.View());
    }
};

EResult FFileEnumeration::VisitDirectory(
    const StringView Relative, const std::size_t Depth) noexcept
{
    if (Depth > MaxTreeDepth)
    {
        return Fail(EResult::LimitExceeded, Relative);
    }
    String Directory;
    if (!Directory.TryAssign(Root) ||
        (!Relative.IsEmpty() && !TryAppendSegment(Directory, Relative)))
    {
        return Fail(EResult::OutOfMemory, Relative);
    }

#if PLATFORM_WINDOWS
    if (!TryAppendSegment(Directory, "*"))
    {
        return Fail(EResult::OutOfMemory, Relative);
    }
    FNativePath Native;
    if (const EResult Result = ConvertPath(Directory.View(), Native); Result != EResult::Success)
    {
        return Fail(Result, Relative);
    }
    WIN32_FIND_DATAW Data{};
    const HANDLE Find = FindFirstFileW(Native.Data(), &Data);
    if (Find == INVALID_HANDLE_VALUE)
    {
        return GetLastError() == ERROR_FILE_NOT_FOUND
            ? EResult::Success : Fail(EResult::EnumerationFailed, Relative);
    }
    EResult Status = EResult::Success;
    do
    {
        String Name;
        Status = FromNativeName(Data.cFileName, Name);
        if (Status != EResult::Success)
        {
            static_cast<void>(Fail(Status, Relative));
            break;
        }
        Status = VisitEntry(Relative, Name.View(), Depth);
    } while (Status == EResult::Success && FindNextFileW(Find, &Data));
    if (Status == EResult::Success && GetLastError() != ERROR_NO_MORE_FILES)
    {
        Status = Fail(EResult::EnumerationFailed, Relative);
    }
    FindClose(Find);
    return Status;
#else
    DIR* const Handle = opendir(Directory.Data());
    if (Handle == nullptr)
    {
        return Fail(EResult::EnumerationFailed, Relative);
    }
    EResult Status = EResult::Success;
    while (Status == EResult::Success)
    {
        errno = 0;
        const dirent* const Entry = readdir(Handle);
        if (Entry == nullptr)
        {
            if (errno != 0)
            {
                Status = Fail(EResult::EnumerationFailed, Relative);
            }
            break;
        }
        Status = VisitEntry(Relative, StringView(Entry->d_name), Depth);
    }
    closedir(Handle);
    return Status;
#endif
}

} // namespace

EContentIOResult ReadContentFile(const StringView Path, const std::size_t MaxBytes,
    Array<uint8>& OutBytes) noexcept
{
    uint64 FileSize = 0;
    return ReadFileBytes(Path, MaxBytes, false, OutBytes, FileSize);
}

EContentIOResult ReadContentFilePrefix(const StringView Path, const std::size_t MaxPrefixBytes,
    Array<uint8>& OutBytes, uint64& OutFileSize) noexcept
{
    return ReadFileBytes(Path, MaxPrefixBytes, true, OutBytes, OutFileSize);
}

EContentIOResult WriteContentFileAtomically(
    const StringView Path, const Span<const uint8> Bytes) noexcept
{
    FNativePath Destination;
    if (const EResult Result = ConvertPath(Path, Destination); Result != EResult::Success)
    {
        return Result;
    }
    EPathKind Kind{};
    const EResult Existing = QueryPath(Destination, Kind);
    if (Existing == EResult::Success && Kind != EPathKind::RegularFile)
    {
        return EResult::NotRegularFile;
    }
    if (Existing != EResult::Success && Existing != EResult::NotFound)
    {
        return Existing;
    }

    String TemporaryPath;
    if (const EResult Result = MakeTemporaryPath(Path, TemporaryPath); Result != EResult::Success)
    {
        return Result;
    }
    FNativePath Temporary;
    if (const EResult Result = ConvertPath(TemporaryPath.View(), Temporary); Result != EResult::Success)
    {
        return Result;
    }
    FFile File;
    if (!File.OpenNew(Temporary))
    {
        return EResult::WriteFailed;
    }
    const bool bWritten = File.WriteAll(Bytes);
    const bool bClosed = File.Close();
    if (!bWritten || !bClosed)
    {
        DeleteTemporary(Temporary);
        return EResult::WriteFailed;
    }
    if (!Replace(Temporary, Destination))
    {
        DeleteTemporary(Temporary);
        return EResult::ReplaceFailed;
    }
    return EResult::Success;
}

EContentIOResult ResolveContentFile(StringView AbsoluteRoot, const StringView RelativePath,
    String& OutPath) noexcept
{
    if (!IsPathText(RelativePath))
    {
        return EResult::InvalidPath;
    }
    if (const EResult Root = RequireRoot(AbsoluteRoot); Root != EResult::Success)
    {
        return Root;
    }
    // Validate all segments before probing the tree, even if a prefix is absent.
    for (std::size_t Start = 0, End = 0; End <= RelativePath.Size(); ++End)
    {
        if (End == RelativePath.Size() || IsSeparator(RelativePath[End]))
        {
            if (!IsContainedSegment(RelativePath.Substr(Start, End - Start)))
            {
                return EResult::OutsideRoot;
            }
            Start = End + 1;
        }
    }
    String Candidate(OutPath.GetAllocator());
    if (!Candidate.TryAssign(AbsoluteRoot))
    {
        return EResult::OutOfMemory;
    }
    for (std::size_t Start = 0, End = 0; End <= RelativePath.Size(); ++End)
    {
        if (End != RelativePath.Size() && !IsSeparator(RelativePath[End]))
        {
            continue;
        }
        if (!TryAppendSegment(Candidate, RelativePath.Substr(Start, End - Start)))
        {
            return EResult::OutOfMemory;
        }
        Start = End + 1;
        EPathKind Kind{};
        if (const EResult Result = QueryPath(Candidate.View(), Kind); Result != EResult::Success)
        {
            return Result;
        }
        const EPathKind Expected = End == RelativePath.Size() ? EPathKind::RegularFile : EPathKind::Directory;
        if (Kind != Expected)
        {
            return EResult::NotRegularFile;
        }
    }
    OutPath = std::move(Candidate);
    return EResult::Success;
}

EContentIOResult EnumerateContentFiles(StringView AbsoluteRoot, const StringView Extension,
    const std::size_t MaxFiles, Array<String>& OutRelativePaths,
    String* const OutFailureRelativePath) noexcept
{
    if (!Extension.IsEmpty() && (Extension[0] != '.' || !IsPathText(Extension)))
    {
        if (OutFailureRelativePath != nullptr)
        {
            OutFailureRelativePath->Clear();
        }
        return EResult::InvalidPath;
    }
    if (const EResult Root = RequireRoot(AbsoluteRoot); Root != EResult::Success)
    {
        if (OutFailureRelativePath != nullptr)
        {
            OutFailureRelativePath->Clear();
        }
        return Root;
    }
    Array<String> Candidate(OutRelativePaths.GetAllocator());
    String Failure(OutFailureRelativePath != nullptr
        ? OutFailureRelativePath->GetAllocator() : GetDefaultAllocator());
    FFileEnumeration Enumeration{AbsoluteRoot, Extension, MaxFiles, Candidate,
        OutFailureRelativePath != nullptr ? &Failure : nullptr};
    const EResult Result = Enumeration.VisitDirectory({}, 0);
    if (OutFailureRelativePath != nullptr)
    {
        *OutFailureRelativePath = std::move(Failure);
    }
    if (Result != EResult::Success)
    {
        return Result;
    }
    if (Candidate.Size() > 1)
    {
        std::sort(Candidate.Data(), Candidate.Data() + Candidate.Size(),
            [](const String& Left, const String& Right) { return Left.View() < Right.View(); });
    }
    OutRelativePaths = std::move(Candidate);
    return EResult::Success;
}

const char* DescribeContentIOResult(const EContentIOResult Result) noexcept
{
    switch (Result)
    {
    case EContentIOResult::Success: return "success";
    case EContentIOResult::InvalidPath: return "invalid UTF-8 file or mount path";
    case EContentIOResult::OutsideRoot: return "path is not relative to the mounted root";
    case EContentIOResult::RedirectedPath: return "path contains a symbolic link or reparse point";
    case EContentIOResult::NotFound: return "file or directory was not found";
    case EContentIOResult::NotRegularFile: return "path does not name the expected file or directory";
    case EContentIOResult::LimitExceeded: return "file size or enumeration exceeds the limit";
    case EContentIOResult::OutOfMemory: return "allocation failed";
    case EContentIOResult::ReadFailed: return "could not read file";
    case EContentIOResult::WriteFailed: return "could not write a sibling temporary file";
    case EContentIOResult::ReplaceFailed: return "could not replace the destination file";
    case EContentIOResult::EnumerationFailed: return "could not enumerate the mounted directory";
    }
    return "unknown content I/O failure";
}

} // namespace LE
