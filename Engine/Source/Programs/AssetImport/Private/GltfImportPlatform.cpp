#include "GltfImportPlatform.h"

#include "GltfSource.h"

#include "FileSystem/FileSystem.h"
#include "Types/Uuid.h"

#include <cstdio>

#if PLATFORM_WINDOWS
#include <Windows.h>
#include <bcrypt.h>
#include <fcntl.h>
#include <io.h>
#elif PLATFORM_MAC
#include <CommonCrypto/CommonDigest.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace LE::Detail
{
namespace
{

constexpr StringView PointerPrefix{"{\"generation\":\"", 15};
constexpr StringView PointerSuffix{"\"}\n", 3};
constexpr std::size_t PointerSize = PointerPrefix.Size() + FUuid::CanonicalStringLength + PointerSuffix.Size();

bool Report(FGltfDiagnostic& Diagnostic, const char* Code, const char* Message, StringView File) noexcept
{
    Diagnostic.Code = Code;
    Diagnostic.Message = Message;
    (void)Diagnostic.File.TryAssign(File);
    return false;
}

bool IsGeneration(StringView Value) noexcept
{
    FUuid Id;
    return FUuid::TryParse(Value, Id) && Id.IsValid();
}

bool JoinPath(String& Path, StringView Root, StringView File) noexcept
{
    return Path.TryAssign(Root) && Path.TryAppend("/") && Path.TryAppend(File);
}

// Native handle lifetime owns the lock. The lock file stays in place so all
// publishers of this root use the same file, including after an interrupted run.
class FPublicationLock
{
public:
    FPublicationLock() noexcept = default;
    FPublicationLock(const FPublicationLock&) = delete;
    FPublicationLock& operator=(const FPublicationLock&) = delete;
    ~FPublicationLock()
    {
#if PLATFORM_WINDOWS
        if (Handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(Handle);
        }
#elif PLATFORM_MAC
        if (Descriptor >= 0)
        {
            close(Descriptor);
        }
#endif
    }

    bool Acquire(const String& Path) noexcept
    {
#if PLATFORM_WINDOWS
        Array<wchar_t> Wide;
        const int Length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Path.Data(), -1, nullptr, 0);
        if (Length <= 0 || !Wide.TryResize(static_cast<std::size_t>(Length)) || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Path.Data(), -1, Wide.Data(), Length) != Length)
        {
            return false;
        }
        for (unsigned Attempt = 0; Attempt < 250; ++Attempt)
        {
            Handle = CreateFileW(Wide.Data(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (Handle != INVALID_HANDLE_VALUE)
            {
                FILE_ATTRIBUTE_TAG_INFO Information{};
                return GetFileInformationByHandleEx(Handle, FileAttributeTagInfo, &Information, sizeof(Information)) &&
                       (Information.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) == 0;
            }
            if (GetLastError() != ERROR_SHARING_VIOLATION)
            {
                return false;
            }
            Sleep(20);
        }
        return false;
#elif PLATFORM_MAC
        Descriptor = open(Path.Data(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (Descriptor < 0)
        {
            return false;
        }
        int Result;
        do
        {
            Result = flock(Descriptor, LOCK_EX);
        } while (Result != 0 && errno == EINTR);
        return Result == 0;
#else
        (void)Path;
        return false;
#endif
    }

private:
#if PLATFORM_WINDOWS
    HANDLE Handle = INVALID_HANDLE_VALUE;
#elif PLATFORM_MAC
    int Descriptor = -1;
#endif
};

bool ReadGeneration(StringView OutputRoot, Array<uint8>& Pointer, StringView& Current, FGltfDiagnostic& Diagnostic) noexcept
{
    String Path;
    const EFileSystemResult Resolved = ResolveContentFile(OutputRoot, "current.json", Path);
    if (Resolved == EFileSystemResult::NotFound)
    {
        Current = "-";
        return true;
    }
    if (Resolved != EFileSystemResult::Success)
    {
        return Report(Diagnostic, "publish_read_failed", DescribeFileSystemResult(Resolved), OutputRoot);
    }
    const EFileSystemResult Read = ReadContentFile(Path.View(), PointerSize, Pointer);
    if (Read != EFileSystemResult::Success)
    {
        return Report(Diagnostic, "publish_read_failed", DescribeFileSystemResult(Read), Path.View());
    }
    const StringView Text(reinterpret_cast<const char*>(Pointer.Data()), Pointer.Size());
    if (Text.Size() != PointerSize || Text.Substr(0, PointerPrefix.Size()) != PointerPrefix ||
        Text.Substr(PointerPrefix.Size() + FUuid::CanonicalStringLength, PointerSuffix.Size()) != PointerSuffix)
    {
        return Report(Diagnostic, "invalid_current_pointer", "Expected the canonical generation pointer.", Path.View());
    }
    Current = Text.Substr(PointerPrefix.Size(), FUuid::CanonicalStringLength);
    return IsGeneration(Current) || Report(Diagnostic, "invalid_current_pointer", "The published generation is not a canonical UUID.", Path.View());
}

} // namespace

bool HashDocument(Span<const uint8> Bytes, char (&Hex)[DocumentDigestHexLength]) noexcept
{
    uint8 Digest[DocumentDigestHexLength / 2]{};
#if PLATFORM_WINDOWS
    BCRYPT_ALG_HANDLE Algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&Algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
    {
        return false;
    }
    const NTSTATUS Result = BCryptHash(Algorithm, nullptr, 0, const_cast<uint8*>(Bytes.Data()), static_cast<ULONG>(Bytes.Size()), Digest, static_cast<ULONG>(sizeof(Digest)));
    BCryptCloseAlgorithmProvider(Algorithm, 0);
    if (Result < 0)
    {
        return false;
    }
#elif PLATFORM_MAC
    if (CC_SHA256(Bytes.Data(), static_cast<CC_LONG>(Bytes.Size()), Digest) == nullptr)
    {
        return false;
    }
#else
    (void)Bytes;
    return false;
#endif
    constexpr char Digits[] = "0123456789abcdef";
    for (std::size_t Index = 0; Index < sizeof(Digest); ++Index)
    {
        Hex[Index * 2] = Digits[Digest[Index] >> 4];
        Hex[Index * 2 + 1] = Digits[Digest[Index] & 0x0f];
    }
    return true;
}

bool SetBinaryInput() noexcept
{
#if PLATFORM_WINDOWS
    return _setmode(_fileno(stdin), _O_BINARY) >= 0;
#else
    return true;
#endif
}

bool PublishGeneration(StringView OutputRoot, StringView Baseline, StringView Candidate, FGltfDiagnostic& Diagnostic) noexcept
{
    if (!IsGeneration(Candidate) || (Baseline != StringView("-") && !IsGeneration(Baseline)) || Candidate == Baseline)
    {
        return Report(Diagnostic, "invalid_generation", "Expected distinct canonical generation IDs or '-' for the first publication.", OutputRoot);
    }
    String CandidateScene;
    String ResolvedScene;
    if (!CandidateScene.TryAssign("generations/") || !CandidateScene.TryAppend(Candidate) || !CandidateScene.TryAppend("/Scene.json"))
    {
        return Report(Diagnostic, "out_of_memory", "Could not build the candidate path.", OutputRoot);
    }
    const EFileSystemResult Scene = ResolveContentFile(OutputRoot, CandidateScene.View(), ResolvedScene);
    if (Scene != EFileSystemResult::Success)
    {
        return Report(Diagnostic, "invalid_publish_candidate", DescribeFileSystemResult(Scene), CandidateScene.View());
    }
    String LockPath;
    String Path;
    String Text;
    if (!JoinPath(LockPath, OutputRoot, ".publish.lock") || !JoinPath(Path, OutputRoot, "current.json") ||
        !Text.TryAssign(PointerPrefix) || !Text.TryAppend(Candidate) || !Text.TryAppend(PointerSuffix))
    {
        return Report(Diagnostic, "out_of_memory", "Could not prepare the generation pointer.", OutputRoot);
    }
    FPublicationLock Lock;
    if (!Lock.Acquire(LockPath))
    {
        return Report(Diagnostic, "publish_lock_failed", "Could not acquire the publication lock.", LockPath.View());
    }
    Array<uint8> Pointer;
    StringView Current;
    if (!ReadGeneration(OutputRoot, Pointer, Current, Diagnostic))
    {
        return false;
    }
    if (Current != Baseline)
    {
        return Report(Diagnostic, "stale_generation", "Another import published since this candidate was prepared. Retry with the current generation.", Path.View());
    }
    const EFileSystemResult Result = WriteContentFileAtomically(Path.View(), {reinterpret_cast<const uint8*>(Text.Data()), Text.Size()});
    return Result == EFileSystemResult::Success || Report(Diagnostic, "publish_replace_failed", DescribeFileSystemResult(Result), Path.View());
}

} // namespace LE::Detail
