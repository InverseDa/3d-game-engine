import { createHash } from "node:crypto";
import * as Fs from "node:fs";
import * as Path from "node:path";
import { BasePassShadowPixelShader, WalkVulkanShaderSources } from "./VulkanShaders.ts";

const MaxSourceFiles = 128;
const MaxSourceFileBytes = 1024 * 1024;
const MaxSourceBytes = 8 * MaxSourceFileBytes;

export class ShaderSourceChanged extends Error {}

interface ShaderInput {
    readonly File: string;
    readonly Relative: string;
    readonly Bytes: Buffer;
    readonly Digest: string;
}

function ReadSourceFile(File: string): Buffer {
    const Handle = Fs.openSync(File, "r");
    try {
        const Stat = Fs.fstatSync(Handle);
        if (!Stat.isFile() || Stat.size > MaxSourceFileBytes) {
            throw new Error("Invalid or oversized shader source: " + File);
        }
        const Bytes = Buffer.alloc(Stat.size + 1);
        let Count = 0;
        while (Count < Bytes.length) {
            const Read = Fs.readSync(Handle, Bytes, Count, Bytes.length - Count, null);
            if (Read === 0) {
                break;
            }
            Count += Read;
        }
        if (Count !== Stat.size) {
            throw new ShaderSourceChanged("Shader source changed while reading: " + File);
        }
        return Bytes.subarray(0, Count);
    } finally {
        Fs.closeSync(Handle);
    }
}

export function CaptureShaderInputs(Root: string): ShaderInput[] {
    const EntryFile = Path.join(Root, BasePassShadowPixelShader.Name + ".lsf");
    const Inputs: ShaderInput[] = [];
    let TotalBytes = 0;
    WalkVulkanShaderSources(Root, EntryFile, (File, Relative) => {
        if (Inputs.length >= MaxSourceFiles) {
            throw new Error("Shader source exceeds the preview file count limit");
        }
        const Bytes = ReadSourceFile(File);
        TotalBytes += Bytes.length;
        if (TotalBytes > MaxSourceBytes) {
            throw new Error("Shader source exceeds the preview byte limit");
        }
        Inputs.push({ File, Relative, Bytes, Digest: createHash("sha256").update(Bytes).digest("hex") });
        return Bytes.toString("utf8");
    }, true);
    return Inputs;
}

export function ShaderInputsMatch(Root: string, Initial: readonly ShaderInput[]): boolean {
    const Expected = new Map(Initial.map((Input) => [Input.Relative, Input]));
    const Current = CaptureShaderInputs(Root);
    return Current.length === Initial.length && Current.every((Input) => {
        const Previous = Expected.get(Input.Relative);
        return Previous !== undefined && Input.File === Previous.File && Input.Digest === Previous.Digest;
    });
}

export function WriteShaderSnapshot(Root: string, Inputs: readonly ShaderInput[]): ReadonlyMap<string, string> {
    const Sources = new Map<string, string>();
    for (const Input of Inputs) {
        const Copy = Path.join(Root, Input.Relative);
        Fs.mkdirSync(Path.dirname(Copy), { recursive: true });
        Fs.writeFileSync(Copy, Input.Bytes, { flag: "wx" });
        Sources.set(Copy, Input.File);
    }
    return Sources;
}
