#pragma once

#include "Containers/Array.h"
#include "Containers/Span.h"
#include "Containers/String.h"
#include "Types/EngineTypes.h"

namespace LE
{

enum class EContentIOResult : uint8
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
CONTENTIO_API EContentIOResult ReadContentFile(StringView Path, std::size_t MaxBytes, Array<uint8>& OutBytes) noexcept;
CONTENTIO_API EContentIOResult ReadContentFilePrefix(StringView Path, std::size_t MaxPrefixBytes, Array<uint8>& OutBytes, uint64& OutFileSize) noexcept;

// Replaces a file via a closed sibling temporary. The parent must exist.
CONTENTIO_API EContentIOResult WriteContentFileAtomically(StringView Path, Span<const uint8> Bytes) noexcept;

// Roots must be absolute. The relative input cannot contain parent traversal.
// Contained resolution rejects symbolic links and Windows reparse points.
CONTENTIO_API EContentIOResult ResolveContentFile(StringView AbsoluteRoot, StringView RelativePath, String& OutPath) noexcept;
// The optional diagnostic is empty on success or when the failing path is
// unavailable. Otherwise it identifies the entry or directory relative to the
// root. The file list remains unchanged on failure.
CONTENTIO_API EContentIOResult EnumerateContentFiles(StringView AbsoluteRoot, StringView Extension, std::size_t MaxFiles, Array<String>& OutRelativePaths, String* OutFailureRelativePath = nullptr) noexcept;

CONTENTIO_API const char* DescribeContentIOResult(EContentIOResult Result) noexcept;

} // namespace LE
