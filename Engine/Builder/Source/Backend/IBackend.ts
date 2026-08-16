import type { BuildAction, ResolvedTarget } from "../Configuration/Types.ts";

export interface GenerateOptions {
    OutputDir: string;
    CompileCommandsPath?: string;
    Verbose?: boolean;
}

export interface BuildOptions {
    Jobs?: number;
    Verbose?: boolean;
    /** Optional concrete Ninja output targets. Omitted builds the backend default. */
    Targets?: string[];
}

export interface BackendResult {
    Actions: BuildAction[];
    NinjaPath: string;
    CompileCommandsPath: string;
    CompileCount: number;
    LinkCount: number;
}

export interface IBackend {
    readonly Name: string;
    Generate(Actions: BuildAction[], Target: ResolvedTarget, Opts: GenerateOptions): Promise<BackendResult>;
    Build?(Result: BackendResult, Opts: BuildOptions): Promise<number>;
}
