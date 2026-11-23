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

const CompilerAction = fileURLToPath(new URL("./CompileVulkanShader.ts", import.meta.url));
const ActionSupport = fileURLToPath(import.meta.url);
const BytecodeMagic = 0x07230203;
const BytecodeHeaderBytes = 5 * 4;
const MaxBytecodeBytes = 16 * 1024 * 1024;

export function FindVulkanShaderCompiler(): string | null {
    const IsWindows = process.platform === "win32";
    const Name = IsWindows ? "glslangValidator.exe" : "glslangValidator";
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

function IsWithin(Root: string, File: string): boolean {
    const Relative = Path.relative(Root, File);
    return Relative !== ".." && !Relative.startsWith(`..${Path.sep}`) && !Path.isAbsolute(Relative);
}

export function CollectVulkanShaderIncludes(ShaderRoot: string, ShaderSource: string): string[] {
    const SourceRoot = FsSync.realpathSync(ShaderRoot);
    const Source = FsSync.realpathSync(ShaderSource);
    const Seen = new Set<string>();
    function Visit(File: string): void {
        if (!IsWithin(SourceRoot, File)) {
            throw new Error(`Shader file escapes its source directory: ${File}`);
        }
        if (Seen.has(File)) {
            return;
        }
        Seen.add(File);
        const Text = FsSync.readFileSync(File, "utf-8");
        // Leave quoted tokens intact and blank comments without changing line structure.
        const WithoutComments = Text.replace(
            /"(?:[^"\\\r\n]|\\.)*"|'(?:[^'\\\r\n]|\\.)*'|\/\/[^\r\n]*|\/\*[\s\S]*?\*\//g,
            (Token) => Token.startsWith("/") ? Token.replace(/[^\r\n]/g, " ") : Token,
        );
        for (const Directive of WithoutComments.matchAll(/^[ \t]*#[ \t]*include\b([^\r\n]*)/gm)) {
            const Include = /^[ \t]*"([^"\r\n]+)"[ \t]*$/.exec(Directive[1])?.[1];
            if (!Include || Path.isAbsolute(Include)) {
                throw new Error(`Shader includes must be quoted relative paths in ${File}: ${Directive[0].trim()}`);
            }
            const Candidate = Path.resolve(Path.dirname(File), Include);
            if (!IsWithin(SourceRoot, Candidate)) {
                throw new Error(`Shader include "${Include}" escapes its source directory in ${File}`);
            }
            let Dependency: string;
            try {
                Dependency = FsSync.realpathSync(Candidate);
            } catch (Reason) {
                throw new Error(`Cannot resolve shader include "${Include}" in ${File}`, { cause: Reason });
            }
            Visit(Dependency);
        }
    }
    Visit(Source);
    Seen.delete(Source);
    return [...Seen].sort();
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
        const { size: Size } = await Fs.stat(Candidate);
        if (Size < BytecodeHeaderBytes || Size > MaxBytecodeBytes || (Size % 4) !== 0) {
            throw new Error(`Invalid SPIR-V byte count: ${Size}`);
        }
        const Bytes = await Fs.readFile(Candidate);
        if (Bytes.readUInt32LE(0) !== BytecodeMagic) {
            throw new Error("Invalid SPIR-V magic");
        }
        const Version = Bytes.readUInt32LE(4);
        if ((Version & 0xff0000ff) !== 0 || (Version >>> 16) === 0 || Bytes.readUInt32LE(12) === 0 || Bytes.readUInt32LE(16) !== 0) {
            throw new Error("Invalid SPIR-V header");
        }
        await Fs.rename(Candidate, Destination);
    } finally {
        await Fs.rm(Candidate, { force: true });
        await Fs.rmdir(Scratch);
    }
}
