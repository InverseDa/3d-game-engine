#pragma once

#include "CoreMinimal.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

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

LE_DECLARE_LOG_CATEGORY_EXTERN(LogXBD);

enum class ERuntimeShaderStage : uint8
{
    Vertex,
    Fragment,
    Compute,
};

namespace Launch::ShaderRuntimeCompiler
{
inline std::string GetParentPath(const std::string& Path)
{
    if (Path.empty())
    {
        return std::string();
    }

    const size_t LastSeparator = Path.find_last_of("/\\");
    if (LastSeparator == std::string::npos)
    {
        return std::string();
    }

    return Path.substr(0, LastSeparator);
}

inline std::string JoinPath(const std::string& Left, const std::string& Right)
{
    if (Left.empty())
    {
        return Right;
    }

    const char LastChar = Left[Left.size() - 1];
    if (LastChar == '/' || LastChar == '\\')
    {
        return Left + Right;
    }

    return Left + "/" + Right;
}

inline std::string GetExecutableDirectory()
{
#if PLATFORM_MAC
    uint32 BufferSize = 0;
    _NSGetExecutablePath(nullptr, &BufferSize);
    if (BufferSize == 0)
    {
        return std::string();
    }

    std::vector<char> Buffer(BufferSize);
    if (_NSGetExecutablePath(Buffer.data(), &BufferSize) != 0)
    {
        return std::string();
    }

    return GetParentPath(std::string(Buffer.data()));
#elif PLATFORM_WINDOWS
    char ModulePath[MAX_PATH] = {};
    const DWORD PathLength = GetModuleFileNameA(nullptr, ModulePath, static_cast<DWORD>(sizeof(ModulePath)));
    if (PathLength == 0 || PathLength >= sizeof(ModulePath))
    {
        return std::string();
    }

    return GetParentPath(std::string(ModulePath, PathLength));
#else
    return std::string();
#endif
}

inline bool ReadBinaryFile(const std::string& FilePath, std::vector<char>& OutBuffer)
{
    std::ifstream File(FilePath.c_str(), std::ios::ate | std::ios::binary);
    if (!File.is_open())
    {
        return false;
    }

    const size_t FileSize = static_cast<size_t>(File.tellg());
    if (FileSize == 0)
    {
        OutBuffer.clear();
        return true;
    }

    OutBuffer.resize(FileSize);
    File.seekg(0);
    File.read(OutBuffer.data(), FileSize);
    return true;
}

inline std::string ReadTextFile(const std::string& FilePath)
{
    std::ifstream File(FilePath.c_str(), std::ios::in);
    if (!File.is_open())
    {
        return std::string();
    }

    std::ostringstream Stream;
    Stream << File.rdbuf();
    return Stream.str();
}

inline std::string ResolveExistingPath(const char* RelativePath)
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
        const std::string Candidate = Prefix[0]
            ? (std::string(Prefix) + "/" + RelativePath)
            : std::string(RelativePath);
        if (std::filesystem::exists(Candidate))
        {
            return Candidate;
        }
    }

    std::string SearchBase = GetExecutableDirectory();
    for (int32 i = 0; i < 10 && !SearchBase.empty(); ++i)
    {
        const std::string Candidate = JoinPath(SearchBase, RelativePath);
        if (std::filesystem::exists(Candidate))
        {
            return Candidate;
        }
        SearchBase = GetParentPath(SearchBase);
    }

    return std::string();
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

inline std::string GetCompilerExecutable()
{
#if PLATFORM_WINDOWS
    if (const char* VulkanSdk = std::getenv("VULKAN_SDK"))
    {
        std::string Candidate = JoinPath(VulkanSdk, "Bin");
        Candidate = JoinPath(Candidate, "glslangValidator.exe");
        if (std::filesystem::exists(Candidate))
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

inline std::string Quote(const std::string& Value)
{
    return "\"" + Value + "\"";
}

inline bool CompileHlslToSpirv(
    const char* SourceFilename,
    ERuntimeShaderStage Stage,
    std::vector<char>& OutByteCode,
    const char* EntryPoint = "Main")
{
    const std::string SourcePath = ResolveExistingPath(SourceFilename);
    if (SourcePath.empty())
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

    const std::filesystem::path TempDir = std::filesystem::temp_directory_path() / "limitless_shader_runtime";
    std::error_code ErrorCode;
    std::filesystem::create_directories(TempDir, ErrorCode);

    const size_t SourceHash = std::hash<std::string>{}(SourcePath + StageArg + EntryPoint);
    const std::filesystem::path OutputPath = TempDir / ("shader_" + std::to_string(SourceHash) + ".spv");
    const std::filesystem::path LogPath = TempDir / ("shader_" + std::to_string(SourceHash) + ".log");

    std::ostringstream Command;
    // NOTE: the compiler executable is intentionally NOT quoted. When std::system
    // hands the command to `cmd.exe /c`, a leading quote triggers cmd.exe's
    // quote-stripping heuristic (it strips the first and last quote of the whole
    // line when more than two quotes are present), which corrupts the command.
    // The resolved VULKAN_SDK path contains no spaces, so quoting is unnecessary.
    Command
        << GetCompilerExecutable()
        << " -V -D"
        << " -S " << StageArg
        << " -e " << EntryPoint
        << " " << Quote(SourcePath)
        << " -o " << Quote(OutputPath.string())
        << " > " << Quote(LogPath.string()) << " 2>&1";

    const int32 CompileResult = std::system(Command.str().c_str());
    if (CompileResult != 0)
    {
        const std::string CompileLog = ReadTextFile(LogPath.string());
        LE_LOG(
            LogXBD,
            Error,
            "Runtime shader compilation failed. Source={}, Stage={}, ExitCode={}, Log={}",
            SourcePath,
            StageArg,
            CompileResult,
            CompileLog.empty() ? "<empty>" : CompileLog.c_str());
        return false;
    }

    if (!ReadBinaryFile(OutputPath.string(), OutByteCode))
    {
        LE_LOG(LogXBD, Error, "Failed to read compiled shader bytecode: {}", OutputPath.string());
        return false;
    }

    if (OutByteCode.empty() || (OutByteCode.size() % 4) != 0)
    {
        LE_LOG(LogXBD, Error, "Compiled shader bytecode is invalid: {} (size={})", SourcePath, OutByteCode.size());
        return false;
    }

    std::filesystem::remove(OutputPath, ErrorCode);
    std::filesystem::remove(LogPath, ErrorCode);

    LE_LOG(LogXBD, Info, "Compiled shader at runtime: {} -> {} bytes", SourcePath, OutByteCode.size());
    return true;
}
}
