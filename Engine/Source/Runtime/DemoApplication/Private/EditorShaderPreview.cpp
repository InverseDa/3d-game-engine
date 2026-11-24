#include "EditorShaderPreview.h"

#if WITH_EDITOR && PLATFORM_WINDOWS

#include "CoreMinimal.h"
#include "FileSystem/FileSystem.h"
#include "Renderer/SceneRenderer.h"
#include "Types/Uuid.h"

#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <Windows.h>

namespace LE
{

LE_DECLARE_LOG_CATEGORY_EXTERN(LogXBD);

namespace
{

constexpr DWORD MaxPathUnits = 32768;
constexpr DWORD CompileTimeoutMilliseconds = 30000;
constexpr DWORD CleanupTimeoutMilliseconds = 5000;
constexpr std::size_t MaxPreviewShaderBytes = 16 * 1024 * 1024;
constexpr std::size_t MaxDiagnosticBytes = 64 * 1024;
constexpr std::size_t MaxResultBytes = 128;
constexpr char WorkerRelativePath[] = "Engine/Builder/Source/Content/PreviewVulkanShader.ts";

struct FWinHandle final : FNonCopyable
{
    ~FWinHandle() { Reset(); }

    void Reset(HANDLE Replacement = nullptr) noexcept
    {
        if (Value != nullptr)
        {
            CloseHandle(Value);
        }
        Value = Replacement;
    }

    HANDLE Value = nullptr;
};

bool IsPathSeparator(char Character) noexcept
{
    return Character == '/' || Character == '\\';
}

bool IsAbsolutePath(StringView Path) noexcept
{
    if (Path.Size() < 3)
    {
        return false;
    }
    const char Start = Path[0];
    const bool bDrive = ((Start >= 'A' && Start <= 'Z') || (Start >= 'a' && Start <= 'z')) && Path[1] == ':' && IsPathSeparator(Path[2]);
    const bool bUnc = IsPathSeparator(Start) && IsPathSeparator(Path[1]) && Path[2] != '.' && Path[2] != '?';
    return bDrive || bUnc;
}

bool JoinPath(StringView Directory, StringView Name, String& Out) noexcept
{
    return !Directory.IsEmpty() && Out.TryAssign(Directory) &&
           (IsPathSeparator(Directory[Directory.Size() - 1]) || Out.TryAppend("/")) && Out.TryAppend(Name);
}

bool ToWide(StringView Text, Array<wchar_t>& Out) noexcept
{
    if (Text.IsEmpty() || Text.Size() > MaxPathUnits * 4)
    {
        return false;
    }
    const int Length = static_cast<int>(Text.Size());
    const int Count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text.Data(), Length, nullptr, 0);
    if (Count <= 0 || Count >= static_cast<int>(MaxPathUnits) || !Out.TryResize(static_cast<std::size_t>(Count) + 1) ||
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text.Data(), Length, Out.Data(), Count) != Count)
    {
        return false;
    }
    Out[Count] = L'\0';
    return true;
}

bool ToUtf8(const wchar_t* Characters, DWORD Length, String& Out) noexcept
{
    const int InputLength = static_cast<int>(Length);
    const int Count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Characters, InputLength, nullptr, 0, nullptr, nullptr);
    Array<char> Utf8;
    if (Count <= 0 || !Utf8.TryResize(static_cast<std::size_t>(Count)) ||
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Characters, InputLength, Utf8.Data(), Count, nullptr, nullptr) != Count)
    {
        return false;
    }
    for (char& Character : Utf8)
    {
        if (Character == '\\')
        {
            Character = '/';
        }
    }
    return Out.TryAssign({Utf8.Data(), Utf8.Size()});
}

bool ReadEnvironment(const wchar_t* Name, String& Out) noexcept
{
    Array<wchar_t> Characters;
    const DWORD Capacity = GetEnvironmentVariableW(Name, nullptr, 0);
    if (Capacity == 0 || Capacity > MaxPathUnits || !Characters.TryResize(Capacity))
    {
        return false;
    }
    const DWORD Length = GetEnvironmentVariableW(Name, Characters.Data(), Capacity);
    return Length != 0 && Length < Capacity && ToUtf8(Characters.Data(), Length, Out);
}

bool IsExecutableFile(StringView Path) noexcept
{
    Array<wchar_t> Wide;
    if (!IsAbsolutePath(Path) || !ToWide(Path, Wide))
    {
        return false;
    }
    const DWORD Attributes = GetFileAttributesW(Wide.Data());
    return Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool FindNode(String& Node) noexcept
{
    String Override;
    if (ReadEnvironment(L"LIMITLESS_BUILDER_NODE", Override) && IsExecutableFile(Override.View()))
    {
        Node.Swap(Override);
        return true;
    }

    String SearchPath;
    if (!ReadEnvironment(L"PATH", SearchPath))
    {
        return false;
    }
    const StringView Path = SearchPath.View();
    for (std::size_t Start = 0, End = 0; End <= Path.Size(); ++End)
    {
        if (End != Path.Size() && Path[End] != ';')
        {
            continue;
        }
        StringView Directory = Path.Substr(Start, End - Start);
        Start = End + 1;
        if (Directory.Size() >= 2 && Directory[0] == '"' && Directory[Directory.Size() - 1] == '"')
        {
            Directory = Directory.Substr(1, Directory.Size() - 2);
        }
        if (IsAbsolutePath(Directory) && JoinPath(Directory, "node.exe", Node) && IsExecutableFile(Node.View()))
        {
            return true;
        }
    }
    return false;
}

bool CreatePrivateDirectory(StringView Directory) noexcept
{
    Array<wchar_t> Wide;
    return ToWide(Directory, Wide) && CreateDirectoryW(Wide.Data(), nullptr) != FALSE;
}

bool CreateSessionDirectory(String& Out) noexcept
{
    Array<wchar_t> Temp;
    if (!Temp.TryResize(MaxPathUnits))
    {
        return false;
    }
    const DWORD Length = GetTempPathW(MaxPathUnits, Temp.Data());
    String Parent;
    FUuid Id;
    if (Length == 0 || Length >= MaxPathUnits || !ToUtf8(Temp.Data(), Length, Parent) ||
        !IsAbsolutePath(Parent.View()) || !FUuid::TryGenerate(Id))
    {
        return false;
    }
    String Leaf("LimitlessShaderPreview-");
    String Directory;
    if (!Leaf.TryAppend(Id.ToString().View()) || !JoinPath(Parent.View(), Leaf.View(), Directory) || !CreatePrivateDirectory(Directory.View()))
    {
        return false;
    }
    Out.Swap(Directory);
    return true;
}

// These paths start at a directory this session created. A reparse directory
// is removed as a link and is never enumerated.
bool IsMissingPathError(DWORD Error) noexcept
{
    return Error == ERROR_FILE_NOT_FOUND || Error == ERROR_PATH_NOT_FOUND;
}

bool RemoveRequestFiles(StringView Directory, bool bKeepDiagnostic = false) noexcept
{
    Array<wchar_t> Wide;
    if (!ToWide(Directory, Wide))
    {
        return false;
    }
    const DWORD Attributes = GetFileAttributesW(Wide.Data());
    if (Attributes == INVALID_FILE_ATTRIBUTES)
    {
        return IsMissingPathError(GetLastError());
    }
    if ((Attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        return DeleteFileW(Wide.Data()) != FALSE || IsMissingPathError(GetLastError());
    }
    if ((Attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
    {
        String Pattern;
        Array<wchar_t> WidePattern;
        if (JoinPath(Directory, "*", Pattern) && ToWide(Pattern.View(), WidePattern))
        {
            WIN32_FIND_DATAW Entry{};
            const HANDLE Find = FindFirstFileW(WidePattern.Data(), &Entry);
            if (Find != INVALID_HANDLE_VALUE)
            {
                do
                {
                    // cFileName is NUL-terminated and has a fixed Win32 limit.
                    DWORD Length = 0;
                    while (Entry.cFileName[Length] != L'\0')
                    {
                        ++Length;
                    }
                    String Name;
                    if (!ToUtf8(Entry.cFileName, Length, Name) || Name.View() == StringView(".") || Name.View() == StringView("..") ||
                        (bKeepDiagnostic && Name.View() == StringView("diagnostics.json")))
                    {
                        continue;
                    }
                    String Child;
                    if (JoinPath(Directory, Name.View(), Child))
                    {
                        RemoveRequestFiles(Child.View());
                    }
                } while (FindNextFileW(Find, &Entry));
                FindClose(Find);
            }
        }
    }
    return bKeepDiagnostic || RemoveDirectoryW(Wide.Data()) != FALSE || IsMissingPathError(GetLastError());
}

String RequestIdText(uint64 Id) noexcept
{
    char Digits[20];
    std::size_t Start = sizeof(Digits);
    do
    {
        Digits[--Start] = static_cast<char>('0' + Id % 10);
        Id /= 10;
    } while (Id != 0);
    return String(StringView(Digits + Start, sizeof(Digits) - Start));
}

bool AppendPathArgument(String& CommandLine, StringView Text) noexcept
{
    // Quote valid native paths. The command line uses forward slashes so a
    // trailing directory separator cannot consume the closing quote.
    if (!CommandLine.TryAppend("\""))
    {
        return false;
    }
    for (std::size_t Index = 0; Index < Text.Size(); ++Index)
    {
        const char Character = Text[Index] == '\\' ? '/' : Text[Index];
        if (Character == '"' || !CommandLine.TryAppend({&Character, 1}))
        {
            return false;
        }
    }
    return CommandLine.TryAppend("\"");
}

bool StartWorker(StringView Node, StringView Worker, StringView Checkout, StringView Directory, StringView RequestId,
                 FWinHandle& Job, FWinHandle& Process) noexcept
{
    String CommandLine;
    Array<wchar_t> WideCommand;
    Array<wchar_t> WideNode;
    Array<wchar_t> WideCheckout;
    if (!AppendPathArgument(CommandLine, Node) || !CommandLine.TryAppend(" --no-warnings --experimental-strip-types ") ||
        !AppendPathArgument(CommandLine, Worker) || !CommandLine.TryAppend(" ") || !AppendPathArgument(CommandLine, Checkout) ||
        !CommandLine.TryAppend(" ") || !AppendPathArgument(CommandLine, Directory) || !CommandLine.TryAppend(" ") ||
        !CommandLine.TryAppend(RequestId) || !ToWide(CommandLine.View(), WideCommand) || !ToWide(Node, WideNode) || !ToWide(Checkout, WideCheckout))
    {
        return false;
    }

    FWinHandle CandidateJob;
    CandidateJob.Reset(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limit{};
    Limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (CandidateJob.Value == nullptr || !SetInformationJobObject(CandidateJob.Value, JobObjectExtendedLimitInformation, &Limit, sizeof(Limit)))
    {
        return false;
    }

    STARTUPINFOW Startup{};
    Startup.cb = sizeof(Startup);
    PROCESS_INFORMATION Child{};
    // Leave the disposable request directory free of the worker's CWD lock.
    // The worker and its compiler still receive explicit absolute scratch paths.
    if (!CreateProcessW(WideNode.Data(), WideCommand.Data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                        nullptr, WideCheckout.Data(), &Startup, &Child))
    {
        return false;
    }

    FWinHandle Thread;
    Thread.Reset(Child.hThread);
    Process.Reset(Child.hProcess);
    if (!AssignProcessToJobObject(CandidateJob.Value, Process.Value) || ResumeThread(Thread.Value) == static_cast<DWORD>(-1))
    {
        // If job assignment failed the child never ran and has no descendants.
        TerminateProcess(Process.Value, ERROR_CANCELLED);
        Process.Reset();
        return false;
    }
    Job.Value = CandidateJob.Value;
    CandidateJob.Value = nullptr;
    return true;
}

const char* ReadWorkerResult(StringView Directory, StringView RequestId, DWORD ExitCode) noexcept
{
    String Path;
    Array<uint8> Bytes;
    if (ResolveContentFile(Directory, "result.txt", Path) != EFileSystemResult::Success ||
        ReadContentFile(Path.View(), MaxResultBytes, Bytes) != EFileSystemResult::Success || Bytes.Size() <= RequestId.Size() + 2)
    {
        return "worker_failed";
    }
    const StringView Result(reinterpret_cast<const char*>(Bytes.Data()), Bytes.Size());
    if (Result.Substr(0, RequestId.Size()) != RequestId || Result[RequestId.Size()] != '\n' || Result[Result.Size() - 1] != '\n')
    {
        return "worker_failed";
    }
    const StringView Code = Result.Substr(RequestId.Size() + 1, Result.Size() - RequestId.Size() - 2);
    if (ExitCode == 0)
    {
        return Code == StringView("ok") ? "ok" : "worker_failed";
    }
    constexpr const char* FailureCodes[] = {"tools_unavailable", "source_error", "source_changed", "compile_failed", "invalid_spirv", "baseline_invalid",
                                           "abi_incompatible", "reflection_failed", "worker_failed"};
    for (const char* Failure : FailureCodes)
    {
        if (ExitCode == 1 && Code == StringView(Failure))
        {
            return Failure;
        }
    }
    return "worker_failed";
}

const char* InstallError(EBasePassPixelShaderInstallResult Result) noexcept
{
    switch (Result)
    {
    case EBasePassPixelShaderInstallResult::Success:
        return "ok";
    case EBasePassPixelShaderInstallResult::InvalidArguments:
        return "invalid_spirv";
    case EBasePassPixelShaderInstallResult::ShaderCreationFailed:
        return "shader_creation_failed";
    case EBasePassPixelShaderInstallResult::PipelineCreationFailed:
        return "pipeline_creation_failed";
    case EBasePassPixelShaderInstallResult::FrameRetirementFailed:
        return "frame_retirement_failed";
    }
    return "pipeline_creation_failed";
}

void LogPreview(uint64 Request, const char* Status, const char* Stage, const char* Code, const char* Diagnostic = "", DWORD ProcessId = 0) noexcept
{
    LE_LOG(LogXBD, Info, "Editor shader preview {{\"requestId\":\"{}\",\"status\":\"{}\",\"stage\":\"{}\",\"code\":\"{}\",\"diagnostic\":\"{}\",\"processId\":{}}}",
           Request, Status, Stage, Code, Diagnostic, ProcessId);
}

} // namespace

struct FEditorShaderPreview::FState final
{
    const char* Begin(uint64 Id, Span<const char> Baseline) noexcept
    {
        String Checkout;
        if (!ReadEnvironment(L"LIMITLESS_EDITOR_SHADER_ROOT", Checkout))
        {
            return "not_configured";
        }
        String Worker;
        String Node;
        if (ResolveContentFile(Checkout.View(), WorkerRelativePath, Worker) != EFileSystemResult::Success || !FindNode(Node))
        {
            return "tools_unavailable";
        }

        if (SessionDirectory.IsEmpty() && !CreateSessionDirectory(SessionDirectory))
        {
            return "workspace_failed";
        }
        if (!RequestDirectory.IsEmpty())
        {
            RemoveRequestFiles(RequestDirectory.View());
        }
        RequestDirectory.Clear();
        RetainedDiagnostic.Clear();
        const String IdText = RequestIdText(Id);
        String Directory;
        if (!JoinPath(SessionDirectory.View(), IdText.View(), Directory) || !CreatePrivateDirectory(Directory.View()))
        {
            return "workspace_failed";
        }
        RequestDirectory.Swap(Directory);
        String BaselineFile;
        if (Baseline.IsEmpty() || Baseline.Size() > MaxPreviewShaderBytes || !JoinPath(RequestDirectory.View(), "baseline.spv", BaselineFile) ||
            WriteContentFileAtomically(BaselineFile.View(), {reinterpret_cast<const uint8*>(Baseline.Data()), Baseline.Size()}) != EFileSystemResult::Success)
        {
            return "baseline_unavailable";
        }

        if (!StartWorker(Node.View(), Worker.View(), Checkout.View(), RequestDirectory.View(), IdText.View(), Job, Process))
        {
            return "worker_start_failed";
        }
        Request = Id;
        StartedAt = GetTickCount64();
        return nullptr;
    }

    void CloseWorker() noexcept
    {
        Job.Reset();
        Process.Reset();
    }

    DWORD CheckWorkerTree() const noexcept
    {
        if (Process.Value != nullptr)
        {
            const DWORD ProcessWait = WaitForSingleObject(Process.Value, 0);
            if (ProcessWait != WAIT_OBJECT_0)
            {
                return ProcessWait;
            }
        }
        if (Job.Value == nullptr)
        {
            return WAIT_OBJECT_0;
        }
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION Accounting{};
        if (!QueryInformationJobObject(Job.Value, JobObjectBasicAccountingInformation, &Accounting, sizeof(Accounting), nullptr))
        {
            return WAIT_FAILED;
        }
        return Accounting.ActiveProcesses == 0 ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
    }

    void CancelWorker() noexcept
    {
        if (!bCancelling)
        {
            bCancelling = true;
            CancelledAt = GetTickCount64();
            if (Job.Value != nullptr && !TerminateJobObject(Job.Value, ERROR_CANCELLED))
            {
                // Closing the job is a second termination route. Still poll
                // the primary and retry deletion if a descendant has open files.
                Job.Reset();
            }
        }
    }

    void PollCancellation() noexcept
    {
        const bool bExpired = GetTickCount64() - CancelledAt >= CleanupTimeoutMilliseconds;
        if (CheckWorkerTree() != WAIT_OBJECT_0 && !bExpired)
        {
            return;
        }
        CloseWorker();
        if (RemoveRequestFiles(RequestDirectory.View()) || bExpired)
        {
            // The cancelled request is never inspected, even if a result file
            // was written before termination. Paths are unique and never reused.
            RequestDirectory.Clear();
            Request = 0;
            bCancelling = false;
        }
    }

    void RetainDiagnostic() noexcept
    {
        String Path;
        Array<uint8> Prefix;
        uint64 Size = 0;
        if (ResolveContentFile(RequestDirectory.View(), "diagnostics.json", Path) != EFileSystemResult::Success ||
            ReadContentFilePrefix(Path.View(), 0, Prefix, Size) != EFileSystemResult::Success || Size == 0 || Size > MaxDiagnosticBytes)
        {
            return;
        }

        // Windows filesystem paths cannot contain quotes or control bytes.
        // Normalize separators before passing the path to the JSON report.
        Array<wchar_t> Wide;
        if (ToWide(Path.View(), Wide))
        {
            ToUtf8(Wide.Data(), static_cast<DWORD>(Wide.Size() - 1), RetainedDiagnostic);
        }
    }

    void CleanRequest() noexcept
    {
        if (!RequestDirectory.IsEmpty())
        {
            RemoveRequestFiles(RequestDirectory.View(), !RetainedDiagnostic.IsEmpty());
            if (RetainedDiagnostic.IsEmpty())
            {
                RequestDirectory.Clear();
            }
        }
        Request = 0;
    }

    void Poll(FRenderFrameScheduler& Scheduler, FSceneRenderer& Renderer) noexcept
    {
        if (Request == 0)
        {
            return;
        }
        if (bCancelling)
        {
            PollCancellation();
            return;
        }
        const DWORD Wait = CheckWorkerTree();
        if (Wait == WAIT_TIMEOUT && GetTickCount64() - StartedAt < CompileTimeoutMilliseconds)
        {
            return;
        }
        const uint64 CompletedRequest = Request;
        if (Wait != WAIT_OBJECT_0)
        {
            CancelWorker();
            LogPreview(CompletedRequest, "rejected", "compile", Wait == WAIT_TIMEOUT ? "timeout" : "worker_failed");
            return;
        }

        DWORD ExitCode = ERROR_GEN_FAILURE;
        GetExitCodeProcess(Process.Value, &ExitCode);
        CloseWorker();
        RetainDiagnostic();
        const String IdText = RequestIdText(CompletedRequest);
        const char* Code = ReadWorkerResult(RequestDirectory.View(), IdText.View(), ExitCode);
        if (StringView(Code) != StringView("ok"))
        {
            LogPreview(CompletedRequest, "rejected", "compile", Code, RetainedDiagnostic.Data());
            CleanRequest();
            return;
        }

        String File;
        Array<uint8> Bytes;
        if (ResolveContentFile(RequestDirectory.View(), "candidate.spv", File) != EFileSystemResult::Success ||
            ReadContentFile(File.View(), MaxPreviewShaderBytes, Bytes) != EFileSystemResult::Success || Bytes.IsEmpty())
        {
            LogPreview(CompletedRequest, "rejected", "install", "invalid_spirv", RetainedDiagnostic.Data());
            CleanRequest();
            return;
        }
        const FSceneShaderBytecode Candidate{{reinterpret_cast<const char*>(Bytes.Data()), Bytes.Size()}, "MainPS"};
        const auto Installed = Renderer.InstallBasePassPixelShader(Scheduler, Candidate);
        if (Installed == EBasePassPixelShaderInstallResult::Success)
        {
            PendingPresentation = CompletedRequest;
            LogPreview(CompletedRequest, "installed", "install", "ok", RetainedDiagnostic.Data());
        }
        else
        {
            LogPreview(CompletedRequest, "rejected", "install", InstallError(Installed), RetainedDiagnostic.Data());
        }
        CleanRequest();
    }

    FWinHandle Job;
    FWinHandle Process;
    String SessionDirectory;
    String RequestDirectory;
    String RetainedDiagnostic;
    uint64 StartedAt = 0;
    uint64 CancelledAt = 0;
    uint64 Request = 0;
    uint64 PendingPresentation = 0;
    bool bCancelling = false;
};

FEditorShaderPreview::FEditorShaderPreview() noexcept = default;
FEditorShaderPreview::~FEditorShaderPreview() { Shutdown(); }

void FEditorShaderPreview::Update(bool bRequested, Span<const char> Baseline, FRenderFrameScheduler& Scheduler, FSceneRenderer& Renderer) noexcept
{
    if (bRequested)
    {
        const uint64 BusyRequest = State ? (State->Request != 0 ? State->Request : State->PendingPresentation) : 0;
        if (BusyRequest != 0)
        {
            LogPreview(BusyRequest, "busy", "request", "busy");
        }
        else
        {
            const uint64 Id = ++LastRequest;
            if (!State)
            {
                State = TryMakeUnique<FState>();
            }
            const char* Error = State ? State->Begin(Id, Baseline) : "workspace_failed";
            if (Error != nullptr)
            {
                LogPreview(Id, "rejected", "request", Error);
                if (State && State->RetainedDiagnostic.IsEmpty())
                {
                    State->CleanRequest();
                }
            }
            else
            {
                LogPreview(Id, "started", "request", "accepted", "", GetProcessId(State->Process.Value));
            }
        }
    }
    if (State)
    {
        State->Poll(Scheduler, Renderer);
    }
}

uint64 FEditorShaderPreview::GetPendingPresentation() const noexcept
{
    return State ? State->PendingPresentation : 0;
}

void FEditorShaderPreview::ConfirmPresentation(uint64 RecordedRequest) noexcept
{
    if (State && RecordedRequest != 0 && State->PendingPresentation == RecordedRequest)
    {
        LogPreview(RecordedRequest, "presented", "present", "ok", State->RetainedDiagnostic.Data());
        State->PendingPresentation = 0;
    }
}

void FEditorShaderPreview::Shutdown() noexcept
{
    if (!State)
    {
        return;
    }
    if (State->Request != 0 && !State->bCancelling)
    {
        State->CancelWorker();
        LogPreview(State->Request, "cancelled", "compile", "cancelled");
    }
    if (!State->SessionDirectory.IsEmpty())
    {
        const uint64 StartedAt = GetTickCount64();
        for (;;)
        {
            const bool bExpired = GetTickCount64() - StartedAt >= CleanupTimeoutMilliseconds;
            if (State->CheckWorkerTree() == WAIT_OBJECT_0 || bExpired)
            {
                State->CloseWorker();
                if (RemoveRequestFiles(State->SessionDirectory.View()) || bExpired)
                {
                    break;
                }
            }
            Sleep(15);
        }
    }
    State.Reset();
}

} // namespace LE

#endif
