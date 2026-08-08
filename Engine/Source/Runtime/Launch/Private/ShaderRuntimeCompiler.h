#pragma once

#include "CoreMinimal.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cstdio>

#if PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#endif

#if PLATFORM_MAC
    #include <mach-o/dyld.h>
#endif

namespace LE
{

LE_DECLARE_LOG_CATEGORY_EXTERN(LogXBD);

enum class ERuntimeShaderStage : uint8
{
    Vertex,
    Fragment,
    Compute,
};

namespace Launch::ShaderRuntimeCompiler
{
inline LE::String GetParentPath(const LE::String& Path)
{
    if (Path.IsEmpty())
    {
        return {};
    }

    for (size_t Index = Path.Size(); Index > 0; --Index)
    {
        const char Character = Path[Index - 1];
        if (Character == '/' || Character == '\\')
        {
            return LE::String(Path.View().Substr(0, Index - 1));
        }
    }
    return {};
}

inline LE::String JoinPath(const LE::String& Left, const LE::StringView Right)
{
    if (Left.IsEmpty())
    {
        return LE::String(Right);
    }

    LE::String Result(Left);
    const char LastChar = Left[Left.Size() - 1];
    if (LastChar == '/' || LastChar == '\\')
    {
        Result.Append(Right);
        return Result;
    }

    Result.Append("/");
    Result.Append(Right);
    return Result;
}

inline LE::String GetExecutableDirectory()
{
#if PLATFORM_MAC
    uint32 BufferSize = 0;
    _NSGetExecutablePath(nullptr, &BufferSize);
    if (BufferSize == 0)
    {
        return {};
    }

    LE::Array<char> Buffer;
    Buffer.Resize(BufferSize);
    if (_NSGetExecutablePath(Buffer.Data(), &BufferSize) != 0)
    {
        return {};
    }

    return GetParentPath(LE::String(Buffer.Data()));
#elif PLATFORM_WINDOWS
    char ModulePath[MAX_PATH] = {};
    const DWORD PathLength = GetModuleFileNameA(nullptr, ModulePath, static_cast<DWORD>(sizeof(ModulePath)));
    if (PathLength == 0 || PathLength >= sizeof(ModulePath))
    {
        return {};
    }

    return GetParentPath(LE::String(ModulePath, PathLength));
#else
    return {};
#endif
}

inline bool ReadBinaryFile(const LE::String& FilePath, LE::Array<char>& OutBuffer)
{
    std::ifstream File(FilePath.Data(), std::ios::ate | std::ios::binary);
    if (!File.is_open())
    {
        return false;
    }

    const size_t FileSize = static_cast<size_t>(File.tellg());
    if (FileSize == 0)
    {
        OutBuffer.Clear();
        return true;
    }

    OutBuffer.Resize(FileSize);
    File.seekg(0);
    File.read(OutBuffer.Data(), static_cast<std::streamsize>(FileSize));
    return true;
}

inline LE::String ReadTextFile(const LE::String& FilePath)
{
    LE::Array<char> Bytes;
    if (!ReadBinaryFile(FilePath, Bytes))
    {
        return {};
    }
    return Bytes.IsEmpty() ? LE::String() : LE::String(Bytes.Data(), Bytes.Size());
}

inline LE::String ResolveExistingPath(const char* RelativePath)
{
    const char* RelativePrefixes[] = {
        "",
        ".",
        "..",
        "../..",
        "../../..",
        "../../../..",
        "../../../../..",
        "../../../../../..",
        "../../../../../../.."
    };

    for (const char* Prefix : RelativePrefixes)
    {
        LE::String Candidate(Prefix);
        if (Prefix[0]) { Candidate.Append("/"); }
        Candidate.Append(RelativePath);
        if (std::filesystem::exists(Candidate.Data()))
        {
            return Candidate;
        }
    }

    LE::String SearchBase = GetExecutableDirectory();
    for (int32 i = 0; i < 10 && !SearchBase.IsEmpty(); ++i)
    {
        const LE::String Candidate = JoinPath(SearchBase, RelativePath);
        if (std::filesystem::exists(Candidate.Data()))
        {
            return Candidate;
        }
        SearchBase = GetParentPath(SearchBase);
    }

    return {};
}

inline const char* ToGlslangStage(ERuntimeShaderStage Stage)
{
    switch (Stage)
    {
        case ERuntimeShaderStage::Vertex: return "vert";
        case ERuntimeShaderStage::Fragment: return "frag";
        case ERuntimeShaderStage::Compute: return "comp";
        default: return "";
    }
}

inline LE::String GetCompilerExecutable()
{
#if PLATFORM_WINDOWS
    if (const char* VulkanSdk = std::getenv("VULKAN_SDK"))
    {
        LE::String Candidate = JoinPath(LE::String(VulkanSdk), "Bin");
        Candidate = JoinPath(Candidate, "glslangValidator.exe");
        if (std::filesystem::exists(Candidate.Data()))
        {
            return Candidate;
        }
    }
#endif
#if PLATFORM_MAC
    const char* MacCandidates[] = {
        "/opt/homebrew/bin/glslangValidator",
        "/usr/local/bin/glslangValidator",
        "glslangValidator"
    };

    for (const char* Candidate : MacCandidates)
    {
        if (Candidate[0] == '/')
        {
            if (std::filesystem::exists(Candidate))
            {
                return Candidate;
            }
        }
        else
        {
            return Candidate;
        }
    }
#endif
    return "glslangValidator";
}

inline LE::String Quote(const LE::String& Value)
{
    LE::String Result("\"");
    Result.Append(Value.View());
    Result.Append("\"");
    return Result;
}

inline bool CompileHlslToSpirv(
    const char* SourceFilename,
    ERuntimeShaderStage Stage,
    LE::Array<char>& OutByteCode,
    const char* EntryPoint = "Main")
{
    const LE::String SourcePath = ResolveExistingPath(SourceFilename);
    if (SourcePath.IsEmpty())
    {
        LE_LOG(LogXBD, Error, "Failed to resolve shader source: {}", SourceFilename);
        return false;
    }

    const char* StageArg = ToGlslangStage(Stage);
    if (StageArg[0] == '\0')
    {
        LE_LOG(LogXBD, Error, "Unsupported shader stage for runtime compilation: {}", static_cast<uint32>(Stage));
        return false;
    }

    const std::filesystem::path TempDirAdapter = std::filesystem::temp_directory_path() / "limitless_shader_runtime";
    std::error_code ErrorCode;
    std::filesystem::create_directories(TempDirAdapter, ErrorCode);

    // `path::string()` is an exact filesystem interop adapter. Convert its
    // owning result immediately and keep all engine-side path assembly in String.
    const auto TempDirNative = TempDirAdapter.string();
    const LE::String TempDir(TempDirNative.data(), TempDirNative.size());

    LE::String HashInput(SourcePath);
    HashInput.Append(StageArg);
    HashInput.Append(EntryPoint);
    const size_t SourceHash = LE::StringHash{}(HashInput);
    char HashText[32]{};
    std::snprintf(HashText, sizeof(HashText), "%zu", SourceHash);

    LE::String OutputFilename("shader_");
    OutputFilename.Append(HashText);
    OutputFilename.Append(".spv");
    const LE::String OutputPath = JoinPath(TempDir, OutputFilename.View());

    LE::String LogFilename("shader_");
    LogFilename.Append(HashText);
    LogFilename.Append(".log");
    const LE::String LogPath = JoinPath(TempDir, LogFilename.View());

    LE::String Command = GetCompilerExecutable();
    // NOTE: the compiler executable is intentionally NOT quoted. When std::system
    // hands the command to `cmd.exe /c`, a leading quote triggers cmd.exe's
    // quote-stripping heuristic (it strips the first and last quote of the whole
    // line when more than two quotes are present), which corrupts the command.
    // The resolved VULKAN_SDK path contains no spaces, so quoting is unnecessary.
    Command.Append(" -V -D -S ");
    Command.Append(StageArg);
    Command.Append(" -e ");
    Command.Append(EntryPoint);
    Command.Append(" ");
    Command.Append(Quote(SourcePath).View());
    Command.Append(" -o ");
    Command.Append(Quote(OutputPath).View());
    Command.Append(" > ");
    Command.Append(Quote(LogPath).View());
    Command.Append(" 2>&1");

    const int32 CompileResult = std::system(Command.Data());
    if (CompileResult != 0)
    {
        const LE::String CompileLog = ReadTextFile(LogPath);
        LE_LOG(
            LogXBD,
            Error,
            "Runtime shader compilation failed. Source={}, Stage={}, ExitCode={}, Log={}",
            SourcePath.Data(),
            StageArg,
            CompileResult,
            CompileLog.IsEmpty() ? "<empty>" : CompileLog.Data());
        return false;
    }

    if (!ReadBinaryFile(OutputPath, OutByteCode))
    {
        LE_LOG(LogXBD, Error, "Failed to read compiled shader bytecode: {}", OutputPath.Data());
        return false;
    }

    if (OutByteCode.IsEmpty() || (OutByteCode.Size() % 4) != 0)
    {
        LE_LOG(LogXBD, Error, "Compiled shader bytecode is invalid: {} (size={})", SourcePath.Data(), OutByteCode.Size());
        return false;
    }

    std::filesystem::remove(OutputPath.Data(), ErrorCode);
    std::filesystem::remove(LogPath.Data(), ErrorCode);

    LE_LOG(LogXBD, Info, "Compiled shader at runtime: {} -> {} bytes", SourcePath.Data(), OutByteCode.Size());
    return true;
}
} // namespace Launch::ShaderRuntimeCompiler

} // namespace LE
