import { spawnSync } from "node:child_process";
import * as Fs from "node:fs/promises";
import * as Path from "node:path";
import {
    BasePassShadowPixelShader, CheckVulkanShaderBytecode, FindVulkanShaderCompiler,
    FindVulkanShaderTool, VulkanShaderCompilerArguments,
} from "./VulkanShaders.ts";
import { NormalizeShaderDeclarations, NormalizeShaderReflection, UnsupportedShaderInterface } from "./VulkanShaderPreviewAbi.ts";
import { CaptureShaderInputs, ShaderInputsMatch, ShaderSourceChanged, WriteShaderSnapshot } from "./VulkanShaderPreviewSources.ts";

interface SourceDiagnostic {
    readonly file?: string;
    readonly line?: number;
    readonly column?: number;
    readonly message: string;
}

interface PreviewResult {
    readonly stage: string;
    readonly code: string;
    readonly message: string;
    readonly diagnostics: SourceDiagnostic[];
    readonly toolOutput?: string;
}

const ToolOutputLimit = 512 * 1024;
const RetainedToolChars = 8 * 1024;
const MaxDiagnostics = 16;
const MaxDiagnosticBytes = 64 * 1024;
const MaxRequestId = 18446744073709551615n;
const SourceName = BasePassShadowPixelShader.Name + ".lsf";
const { Stage, EntryPoint } = BasePassShadowPixelShader;

class PreviewFailure extends Error {
    readonly Result: PreviewResult;

    constructor(Stage: string, Code: string, Message: string, Diagnostics: SourceDiagnostic[] = [], ToolOutput?: string) {
        super(Message);
        this.Result = { stage: Stage, code: Code, message: Message, diagnostics: Diagnostics, toolOutput: ToolOutput };
    }
}

function ErrorMessage(Reason: unknown): string {
    return (Reason instanceof Error ? Reason.message : String(Reason)).slice(0, 1024);
}

function RunTool(Tool: string, Args: string[], Directory: string, Stage: string, Code: string) {
    const Result = spawnSync(Tool, Args, {
        cwd: Directory, encoding: "utf8", maxBuffer: ToolOutputLimit, windowsHide: true, shell: false,
    });
    if (Result.error) {
        const ErrorCode = (Result.error as NodeJS.ErrnoException).code;
        const FailedCode = ErrorCode === "ENOENT" || ErrorCode === "EACCES" ? "tools_unavailable" : Code;
        throw new PreviewFailure(Stage, FailedCode, Path.basename(Tool) + ": " + ErrorMessage(Result.error));
    }
    return Result;
}

function CompilerDiagnostics(Output: string, Root: string, Sources: ReadonlyMap<string, string>): SourceDiagnostic[] {
    const Diagnostics: SourceDiagnostic[] = [];
    const ByPath = process.platform === "win32" ? new Map([...Sources].map(([File, Source]) => [File.toLowerCase(), Source])) : Sources;
    for (const Line of Output.split(/\r?\n/)) {
        const Match = /^(?:ERROR|WARNING):\s+(.+?):(\d+)(?::(\d+))?:\s*(.*)$/.exec(Line);
        if (!Match || Diagnostics.length >= MaxDiagnostics) {
            continue;
        }
        const [, File, LineNumber, Column, Message] = Match;
        const Candidate = Path.resolve(Root, File.replace(/[\\/]/g, Path.sep));
        const Source = ByPath.get(process.platform === "win32" ? Candidate.toLowerCase() : Candidate);
        if (Source) {
            Diagnostics.push({ file: Source, line: Number(LineNumber), column: Column ? Number(Column) : undefined, message: Message.slice(0, 512) });
        }
    }
    return Diagnostics;
}

function RemapCompilerOutput(Output: string, Sources: ReadonlyMap<string, string>): string {
    let Text = Output.slice(0, RetainedToolChars);
    for (const [Snapshot, Source] of Sources) {
        Text = Text.replaceAll(Snapshot, () => Source).replaceAll(Snapshot.replaceAll("\\", "/"), () => Source);
    }
    return Text.slice(0, RetainedToolChars);
}

async function ReadAbi(File: string, Baseline: boolean, Directory: string, Validate: string, Reflect: string, Disassemble: string) {
    const Stage = Baseline ? "baseline" : "candidate";
    const Invalid = Baseline ? "baseline_invalid" : "invalid_spirv";
    try {
        await CheckVulkanShaderBytecode(File);
    } catch (Reason) {
        throw new PreviewFailure(Stage, Invalid, ErrorMessage(Reason));
    }
    const Validated = RunTool(Validate, ["--target-env", "vulkan1.0", File], Directory, Stage, Invalid);
    if (Validated.status !== 0) {
        throw new PreviewFailure(Stage, Invalid, "SPIR-V validation failed", [], Validated.stderr.slice(0, RetainedToolChars));
    }
    const Reflected = RunTool(Reflect, [File, "--reflect"], Directory, Stage, "reflection_failed");
    const Disassembled = RunTool(Disassemble, [File, "--no-header", "--raw-id"], Directory, Stage, "reflection_failed");
    if (Reflected.status !== 0 || Disassembled.status !== 0) {
        throw new PreviewFailure(Stage, "reflection_failed", "SPIR-V interface inspection failed", [],
            (Reflected.stderr + Disassembled.stderr).slice(0, RetainedToolChars));
    }
    try {
        return {
            Reflection: NormalizeShaderReflection(JSON.parse(Reflected.stdout), EntryPoint),
            Declarations: NormalizeShaderDeclarations(Disassembled.stdout, EntryPoint),
        };
    } catch (Reason) {
        const Code = Reason instanceof UnsupportedShaderInterface
            ? (Baseline ? "baseline_invalid" : "abi_incompatible") : "reflection_failed";
        throw new PreviewFailure(Stage, Code, ErrorMessage(Reason));
    }
}

async function CompilePreview(Checkout: string, Request: string, Scratch: string): Promise<PreviewResult> {
    const Compiler = FindVulkanShaderCompiler();
    const Validate = FindVulkanShaderTool("spirv-val");
    const Reflect = FindVulkanShaderTool("spirv-cross");
    const Disassemble = FindVulkanShaderTool("spirv-dis");
    if (!Compiler || !Validate || !Reflect || !Disassemble) {
        const Missing = [[Compiler, "glslangValidator"], [Validate, "spirv-val"], [Reflect, "spirv-cross"], [Disassemble, "spirv-dis"]]
            .filter(([Tool]) => !Tool).map(([, Name]) => Name);
        throw new PreviewFailure("tools", "tools_unavailable", "Vulkan SDK tools missing: " + Missing.join(", "));
    }
    const Baseline = await ReadAbi(Path.join(Request, "baseline.spv"), true, Scratch, Validate, Reflect, Disassemble);
    const SourceRoot = Path.join(Checkout, "Engine", "Content", "Shaders");
    const Snapshot = Path.join(Scratch, "Sources");
    let Inputs: ReturnType<typeof CaptureShaderInputs>;
    let Sources: ReadonlyMap<string, string>;
    try {
        Inputs = CaptureShaderInputs(SourceRoot);
        Sources = WriteShaderSnapshot(Snapshot, Inputs);
    } catch (Reason) {
        throw new PreviewFailure("snapshot", Reason instanceof ShaderSourceChanged ? "source_changed" : "source_error", ErrorMessage(Reason));
    }

    function VerifyInputs(): void {
        try {
            if (ShaderInputsMatch(SourceRoot, Inputs)) {
                return;
            }
        } catch (Reason) {
            throw new PreviewFailure("snapshot", "source_changed", ErrorMessage(Reason));
        }
        throw new PreviewFailure("snapshot", "source_changed", "Shader source changed during preview");
    }

    VerifyInputs();
    const Candidate = Path.join(Scratch, "compiled.spv");
    let Output = "";
    let Diagnostics: SourceDiagnostic[] = [];
    try {
        const Args = VulkanShaderCompilerArguments(Path.join(Snapshot, SourceName), Stage, EntryPoint, Candidate);
        const Compiled = RunTool(Compiler, Args, Snapshot, "compile", "compile_failed");
        const Combined = Compiled.stdout + Compiled.stderr;
        Diagnostics = CompilerDiagnostics(Combined, Snapshot, Sources);
        Output = RemapCompilerOutput(Combined, Sources);
        if (Compiled.status !== 0) {
            throw new PreviewFailure("compile", "compile_failed", "glslangValidator compilation failed", Diagnostics, Output);
        }
        const Preview = await ReadAbi(Candidate, false, Scratch, Validate, Reflect, Disassemble);
        if (Baseline.Reflection !== Preview.Reflection || Baseline.Declarations !== Preview.Declarations) {
            throw new PreviewFailure("candidate", "abi_incompatible", "Candidate does not match the startup BasePass pixel shader interface",
                Diagnostics, Output);
        }
    } finally {
        VerifyInputs();
    }
    await Fs.rename(Candidate, Path.join(Request, "candidate.spv"));
    return { stage: "ready", code: "ok", message: "BasePass pixel shader candidate validated", diagnostics: Diagnostics, toolOutput: Output };
}

async function PublishResult(Request: string, RequestId: string, Result: PreviewResult): Promise<void> {
    let Json = JSON.stringify({ requestId: RequestId, ...Result });
    if (Buffer.byteLength(Json) >= MaxDiagnosticBytes) {
        Json = JSON.stringify({ requestId: RequestId, ...Result, toolOutput: undefined });
    }
    if (Buffer.byteLength(Json) >= MaxDiagnosticBytes) {
        Json = JSON.stringify({ requestId: RequestId, ...Result, diagnostics: [], toolOutput: undefined });
    }
    await Fs.writeFile(Path.join(Request, "diagnostics.json"), Json + "\n", { flag: "wx" });
    // A consumer can trust the marker only after the process exits. Publish it last.
    const Marker = Path.join(Request, "result.pending");
    await Fs.writeFile(Marker, RequestId + "\n" + Result.code + "\n", { flag: "wx" });
    await Fs.rename(Marker, Path.join(Request, "result.txt"));
    process.exitCode = Result.code === "ok" ? 0 : 1;
}

async function Main(): Promise<void> {
    const [Checkout, Request, RequestId, ...Extra] = process.argv.slice(2);
    if (!Checkout || !Request || !Path.isAbsolute(Checkout) || !Path.isAbsolute(Request) ||
        !/^[1-9][0-9]{0,19}$/.test(RequestId) || BigInt(RequestId) > MaxRequestId || Extra.length !== 0) {
        throw new Error("Usage: PreviewVulkanShader CHECKOUT REQUEST_DIRECTORY REQUEST_ID");
    }
    const Scratch = await Fs.mkdtemp(Path.join(Request, ".shader-preview-"));
    let Result: PreviewResult;
    try {
        Result = await CompilePreview(Checkout, Request, Scratch);
    } catch (Reason) {
        Result = Reason instanceof PreviewFailure ? Reason.Result
            : { stage: "worker", code: "worker_failed", message: ErrorMessage(Reason), diagnostics: [] };
    } finally {
        await Fs.rm(Scratch, { recursive: true, force: true });
    }
    await PublishResult(Request, RequestId, Result);
}

try {
    await Main();
} catch (Reason) {
    console.error(ErrorMessage(Reason));
    process.exitCode = 1;
}
