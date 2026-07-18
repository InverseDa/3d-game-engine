import type { ModuleConfiguration, OutputType, Target } from "./Types.ts";

export abstract class ModuleBuild {
    public abstract readonly Name: string;
    public readonly ThirdParty = false;

    public abstract Configure(Target: Target, Configuration: ModuleConfiguration): void;
}

export function CreateModuleConfiguration(): ModuleConfiguration {
    return {
        Output: "Lib" as OutputType,
        PublicDependencies: [],
        PrivateDependencies: [],
        IncludePaths: [],
        LibraryPaths: [],
        LibraryFiles: [],
        SystemFrameworks: [],
        Defines: {},
        ExportDefines: {},
        SourceFilesExclude: [],
        SourceFilesExcludeRegex: [],
        CustomProperties: {},
    };
}
