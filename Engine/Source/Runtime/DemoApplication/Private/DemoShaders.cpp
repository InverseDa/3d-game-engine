#include "DemoShaders.h"

#include "CoreMinimal.h"
#include "FileSystem/FileSystem.h"

#if PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif PLATFORM_MAC
#include <mach-o/dyld.h>
#endif

namespace LE
{

LE_DECLARE_LOG_CATEGORY_EXTERN(LogXBD);

namespace
{

constexpr std::size_t MaxShaderBytes = 16 * 1024 * 1024;
constexpr std::size_t SpirvHeaderWords = 5;
constexpr uint32 SpirvMagic = 0x07230203u;
constexpr uint32 SpirvEntryPointOpcode = 15;
constexpr uint32 SpirvVertexModel = 0;
constexpr uint32 SpirvFragmentModel = 4;
constexpr const char* VertexEntryPoint = "MainVS";
constexpr const char* FragmentEntryPoint = "MainPS";

String ParentDirectory(StringView Path) noexcept
{
    for (std::size_t Index = Path.Size(); Index > 0; --Index)
    {
        if (Path[Index - 1] == '/' || Path[Index - 1] == '\\')
        {
            return String(Path.Substr(0, Index - 1));
        }
    }
    return {};
}

uint32 DecodeLittleEndianWord(const uint8* Bytes) noexcept
{
    return static_cast<uint32>(Bytes[0]) |
        (static_cast<uint32>(Bytes[1]) << 8) |
        (static_cast<uint32>(Bytes[2]) << 16) |
        (static_cast<uint32>(Bytes[3]) << 24);
}

const char* FindShaderValidationFailure(Span<const uint8> Bytes, Span<const uint32> Words,
                                        uint32 ExecutionModel, StringView EntryPoint) noexcept
{
    const uint32 Version = Words[1];
    if (Words[0] != SpirvMagic || Version < 0x00010000u || Version > 0x00010600u ||
        (Version & 0xffu) != 0 || Words[3] == 0 || Words[4] != 0)
    {
        return "invalid SPIR-V header";
    }

    bool bFoundEntryPoint = false;
    for (std::size_t Offset = SpirvHeaderWords; Offset < Words.Size();)
    {
        const uint32 Instruction = Words[Offset];
        const std::size_t WordCount = Instruction >> 16;
        if (WordCount == 0 || WordCount > Words.Size() - Offset)
        {
            return "invalid SPIR-V instruction length";
        }
        if ((Instruction & 0xffffu) == SpirvEntryPointOpcode)
        {
            if (WordCount < 4 || Words[Offset + 2] == 0 || Words[Offset + 2] >= Words[3])
            {
                return "invalid SPIR-V entry point declaration";
            }
            const char* const Name = reinterpret_cast<const char*>(Bytes.Data() + (Offset + 3) * sizeof(uint32));
            const std::size_t Capacity = (WordCount - 3) * sizeof(uint32);
            std::size_t Length = 0;
            while (Length < Capacity && Name[Length] != '\0')
            {
                ++Length;
            }
            if (Length == Capacity)
            {
                return "invalid SPIR-V entry point name";
            }
            bFoundEntryPoint |= Words[Offset + 1] == ExecutionModel && StringView(Name, Length) == EntryPoint;
        }
        Offset += WordCount;
    }
    return bFoundEntryPoint ? nullptr : "expected SPIR-V stage and entry point not present";
}

bool LoadShader(StringView Directory, StringView FileName, uint32 ExecutionModel,
                StringView EntryPoint, Array<uint32>& OutWords) noexcept
{
    const String Path = JoinDemoPath(Directory, FileName);
    Array<uint8> Bytes;
    const EFileSystemResult Result = ReadContentFile(Path.View(), MaxShaderBytes, Bytes);
    if (Result != EFileSystemResult::Success)
    {
        LE_LOG(LogXBD, Error, "Could not read packaged shader '{}': {}.", Path.Data(), DescribeFileSystemResult(Result));
        return false;
    }
    if (Bytes.Size() <= SpirvHeaderWords * sizeof(uint32) || Bytes.Size() % sizeof(uint32) != 0)
    {
        LE_LOG(LogXBD, Error, "Invalid packaged shader '{}': invalid SPIR-V byte length ({} bytes).", Path.Data(), Bytes.Size());
        return false;
    }

    Array<uint32> Words;
    if (!Words.TryResize(Bytes.Size() / sizeof(uint32)))
    {
        LE_LOG(LogXBD, Error, "Could not allocate aligned bytecode for packaged shader '{}'.", Path.Data());
        return false;
    }
    for (std::size_t Index = 0; Index < Words.Size(); ++Index)
    {
        Words[Index] = DecodeLittleEndianWord(Bytes.Data() + Index * sizeof(uint32));
    }
    if (const char* Failure = FindShaderValidationFailure(Bytes.AsSpan(), Words.AsSpan(), ExecutionModel, EntryPoint))
    {
        LE_LOG(LogXBD, Error, "Invalid packaged shader '{}': {}.", Path.Data(), Failure);
        return false;
    }

    LE_LOG(LogXBD, Info, "Loaded packaged shader: '{}' ({} bytes).", Path.Data(), Bytes.Size());
    OutWords = std::move(Words);
    return true;
}

FSceneShaderBytecode DescribeShader(const Array<uint32>& Words, StringView EntryPoint) noexcept
{
    return {{reinterpret_cast<const char*>(Words.Data()), Words.Size() * sizeof(uint32)}, EntryPoint};
}

} // namespace

String GetDemoExecutableDirectory() noexcept
{
    // The OS paths are copied immediately to the engine UTF-8 string type.
#if PLATFORM_WINDOWS
    Array<wchar_t> ModulePath;
    if (!ModulePath.TryResize(32768))
    {
        return {};
    }
    const DWORD PathLength = GetModuleFileNameW(nullptr, ModulePath.Data(), static_cast<DWORD>(ModulePath.Size()));
    if (PathLength == 0 || PathLength >= ModulePath.Size())
    {
        return {};
    }

    const int SourceLength = static_cast<int>(PathLength);
    const int ByteCount = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, ModulePath.Data(), SourceLength, nullptr, 0, nullptr, nullptr);
    Array<char> Utf8;
    if (ByteCount <= 0 || !Utf8.TryResize(static_cast<std::size_t>(ByteCount)) ||
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, ModulePath.Data(), SourceLength, Utf8.Data(), ByteCount, nullptr, nullptr) != ByteCount)
    {
        return {};
    }
    return ParentDirectory(StringView(Utf8.Data(), Utf8.Size()));
#elif PLATFORM_MAC
    uint32 BufferSize = 0;
    _NSGetExecutablePath(nullptr, &BufferSize);
    Array<char> Buffer;
    if (BufferSize == 0 || !Buffer.TryResize(BufferSize) || _NSGetExecutablePath(Buffer.Data(), &BufferSize) != 0)
    {
        return {};
    }
    return ParentDirectory(StringView(Buffer.Data()));
#else
    return {};
#endif
}

String JoinDemoPath(StringView Directory, StringView Name) noexcept
{
    String Result(Directory);
    if (!Directory.IsEmpty() && Directory[Directory.Size() - 1] != '/' && Directory[Directory.Size() - 1] != '\\')
    {
        Result.Append("/");
    }
    Result.Append(Name);
    return Result;
}

bool FDemoShaderBundle::Load(StringView ExecutableDirectory) noexcept
{
    const String ShaderDirectory = JoinDemoPath(ExecutableDirectory, "Shaders/Vulkan");
    FDemoShaderBundle Candidate;
    if (!LoadShader(ShaderDirectory.View(), "BasePassShadowVS.spv", SpirvVertexModel, VertexEntryPoint, Candidate.BasePassVertex) ||
        !LoadShader(ShaderDirectory.View(), "BasePassShadowPS.spv", SpirvFragmentModel, FragmentEntryPoint, Candidate.BasePassPixel) ||
        !LoadShader(ShaderDirectory.View(), "CompositeVS.spv", SpirvVertexModel, VertexEntryPoint, Candidate.CompositeVertex) ||
        !LoadShader(ShaderDirectory.View(), "CompositePS.spv", SpirvFragmentModel, FragmentEntryPoint, Candidate.CompositePixel) ||
        !LoadShader(ShaderDirectory.View(), "ShadowDepthVS.spv", SpirvVertexModel, VertexEntryPoint, Candidate.ShadowDepthVertex))
    {
        return false;
    }
    *this = std::move(Candidate);
    return true;
}

FSceneRenderShaders FDemoShaderBundle::GetShaders() const noexcept
{
    return {
        DescribeShader(BasePassVertex, VertexEntryPoint),
        DescribeShader(BasePassPixel, FragmentEntryPoint),
        DescribeShader(CompositeVertex, VertexEntryPoint),
        DescribeShader(CompositePixel, FragmentEntryPoint),
        DescribeShader(ShadowDepthVertex, VertexEntryPoint),
    };
}

} // namespace LE
