import type { Target } from "../Configuration/Types.ts";

export interface IToolchain {
    readonly Name: string;
    readonly Platform: string;

    FindCompiler(): string;
    FindLinker(): string;
    FindArchiver(): string;
    GetSystemIncludePaths(): string[];
    GetSystemLibPaths(): string[];

    MakeCompileCommand(
        SourceFile: string,
        OutputFile: string,
        IncludePaths: string[],
        Defines: Record<string, string>,
        Target: Target,
    ): string[];

    MakeLinkCommand(
        OutputFile: string,
        InputObjects: string[],
        LibraryFiles: string[],
        LibraryPaths: string[],
        Target: Target,
        IsDll: boolean,
    ): string[];

    /** Additional files emitted by one linker invocation (for example an MSVC DLL import library). */
    GetLinkOutputs?(OutputFile: string, IsDll: boolean): string[];

    MakeArchiveCommand(OutputFile: string, InputObjects: string[]): string[];
    GetEnvironment(): Promise<Record<string, string>>;
}
