import type { ModuleBuild } from "./ModuleBuild.ts";

export const Platform = {
    Win64: "Win64",
    Mac: "Mac",
} as const;
export type Platform = (typeof Platform)[keyof typeof Platform];

export const Optimization = {
    Debug: "Debug",
    Release: "Release",
} as const;
export type Optimization = (typeof Optimization)[keyof typeof Optimization];

export const TargetType = {
    Game: "Game",
    Editor: "Editor",
} as const;
export type TargetType = (typeof TargetType)[keyof typeof TargetType];

export const OutputType = {
    None: "None",
    Lib: "Lib",
    Dll: "Dll",
    Exe: "Exe",
} as const;
export type OutputType = (typeof OutputType)[keyof typeof OutputType];

export interface Target {
    Platform: Platform;
    Optimization: Optimization;
    TargetType: TargetType;
}

export interface TargetMatrixEntry {
    Platform: Platform;
    Optimization: Optimization;
    TargetType: TargetType;
}

export type DependencyRef = string | { Name: string; WithoutLinking?: boolean };

export interface ModuleConfiguration {
    Output: OutputType;
    PublicDependencies: DependencyRef[];
    PrivateDependencies: DependencyRef[];
    IncludePaths: string[];
    LibraryPaths: string[];
    LibraryFiles: string[];
    SystemFrameworks: string[];
    Defines: Record<string, string>;
    ExportDefines: Record<string, string>;
    SourceFilesExclude: string[];
    SourceFilesExcludeRegex: string[];
    CustomProperties: Record<string, unknown>;
}

export interface ModuleInstance {
    Descriptor: ModuleBuild;
    SourceRoot: string;
    BuildFilePath: string;
}

export interface TargetDescriptor {
    Name: string;
    Modules: string[];
    EntryModule: string;
    Matrix: TargetMatrixEntry[];
    OutputName: (Target: Target) => string;
}

export const ActionType = {
    Compile: "compile",
    Link: "link",
    Archive: "archive",
    Custom: "custom",
} as const;
export type ActionType = (typeof ActionType)[keyof typeof ActionType];

export interface BuildAction {
    Id: string;
    Type: ActionType;
    Inputs: string[];
    Outputs: string[];
    Command: string[];
    WorkingDirectory: string;
    DependsOn: string[];
    Description: string;
    ImplicitInputs?: string[];
}

export interface BuildResult {
    Actions: BuildAction[];
    ModuleCount: number;
    CompileCount: number;
    LinkCount: number;
}
