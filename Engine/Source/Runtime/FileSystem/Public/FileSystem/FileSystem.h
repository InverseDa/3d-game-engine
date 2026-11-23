#pragma once

#include "Containers/Array.h"
#include "Containers/Span.h"
#include "Containers/String.h"
#include "Types/EngineTypes.h"

namespace LE
{

enum class EFileSystemResult : uint8
{
    Success = 0,
    InvalidPath,
    OutsideRoot,
    RedirectedPath,
    NotFound,
    NotRegularFile,
    LimitExceeded,
    OutOfMemory,
    ReadFailed,
    WriteFailed,
    ReplaceFailed,
    EnumerationFailed,
};

// Paths use UTF-8. Standalone file operations accept absolute and relative
// paths. Outputs retain their previous values on failure.
FILESYSTEM_API EFileSystemResult ReadContentFile(StringView Path, std::size_t MaxBytes, Array<uint8>& OutBytes) noexcept;
FILESYSTEM_API EFileSystemResult ReadContentFilePrefix(StringView Path, std::size_t MaxPrefixBytes, Array<uint8>& OutBytes, uint64& OutFileSize) noexcept;

// Replaces a file via a closed sibling temporary. The parent must exist.
FILESYSTEM_API EFileSystemResult WriteContentFileAtomically(StringView Path, Span<const uint8> Bytes) noexcept;

// Roots must be absolute. The relative input cannot contain parent traversal.
// Contained resolution rejects symbolic links and Windows reparse points.
FILESYSTEM_API EFileSystemResult ResolveContentFile(StringView AbsoluteRoot, StringView RelativePath, String& OutPath) noexcept;
// The optional diagnostic is empty on success or when the failing path is
// unavailable. Otherwise it identifies the entry or directory relative to the
// root. The file list remains unchanged on failure.
FILESYSTEM_API EFileSystemResult EnumerateContentFiles(StringView AbsoluteRoot, StringView Extension, std::size_t MaxFiles, Array<String>& OutRelativePaths, String* OutFailureRelativePath = nullptr) noexcept;

FILESYSTEM_API const char* DescribeFileSystemResult(EFileSystemResult Result) noexcept;

} // namespace LE
