import * as Path from "node:path";
import * as Fs from "node:fs";
import type {
    BuildAction,
    Target,
    OutputType,
    DependencyRef,
} from "../Configuration/Types.ts";
import { OutputType as OutputTypeEnum } from "../Configuration/Types.ts";
import type { ResolvedModule, DependencyGraph } from "../Graph/DependencyGraph.ts";
import type { IToolchain } from "../Toolchain/IToolchain.ts";
import { SourceScanner } from "./SourceScanner.ts";
import type { EnginePaths } from "../Project/EnginePaths.ts";

export class IRBuilder {
    private readonly Actions: BuildAction[] = [];
    private readonly Scanner = new SourceScanner();
    private readonly Paths: EnginePaths;
    private readonly Toolchain: IToolchain;
    private readonly Target: Target;
    private readonly Graph: DependencyGraph;

    constructor(
        Paths: EnginePaths,
        Toolchain: IToolchain,
        Target: Target,
        Graph: DependencyGraph,
    ) {
        this.Paths = Paths;
        this.Toolchain = Toolchain;
        this.Target = Target;
        this.Graph = Graph;
    }

    async Build(Modules: ResolvedModule[]): Promise<BuildAction[]> {
        for (const Module of Modules) {
            await this.BuildModule(Module);
        }
        this.BuildExeTarget(Modules);
        return this.Actions;
    }

    private BuildExeTarget(Modules: ResolvedModule[]): void {
        const ExeName = this.Target.TargetType === "Editor" ? "LimitlessEditor" : "LimitlessGame";
        const BinaryDir = this.Paths.BinaryOutputDirectory(this.Target.Platform);
        const ExePath = Path.join(BinaryDir, ExeName + ".exe");

        const LibFiles: string[] = [];
        const ObjFiles: string[] = [];
        const DepIds: string[] = [];
        for (const Action of this.Actions) {
            if (Action.Type === "compile" && Action.Id.startsWith("Launch::compile::")) {
                ObjFiles.push(Action.Outputs[0]);
                DepIds.push(Action.Id);
            } else if (Action.Type === "archive" || Action.Type === "link") {
                if (!Action.Id.startsWith("Launch::")) {
                    LibFiles.push(Action.Outputs[0]);
                }
                DepIds.push(Action.Id);
            }
        }
        LibFiles.reverse();

        const ExternalLibs: string[] = [];
        const LibPaths: string[] = [BinaryDir];
        for (const Module of Modules) {
            ExternalLibs.push(...Module.Configuration.LibraryFiles);
            for (const Lp of Module.Configuration.LibraryPaths) {
                if (!LibPaths.includes(Lp)) LibPaths.push(Lp);
            }
        }
        LibPaths.push(...this.Toolchain.GetSystemLibPaths());

        const SystemLibs = this.Target.Platform === "Win64"
            ? ["user32.lib", "gdi32.lib", "shell32.lib", "ole32.lib"]
            : [];

        const Command = this.Toolchain.MakeLinkCommand(
            ExePath, ObjFiles, [...LibFiles, ...ExternalLibs, ...SystemLibs], LibPaths, this.Target, false,
        );

        this.Actions.push({
            Id: "LimitlessEngine::exe",
            Type: "link",
            Inputs: [...ObjFiles, ...LibFiles],
            Outputs: [ExePath],
            Command: [this.Toolchain.FindLinker(), ...Command],
            WorkingDirectory: this.Paths.Root,
            DependsOn: DepIds,
            Description: `EXE ${ExeName}`,
        });
    }

    private async BuildModule(Module: ResolvedModule): Promise<void> {
        const Name = Module.Instance.Descriptor.Name;
        const Conf = Module.Configuration;
        const SourceRoot = Module.Instance.SourceRoot;

        if (Conf.Output === OutputTypeEnum.None) return;

        const SourceFiles = await this.Scanner.Scan(SourceRoot, Conf);
        if (SourceFiles.length === 0) return;

        const IncludePaths = this.CollectIncludePaths(Module);
        const Defines = this.CollectDefines(Module);
        const TempDir = this.Paths.TemporaryOutputDirectory(this.Target.Platform, this.Target.Optimization);
        const ModuleTempDir = Path.join(TempDir, Name);

        const ObjectFiles: string[] = [];
        for (const Source of SourceFiles) {
            const RelPath = Path.relative(SourceRoot, Source).replace(/\\/g, "/");
            const ObjName = this.MakeObjName(RelPath);
            const ObjPath = Path.join(ModuleTempDir, ObjName);
            const Command = this.Toolchain.MakeCompileCommand(
                Source, ObjPath, IncludePaths, Defines, this.Target,
            );
            const ActionId = `${Name}::compile::${RelPath}`;
            this.Actions.push({
                Id: ActionId,
                Type: "compile",
                Inputs: [Source],
                Outputs: [ObjPath],
                Command: [this.Toolchain.FindCompiler(), ...Command],
                WorkingDirectory: this.Paths.Root,
                DependsOn: [],
                Description: `CC ${Name}/${RelPath}`,
            });
            ObjectFiles.push(ObjPath);
        }

        if (ObjectFiles.length === 0) return;
        await this.BuildOutputAction(Module, ObjectFiles);
    }

    private async BuildOutputAction(
        Module: ResolvedModule,
        ObjectFiles: string[],
    ): Promise<void> {
        const Name = Module.Instance.Descriptor.Name;
        const Conf = Module.Configuration;
        const BinaryDir = this.Paths.BinaryOutputDirectory(this.Target.Platform);
        const Ext = Conf.Output === OutputTypeEnum.Dll ? ".dll"
                            : Conf.Output === OutputTypeEnum.Exe ? ".exe"
                            : ".lib";
        const OutputPath = Path.join(BinaryDir, Name + Ext);

        if (Conf.Output === OutputTypeEnum.Lib) {
            const Command = this.Toolchain.MakeArchiveCommand(OutputPath, ObjectFiles);
            this.Actions.push({
                Id: `${Name}::archive`,
                Type: "archive",
                Inputs: ObjectFiles,
                Outputs: [OutputPath],
                Command: [this.Toolchain.FindArchiver(), ...Command],
                WorkingDirectory: this.Paths.Root,
                DependsOn: this.CompileActionIds(Name, ObjectFiles.length),
                Description: `LIB ${Name}`,
            });
        } else {
            const LibFiles = this.CollectLibraryFiles(Module);
            const LibPaths = this.CollectLibraryPaths(Module);
            const Command = this.Toolchain.MakeLinkCommand(
                OutputPath, ObjectFiles, LibFiles, LibPaths,
                this.Target, Conf.Output === OutputTypeEnum.Dll,
            );
            this.Actions.push({
                Id: `${Name}::link`,
                Type: "link",
                Inputs: ObjectFiles,
                Outputs: [OutputPath],
                Command: [this.Toolchain.FindLinker(), ...Command],
                WorkingDirectory: this.Paths.Root,
                DependsOn: this.CompileActionIds(Name, ObjectFiles.length),
                Description: `${Conf.Output === OutputTypeEnum.Dll ? "DLL" : "EXE"} ${Name}`,
            });
        }
    }

    private CompileActionIds(ModuleName: string, Count: number): string[] {
        return this.Actions
            .filter((A) => A.Id.startsWith(`${ModuleName}::compile::`))
            .map((A) => A.Id);
    }

    private MakeObjName(RelPath: string): string {
        return RelPath.replace(/[\\/]/g, "_").replace(/\.(cpp|cc|c|mm|m)$/i, ".obj");
    }

    private ResolvePathVar(P: string, SourceRoot: string): string {
        return Path.normalize(P
            .replace("[module.SourceRoot]", SourceRoot)
            .replace("[project.SourceRootPath]", SourceRoot)
            .replace("[engine.Root]", this.Paths.Root)
            .replace("[engine.Source]", this.Paths.SourceDirectory));
    }

    private CollectIncludePaths(Module: ResolvedModule): string[] {
        const Result: string[] = [];
        const Conf = Module.Configuration;
        const SourceRoot = Module.Instance.SourceRoot;

        for (const Inc of Conf.IncludePaths) {
            Result.push(this.ResolvePathVar(Inc, SourceRoot));
        }
        const PublicDir = Path.join(SourceRoot, "Public");
        const PrivateDir = Path.join(SourceRoot, "Private");
        if (Fs.existsSync(PublicDir)) Result.push(PublicDir);
        if (Fs.existsSync(PrivateDir)) Result.push(PrivateDir);

        for (const Dep of this.Graph.GetTransitiveExportDefines(Module) ? [] : []) {
            // placeholder for transitive include path collection
        }

        const Transitive = this.CollectTransitiveIncludePaths(Module);
        Result.push(...Transitive);

        Result.push(...this.Toolchain.GetSystemIncludePaths());
        return Result;
    }

    private CollectTransitiveIncludePaths(Module: ResolvedModule): string[] {
        const Result: string[] = [];
        const Visited = new Set<string>();
        const Visit = (M: ResolvedModule) => {
            for (const Dep of M.Configuration.PublicDependencies) {
                const Name = typeof Dep === "string" ? Dep : Dep.Name;
                if (Visited.has(Name)) continue;
                Visited.add(Name);
                const Resolved = this.Graph.Get(Name);
                if (!Resolved) continue;
                const DepRoot = Resolved.Instance.SourceRoot;
                for (const Inc of Resolved.Configuration.IncludePaths) {
                    Result.push(this.ResolvePathVar(Inc, DepRoot));
                }
                const PubDir = Path.join(DepRoot, "Public");
                if (Fs.existsSync(PubDir)) Result.push(PubDir);
                Visit(Resolved);
            }
        };
        Visit(Module);
        return Result;
    }

    private CollectDefines(Module: ResolvedModule): Record<string, string> {
        const Result: Record<string, string> = {};
        const Transitive = this.Graph.GetTransitiveExportDefines(Module);
        Object.assign(Result, Transitive);
        Object.assign(Result, Module.Configuration.Defines);

        if (this.Target.Platform === "Win64") {
            Result.PLATFORM_WINDOWS = "1";
        } else if (this.Target.Platform === "Mac") {
            Result.PLATFORM_MAC = "1";
        }
        Result.WITH_EDITOR = this.Target.TargetType === "Editor" ? "1" : "0";
        return Result;
    }

    private CollectLibraryFiles(Module: ResolvedModule): string[] {
        const Result: string[] = [...Module.Configuration.LibraryFiles];
        const LinkDeps = this.Graph.GetLinkDependencies(Module);
        for (const DepName of LinkDeps) {
            Result.push(`${DepName}.lib`);
        }
        return Result;
    }

    private CollectLibraryPaths(Module: ResolvedModule): string[] {
        const Result: string[] = [...Module.Configuration.LibraryPaths];
        Result.push(this.Paths.BinaryOutputDirectory(this.Target.Platform));
        Result.push(...this.Toolchain.GetSystemLibPaths());
        return Result;
    }
}
