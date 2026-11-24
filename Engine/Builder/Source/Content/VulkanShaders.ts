import * as FsSync from "node:fs";
import * as Fs from "node:fs/promises";
import * as Path from "node:path";
import { fileURLToPath } from "node:url";
import type { CustomActionDescriptor } from "../Configuration/Types.ts";

export interface VulkanShader {
    readonly Name: string;
    readonly Stage: "vert" | "frag";
    readonly EntryPoint: string;
}

export const BasePassShadowPixelShader = { Name: "BasePassShadowPS", Stage: "frag", EntryPoint: "MainPS" } as const;

const CompilerAction = fileURLToPath(new URL("./CompileVulkanShader.ts", import.meta.url));
const ActionSupport = fileURLToPath(import.meta.url);
const BytecodeMagic = 0x07230203;
const BytecodeHeaderBytes = 5 * 4;
const MaxBytecodeBytes = 16 * 1024 * 1024;

export function FindVulkanShaderCompiler(): string | null {
    return FindVulkanShaderTool("glslangValidator");
}

export function FindVulkanShaderTool(Tool: string): string | null {
    const IsWindows = process.platform === "win32";
    const Name = IsWindows ? `${Tool}.exe` : Tool;
    const Sdk = process.env.VULKAN_SDK;
    const Directories = Sdk && Path.isAbsolute(Sdk) ? [Path.join(Sdk, IsWindows ? "Bin" : "bin")] : [];
    Directories.push(...(process.env.PATH ?? "").split(Path.delimiter).map((Directory) => Directory.replace(/^"(.*)"$/, "$1")));
    if (process.platform === "darwin") {
        Directories.push("/opt/homebrew/bin", "/usr/local/bin");
    }
    for (const Directory of Directories) {
        if (!Path.isAbsolute(Directory)) {
            continue;
        }
        const Candidate = Path.join(Directory, Name);
        try {
            if (FsSync.statSync(Candidate).isFile()) {
                FsSync.accessSync(Candidate, FsSync.constants.X_OK);
                return Candidate;
            }
        } catch {
            // An unavailable SDK or PATH entry does not mask later candidates.
        }
    }
    return null;
}

export function VulkanShaderCompilerArguments(Source: string, Stage: string, EntryPoint: string, Output: string): string[] {
    return ["-V", "-D", "-S", Stage, "-e", EntryPoint, Source, "-o", Output];
}

function IsWithin(Root: string, File: string): boolean {
    const Relative = Path.relative(Root, File);
    return Relative !== ".." && !Relative.startsWith(`..${Path.sep}`) && !Path.isAbsolute(Relative);
}

export function CollectVulkanShaderIncludes(ShaderRoot: string, ShaderSource: string): string[] {
    const Files = WalkVulkanShaderSources(ShaderRoot, ShaderSource, (File) => FsSync.readFileSync(File, "utf-8"));
    return Files.slice(1).sort();
}

export function WalkVulkanShaderSources(
    ShaderRoot: string,
    ShaderSource: string,
    ReadSource: (File: string, Relative: string) => string,
    PreserveRelativePaths = false,
): string[] {
    const ResolveRealPath = PreserveRelativePaths ? FsSync.realpathSync.native : FsSync.realpathSync;
    const SourceRoot = ResolveRealPath(ShaderRoot);
    const Source = ResolveRealPath(ShaderSource);
    const Seen = new Map<string, string>();
    function Visit(File: string, Requested: string): void {
        if (!IsWithin(SourceRoot, File)) {
            throw new Error(`Shader file escapes its source directory: ${File}`);
        }
        const PathInTree = PreserveRelativePaths ? Requested : File;
        const Key = PreserveRelativePaths && process.platform === "win32" ? PathInTree.toLowerCase() : PathInTree;
        const Previous = Seen.get(Key);
        if (Previous !== undefined) {
            if (Previous !== File) {
                throw new Error("Case-conflicting shader include paths: " + Previous + " and " + File);
            }
            return;
        }
        Seen.set(Key, File);
        const Text = ReadSource(File, Path.relative(SourceRoot, PathInTree));
        // Match logical preprocessor lines and leave quoted tokens intact when removing comments.
        const WithoutComments = Text.replace(/^\uFEFF/, " ").replace(/\\\r?\n/g, "").replace(
            /"(?:[^"\\\r\n]|\\.)*"|'(?:[^'\\\r\n]|\\.)*'|\/\/[^\r\n]*|\/\*[\s\S]*?\*\//g,
            (Token) => Token.startsWith("/") ? Token.replace(/[^\r\n]/g, " ") : Token,
        );
        for (const Directive of WithoutComments.matchAll(/^[ \t]*#[ \t]*include\b([^\r\n]*)/gm)) {
            const Include = /^[ \t]*"([^"\r\n]+)"[ \t]*$/.exec(Directive[1])?.[1];
            if (!Include || Path.isAbsolute(Include)) {
                throw new Error(`Shader includes must be quoted relative paths in ${File}: ${Directive[0].trim()}`);
            }
            const Candidate = Path.resolve(Path.dirname(PathInTree), Include);
            if (!IsWithin(SourceRoot, Candidate)) {
                throw new Error(`Shader include "${Include}" escapes its source directory in ${File}`);
            }
            let Dependency: string;
            try {
                Dependency = ResolveRealPath(Candidate);
            } catch (Reason) {
                throw new Error(`Cannot resolve shader include "${Include}" in ${File}`, { cause: Reason });
            }
            Visit(Dependency, Candidate);
        }
    }
    Visit(Source, Path.resolve(SourceRoot, Path.relative(Path.resolve(ShaderRoot), ShaderSource)));
    return [...Seen.values()];
}

export function CreateVulkanShaderActions(ShaderRoot: string, Shaders: readonly VulkanShader[]): CustomActionDescriptor[] {
    const Compiler = FindVulkanShaderCompiler();
    const ToolInputs = Compiler ? [CompilerAction, ActionSupport, Compiler] : [CompilerAction, ActionSupport];
    return Shaders.map(({ Name, Stage, EntryPoint }) => {
        const Source = Path.resolve(ShaderRoot, `${Name}.lsf`);
        const Output = `[engine.Binaries]/Shaders/Vulkan/${Name}.spv`;
        return {
            Id: `CompileShader_${Name}`,
            Inputs: [Source],
            ImplicitInputs: [...ToolInputs, ...CollectVulkanShaderIncludes(ShaderRoot, Source)],
            Outputs: [Output],
            Command: [process.execPath, "--no-warnings", "--experimental-strip-types", CompilerAction,
                Source, Stage, EntryPoint, Output, ...(Compiler ? [Compiler] : [])],
            Description: `SHADER ${Name}`,
        };
    });
}

/** Publishes a complete SPIR-V file on the destination filesystem. */
export async function PublishVulkanShader(Output: string, Compile: (Candidate: string) => void | Promise<void>): Promise<void> {
    const Destination = Path.resolve(Output);
    const Directory = Path.dirname(Destination);
    await Fs.mkdir(Directory, { recursive: true });
    const Scratch = await Fs.mkdtemp(Path.join(Directory, ".shader-"));
    const Candidate = Path.join(Scratch, "shader.spv");
    try {
        await Compile(Candidate);
        await CheckVulkanShaderBytecode(Candidate);
        await Fs.rename(Candidate, Destination);
    } finally {
        await Fs.rm(Candidate, { force: true });
        await Fs.rmdir(Scratch);
    }
}

export async function CheckVulkanShaderBytecode(File: string): Promise<void> {
    const Handle = await Fs.open(File, "r");
    try {
        const { size: Size } = await Handle.stat();
        if (Size < BytecodeHeaderBytes || Size > MaxBytecodeBytes || (Size % 4) !== 0) {
            throw new Error(`Invalid SPIR-V byte count: ${Size}`);
        }
        const Header = Buffer.alloc(BytecodeHeaderBytes);
        const { bytesRead: Read } = await Handle.read(Header, 0, Header.length, 0);
        if (Read !== Header.length || Header.readUInt32LE(0) !== BytecodeMagic) {
            throw new Error("Invalid SPIR-V magic or truncated header");
        }
        const Version = Header.readUInt32LE(4);
        if ((Version & 0xff0000ff) !== 0 || (Version >>> 16) === 0 || Header.readUInt32LE(12) === 0 || Header.readUInt32LE(16) !== 0) {
            throw new Error("Invalid SPIR-V header");
        }
    } finally {
        await Handle.close();
    }
}
