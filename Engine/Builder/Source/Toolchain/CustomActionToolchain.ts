import type { Target } from "../Configuration/Types.ts";
import type { IToolchain } from "./IToolchain.ts";

function Unreachable(): never {
    throw new Error("The custom-action-only toolchain cannot compile, link, or archive native code.");
}

export class CustomActionToolchain implements IToolchain {
    public readonly Name = "CustomActionOnly";
    public readonly Platform = "Host";

    public FindCompiler(): string { return Unreachable(); }
    public FindLinker(): string { return Unreachable(); }
    public FindArchiver(): string { return Unreachable(); }
    public GetSystemIncludePaths(): string[] { return []; }
    public GetSystemLibPaths(): string[] { return []; }
    public MakeCompileCommand(
        _SourceFile: string, _OutputFile: string, _IncludePaths: string[],
        _Defines: Record<string, string>, _Target: Target,
    ): string[] { return Unreachable(); }
    public MakeLinkCommand(
        _OutputFile: string, _InputObjects: string[], _LibraryFiles: string[],
        _LibraryPaths: string[], _Target: Target, _IsDll: boolean,
    ): string[] { return Unreachable(); }
    public MakeArchiveCommand(_OutputFile: string, _InputObjects: string[]): string[] {
        return Unreachable();
    }
    public async GetEnvironment(): Promise<Record<string, string>> {
        return { ...process.env } as Record<string, string>;
    }
}
