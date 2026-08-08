import * as Path from "node:path";
import * as Fs from "node:fs";
import type {
    BuildAction,
    CustomActionDescriptor,
    ResolvedTarget,
    OutputType,
    DependencyRef,
} from "../Configuration/Types.ts";
import { OutputType as OutputTypeEnum } from "../Configuration/Types.ts";
import type { ResolvedModule, DependencyGraph } from "../Graph/DependencyGraph.ts";
import type { IToolchain } from "../Toolchain/IToolchain.ts";
import { IsCompilableSource, SourceScanner } from "./SourceScanner.ts";
import { ValidateAndSortActions } from "./ActionGraph.ts";
import type { EnginePaths } from "../Project/EnginePaths.ts";

export class IRBuilder {
    private readonly Actions: BuildAction[] = [];
    private readonly Scanner = new SourceScanner();
    private readonly Paths: EnginePaths;
    private readonly Toolchain: IToolchain;
    private readonly BuildTarget: ResolvedTarget;
    private readonly Graph: DependencyGraph;

    constructor(
        Paths: EnginePaths,
        Toolchain: IToolchain,
        BuildTarget: ResolvedTarget,
        Graph: DependencyGraph,
    ) {
        this.Paths = Paths;
        this.Toolchain = Toolchain;
        this.BuildTarget = BuildTarget;
        this.Graph = Graph;
    }

    async Build(Modules: ResolvedModule[]): Promise<BuildAction[]> {
        this.Actions.length = 0;
        for (const Module of Modules) {
            await this.BuildModule(Module);
        }
        this.BuildExeTarget(Modules);
        return ValidateAndSortActions(this.Actions);
    }

    private BuildExeTarget(Modules: ResolvedModule[]): void {
        const EntryModule = this.BuildTarget.Descriptor.EntryModule;
        const ExeName = this.BuildTarget.OutputName;
        const Target = this.BuildTarget.Target;
        const BinaryDir = this.Paths.BinaryOutputDirectory(Target.Platform);
        const ExePath = Path.join(BinaryDir, ExeName + ".exe");

        const LibFiles: string[] = [];
        const ObjFiles: string[] = [];
        const DepIds: string[] = [];
        for (const Action of this.Actions) {
            if (Action.Type === "compile" && Action.Id.startsWith(`${EntryModule}::compile::`)) {
                ObjFiles.push(Action.Outputs[0]);
                DepIds.push(Action.Id);
            } else if (Action.Type === "archive" || Action.Type === "link") {
                if (!Action.Id.startsWith(`${EntryModule}::`)) {
                    LibFiles.push(Action.Outputs[0]);
                }
                DepIds.push(Action.Id);
            } else if (Action.Type === "custom") {
                DepIds.push(Action.Id);
            }
        }
        LibFiles.reverse();

        if (ObjFiles.length === 0) {
            throw new Error(
                `Target "${this.BuildTarget.Descriptor.Name}" entry module "${EntryModule}" produced no compiled object files.`,
            );
        }

        const ExternalLibs: string[] = [];
        const LibPaths: string[] = [BinaryDir];
        for (const Module of Modules) {
            ExternalLibs.push(...Module.Configuration.LibraryFiles);
            for (const Lp of Module.Configuration.LibraryPaths) {
                if (!LibPaths.includes(Lp)) LibPaths.push(Lp);
            }
        }
        LibPaths.push(...this.Toolchain.GetSystemLibPaths());

        const SystemLibs = Target.Platform === "Win64"
            ? ["user32.lib", "gdi32.lib", "shell32.lib", "ole32.lib"]
            : [];

        const Command = this.Toolchain.MakeLinkCommand(
            ExePath, ObjFiles, [...LibFiles, ...ExternalLibs, ...SystemLibs], LibPaths, Target, false,
        );

        this.Actions.push({
            Id: `${this.BuildTarget.Descriptor.Name}::exe`,
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

        const CustomActions = this.BuildCustomActions(Module);
        if (Conf.Output === OutputTypeEnum.None) return;

        const ScannedSources = await this.Scanner.Scan(SourceRoot, Conf);
        const GeneratedSources = CustomActions
            .flatMap((Action) => Action.Outputs)
            .filter(IsCompilableSource);
        const SourceFiles = [...new Set([...ScannedSources, ...GeneratedSources])];
        if (SourceFiles.length === 0) return;

        const IncludePaths = this.CollectIncludePaths(Module);
        const Defines = this.CollectDefines(Module);
        const TempDir = this.Paths.TemporaryOutputDirectory(
            this.BuildTarget.Target.Platform,
            this.BuildTarget.Target.Optimization,
        );
        const ModuleTempDir = Path.join(TempDir, Name);
        const ProducerByOutput = new Map<string, BuildAction>();
        for (const Action of CustomActions) {
            for (const Output of Action.Outputs) ProducerByOutput.set(this.PathKey(Output), Action);
        }
        const BeforeCompile = CustomActions.filter((_Action, Index) => Conf.CustomActions[Index]?.RunBeforeCompile);

        const ObjectFiles: string[] = [];
        for (const Source of SourceFiles) {
            const RelPath = this.SourceIdentity(SourceRoot, Source);
            const ObjName = this.MakeObjName(RelPath);
            const ObjPath = Path.join(ModuleTempDir, ObjName);
            const Command = this.Toolchain.MakeCompileCommand(
                Source, ObjPath, IncludePaths, Defines, this.BuildTarget.Target,
            );
            const ActionId = `${Name}::compile::${RelPath}`;
            const Producer = ProducerByOutput.get(this.PathKey(Source));
            const CompileDependencies = new Set(BeforeCompile.map((Action) => Action.Id));
            if (Producer) CompileDependencies.add(Producer.Id);
            const ImplicitInputs = [...new Set(BeforeCompile.flatMap((Action) => Action.Outputs))]
                .filter((Input) => this.PathKey(Input) !== this.PathKey(Source));
            this.Actions.push({
                Id: ActionId,
                Type: "compile",
                Inputs: [Source],
                Outputs: [ObjPath],
                Command: [this.Toolchain.FindCompiler(), ...Command],
                WorkingDirectory: this.Paths.Root,
                DependsOn: [...CompileDependencies],
                Description: `CC ${Name}/${RelPath}`,
                ImplicitInputs,
            });
            ObjectFiles.push(ObjPath);
        }

        if (ObjectFiles.length === 0) return;
        if (Name !== this.BuildTarget.Descriptor.EntryModule) {
            await this.BuildOutputAction(Module, ObjectFiles);
        }
    }

    private async BuildOutputAction(
        Module: ResolvedModule,
        ObjectFiles: string[],
    ): Promise<void> {
        const Name = Module.Instance.Descriptor.Name;
        const Conf = Module.Configuration;
        const BinaryDir = this.Paths.BinaryOutputDirectory(this.BuildTarget.Target.Platform);
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
                DependsOn: this.CompileActionIds(Name),
                Description: `LIB ${Name}`,
            });
        } else {
            const LibFiles = this.CollectLibraryFiles(Module);
            const LibPaths = this.CollectLibraryPaths(Module);
            const Command = this.Toolchain.MakeLinkCommand(
                OutputPath, ObjectFiles, LibFiles, LibPaths,
                this.BuildTarget.Target, Conf.Output === OutputTypeEnum.Dll,
            );
            this.Actions.push({
                Id: `${Name}::link`,
                Type: "link",
                Inputs: ObjectFiles,
                Outputs: [OutputPath],
                Command: [this.Toolchain.FindLinker(), ...Command],
                WorkingDirectory: this.Paths.Root,
                DependsOn: this.CompileActionIds(Name),
                Description: `${Conf.Output === OutputTypeEnum.Dll ? "DLL" : "EXE"} ${Name}`,
            });
        }
    }

    private CompileActionIds(ModuleName: string): string[] {
        return this.Actions
            .filter((A) => A.Id.startsWith(`${ModuleName}::compile::`))
            .map((A) => A.Id);
    }

    private MakeObjName(RelPath: string): string {
        const Stem = RelPath.replace(/[^A-Za-z0-9_.-]/g, "_")
            .replace(/\.(cpp|cc|c|mm|m)$/i, "")
            .slice(0, 96);
        let Hash = 2166136261;
        for (const Character of RelPath) {
            Hash ^= Character.charCodeAt(0);
            Hash = Math.imul(Hash, 16777619);
        }
        return `${Stem}_${(Hash >>> 0).toString(16).padStart(8, "0")}.obj`;
    }

    private BuildCustomActions(Module: ResolvedModule): BuildAction[] {
        const Name = Module.Instance.Descriptor.Name;
        const SourceRoot = Module.Instance.SourceRoot;
        const Result: BuildAction[] = [];
        for (const Descriptor of Module.Configuration.CustomActions) {
            this.ValidateCustomDescriptor(Name, Descriptor);
            const Id = `${Name}::custom::${Descriptor.Id}`;
            const Outputs = Descriptor.Outputs.map((Output) => {
                const Resolved = this.ResolvePath(Output, SourceRoot, Name);
                if (!this.IsAllowedCustomOutput(Resolved)) {
                    throw new Error(`Custom action "${Id}" output escapes engine generated/temp/binaries roots: ${Resolved}`);
                }
                return Resolved;
            });
            const Action: BuildAction = {
                Id,
                Type: "custom",
                Inputs: Descriptor.Inputs.map((Input) => this.ResolvePath(Input, SourceRoot, Name)),
                Outputs,
                Command: Descriptor.Command.map((Token) => this.ResolveCommandToken(Token, SourceRoot, Name)),
                WorkingDirectory: this.ResolvePath(Descriptor.WorkingDirectory ?? "[engine.Root]", SourceRoot, Name),
                DependsOn: (Descriptor.DependsOn ?? []).map((Dependency) =>
                    Dependency.includes("::") ? Dependency : `${Name}::custom::${Dependency}`),
                Description: Descriptor.Description ?? `CUSTOM ${Name}/${Descriptor.Id}`,
                ImplicitInputs: (Descriptor.ImplicitInputs ?? []).map((Input) => this.ResolvePath(Input, SourceRoot, Name)),
            };
            this.Actions.push(Action);
            Result.push(Action);
        }
        return Result;
    }

    private ValidateCustomDescriptor(ModuleName: string, Descriptor: CustomActionDescriptor): void {
        if (!/^[A-Za-z0-9_.-]+$/.test(Descriptor.Id)) {
            throw new Error(`Module "${ModuleName}" has unsafe custom action id "${Descriptor.Id}".`);
        }
        if (Descriptor.Outputs.length === 0) {
            throw new Error(`Custom action "${ModuleName}::custom::${Descriptor.Id}" has no outputs.`);
        }
        if (Descriptor.Command.length === 0 || Descriptor.Command.some((Token) => Token.length === 0)) {
            throw new Error(`Custom action "${ModuleName}::custom::${Descriptor.Id}" has an empty command or command token.`);
        }
    }

    private ResolvePath(Value: string, SourceRoot: string, ModuleName?: string): string {
        const Expanded = this.ExpandPathVariables(Value, SourceRoot, ModuleName);
        return Path.normalize(Path.isAbsolute(Expanded) ? Expanded : Path.resolve(SourceRoot, Expanded));
    }

    private ResolveCommandToken(Value: string, SourceRoot: string, ModuleName: string): string {
        // Command arguments are opaque tokens: `/flag` is an option, not a filesystem root.
        return this.ExpandPathVariables(Value, SourceRoot, ModuleName);
    }

    private ExpandPathVariables(Value: string, SourceRoot: string, ModuleName?: string): string {
        const Target = this.BuildTarget.Target;
        const Temp = this.Paths.TemporaryOutputDirectory(Target.Platform, Target.Optimization);
        const Generated = this.Paths.GeneratedOutputDirectory(Target.Platform, Target.Optimization);
        const Variables: Record<string, string> = {
            "[module.SourceRoot]": SourceRoot,
            "[project.SourceRootPath]": SourceRoot,
            "[engine.Root]": this.Paths.Root,
            "[engine.Source]": this.Paths.SourceDirectory,
            "[engine.Temp]": Temp,
            "[engine.Binaries]": this.Paths.BinaryOutputDirectory(Target.Platform),
            "[engine.Generated]": Generated,
            "[module.Generated]": Path.join(Generated, ModuleName ?? Path.basename(SourceRoot)),
        };
        let Expanded = Value;
        for (const [Variable, Replacement] of Object.entries(Variables)) {
            Expanded = Expanded.replaceAll(Variable, Replacement);
        }
        const Unknown = Expanded.match(/\[(?:module|project|engine)\.[^\]]+\]/);
        if (Unknown) throw new Error(`Unknown Builder path variable "${Unknown[0]}".`);
        return Expanded;
    }

    private IsAllowedCustomOutput(Output: string): boolean {
        const Target = this.BuildTarget.Target;
        return [
            this.Paths.TemporaryOutputDirectory(Target.Platform, Target.Optimization),
            this.Paths.GeneratedOutputDirectory(Target.Platform, Target.Optimization),
            this.Paths.BinaryOutputDirectory(Target.Platform),
        ].some((Root) => this.IsWithin(Root, Output));
    }

    private IsWithin(Root: string, Candidate: string): boolean {
        const Relative = Path.relative(Path.resolve(Root), Path.resolve(Candidate));
        return Relative === "" || (!Relative.startsWith(`..${Path.sep}`) && Relative !== ".." && !Path.isAbsolute(Relative));
    }

    private PathKey(FilePath: string): string {
        const Normalized = Path.normalize(FilePath);
        return process.platform === "win32" ? Normalized.toLowerCase() : Normalized;
    }

    private SourceIdentity(SourceRoot: string, Source: string): string {
        if (this.IsWithin(SourceRoot, Source)) return Path.relative(SourceRoot, Source).replace(/\\/g, "/");
        const GeneratedRoot = this.Paths.GeneratedOutputDirectory(
            this.BuildTarget.Target.Platform,
            this.BuildTarget.Target.Optimization,
        );
        if (this.IsWithin(GeneratedRoot, Source)) {
            return `@generated/${Path.relative(GeneratedRoot, Source).replace(/\\/g, "/")}`;
        }
        if (this.IsWithin(this.Paths.Root, Source)) {
            return `@root/${Path.relative(this.Paths.Root, Source).replace(/\\/g, "/")}`;
        }
        return `@absolute/${Path.resolve(Source).replace(/[^A-Za-z0-9_.-]/g, "_")}`;
    }

    private ResolvePathVar(P: string, SourceRoot: string): string {
        return this.ResolvePath(P, SourceRoot);
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

        if (this.BuildTarget.Target.Platform === "Win64") {
            Result.PLATFORM_WINDOWS = "1";
        } else if (this.BuildTarget.Target.Platform === "Mac") {
            Result.PLATFORM_MAC = "1";
        }
        Result.WITH_EDITOR = this.BuildTarget.Target.TargetType === "Editor" ? "1" : "0";
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
        Result.push(this.Paths.BinaryOutputDirectory(this.BuildTarget.Target.Platform));
        Result.push(...this.Toolchain.GetSystemLibPaths());
        return Result;
    }
}
