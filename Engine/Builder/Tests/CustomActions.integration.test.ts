import * as Assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import * as Fs from "node:fs/promises";
import * as Os from "node:os";
import * as Path from "node:path";
import Test from "node:test";
import { NinjaBackend } from "../Source/Backend/NinjaBackend.ts";
import { BuildCustomActionGraph } from "../Source/Build/CustomActionBuilder.ts";
import { IRBuilder } from "../Source/Build/IRBuilder.ts";
import { ModuleBuild } from "../Source/Configuration/ModuleBuild.ts";
import { ResolveTarget } from "../Source/Configuration/Target.ts";
import {
    Optimization, Platform, TargetType,
    type BuildAction, type ModuleConfiguration, type ModuleInstance,
    type ResolvedTarget, type Target, type TargetDescriptor,
} from "../Source/Configuration/Types.ts";
import { DependencyGraph } from "../Source/Graph/DependencyGraph.ts";
import { EnginePaths } from "../Source/Project/EnginePaths.ts";
import { CustomActionToolchain } from "../Source/Toolchain/CustomActionToolchain.ts";
import { FindNinjaExecutable } from "../Source/Toolchain/ExecutableLocator.ts";
import type { IToolchain } from "../Source/Toolchain/IToolchain.ts";

const NodeExecutable = process.execPath;

class NodeToolchain implements IToolchain {
    public readonly Name = "NodeStandIn";
    public readonly Platform = "Test";
    private readonly Tool: string;
    private readonly Counters: string;
    public constructor(Tool: string, Counters: string) {
        this.Tool = Tool;
        this.Counters = Counters;
    }
    public FindCompiler(): string { return NodeExecutable; }
    public FindLinker(): string { return NodeExecutable; }
    public FindArchiver(): string { return NodeExecutable; }
    public GetSystemIncludePaths(): string[] { return []; }
    public GetSystemLibPaths(): string[] { return []; }
    public MakeCompileCommand(Source: string, Output: string): string[] {
        return [this.Tool, "compile", Path.join(this.Counters, "compile.count"), Source, Output];
    }
    public MakeLinkCommand(Output: string, Objects: string[]): string[] {
        return [this.Tool, "link", Path.join(this.Counters, "link.count"), Output, ...Objects];
    }
    public MakeArchiveCommand(Output: string, Objects: string[]): string[] {
        return [this.Tool, "link", Path.join(this.Counters, "archive.count"), Output, ...Objects];
    }
    public async GetEnvironment(): Promise<Record<string, string>> {
        return { ...process.env } as Record<string, string>;
    }
}

class CustomEntryBuild extends ModuleBuild {
    public readonly Name = "Entry";
    private readonly Tool: string;
    private readonly Counters: string;
    private readonly Explicit: string;
    private readonly Implicit: string;
    private readonly DependencyInput: string;
    private readonly WorkingDirectory: string;
    public constructor(
        Tool: string,
        Counters: string,
        Explicit: string,
        Implicit: string,
        DependencyInput: string,
        WorkingDirectory: string,
    ) {
        super();
        this.Tool = Tool;
        this.Counters = Counters;
        this.Explicit = Explicit;
        this.Implicit = Implicit;
        this.DependencyInput = DependencyInput;
        this.WorkingDirectory = WorkingDirectory;
    }

    public Configure(_Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.CustomActions.push(
            {
                Id: "Args",
                Inputs: [],
                Outputs: ["[module.Generated]/argv.json", "[module.Generated]/cwd.txt"],
                Command: [
                    NodeExecutable, this.Tool, "argv",
                    "[module.Generated]/argv.json", "[module.Generated]/cwd.txt",
                    "space arg", "dollar$arg", "hash#arg", "amp&arg",
                    "percent%PATH%arg", "caret^arg",
                ],
                WorkingDirectory: this.WorkingDirectory,
            },
            {
                Id: "Dependency",
                Inputs: [this.DependencyInput],
                Outputs: ["[module.Generated]/dependency.stamp"],
                Command: [
                    NodeExecutable, this.Tool, "copy",
                    Path.join(this.Counters, "dependency.count"),
                    this.DependencyInput, "[module.Generated]/dependency.stamp",
                ],
            },
            {
                Id: "Generate",
                Inputs: [this.Explicit],
                ImplicitInputs: [this.Implicit, "[module.Generated]/dependency.stamp"],
                Outputs: ["[module.Generated]/generated.cpp", "[module.Generated]/generated.h"],
                Command: [
                    NodeExecutable, this.Tool, "generate",
                    Path.join(this.Counters, "generate.count"),
                    this.Explicit, this.Implicit, "[module.Generated]/dependency.stamp",
                    "[module.Generated]/generated.cpp", "[module.Generated]/generated.h",
                ],
                DependsOn: ["Dependency"],
                RunBeforeCompile: true,
            },
            {
                Id: "OrderOnly",
                Inputs: [],
                Outputs: ["[module.Generated]/order-only.stamp"],
                Command: [
                    NodeExecutable, this.Tool, "literal",
                    Path.join(this.Counters, "order.count"), "[module.Generated]/order-only.stamp", "order",
                ],
                DependsOn: ["Dependency"],
            },
        );
    }
}

class SelectedActionBuild extends ModuleBuild {
    public readonly Name = "Entry";
    private readonly Tool: string;
    private readonly Counters: string;
    private readonly DependencyInput: string;
    private readonly ImplicitInput: string;

    public constructor(Tool: string, Counters: string, DependencyInput: string, ImplicitInput: string) {
        super();
        this.Tool = Tool;
        this.Counters = Counters;
        this.DependencyInput = DependencyInput;
        this.ImplicitInput = ImplicitInput;
    }

    public Configure(_Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.CustomActions.push(
            {
                Id: "Dependency",
                Inputs: [this.DependencyInput],
                Outputs: ["[module.Generated]/dependency.stamp"],
                Command: [
                    NodeExecutable, this.Tool, "copy", Path.join(this.Counters, "dependency.count"),
                    this.DependencyInput, "[module.Generated]/dependency.stamp",
                ],
            },
            {
                Id: "Selected",
                Inputs: [],
                ImplicitInputs: [this.ImplicitInput, "[module.Generated]/dependency.stamp"],
                Outputs: ["[module.Generated]/selected.stamp"],
                Command: [
                    NodeExecutable, this.Tool, "combine", Path.join(this.Counters, "selected.count"),
                    this.ImplicitInput, "[module.Generated]/dependency.stamp", "[module.Generated]/selected.stamp",
                ],
                DependsOn: ["Dependency"],
            },
            {
                Id: "Unrelated",
                Inputs: [],
                Outputs: ["[module.Generated]/unrelated.stamp"],
                Command: [
                    NodeExecutable, this.Tool, "literal", Path.join(this.Counters, "unrelated.count"),
                    "[module.Generated]/unrelated.stamp", "unrelated",
                ],
            },
        );
    }
}

function MakeTarget(): { Descriptor: TargetDescriptor; Target: Target; Resolved: ResolvedTarget } {
    const Concrete: Target = {
        Platform: Platform.Win64,
        Optimization: Optimization.Debug,
        TargetType: TargetType.Program,
    };
    const Descriptor: TargetDescriptor = {
        Name: "CustomIncremental", Modules: ["Entry"], EntryModule: "Entry",
        Matrix: [Concrete], OutputName: () => "CustomIncrementalDebug",
    };
    return { Descriptor, Target: Concrete, Resolved: ResolveTarget(Descriptor, Concrete) };
}

async function Count(FilePath: string): Promise<number> {
    try { return Number(await Fs.readFile(FilePath, "utf-8")); } catch { return 0; }
}

function RunNinja(Ninja: string, NinjaFile: string): ReturnType<typeof spawnSync> {
    return spawnSync(Ninja, ["-f", NinjaFile], {
        cwd: Path.dirname(NinjaFile), encoding: "utf-8", shell: false,
    });
}

async function TouchWithContent(FilePath: string, Suffix: string): Promise<void> {
    await new Promise((Resolve) => setTimeout(Resolve, 30));
    await Fs.appendFile(FilePath, Suffix);
}

Test("real Ninja custom actions preserve argv/cwd and have correct incremental semantics", async (Context) => {
    const Ninja = FindNinjaExecutable();
    if (!Ninja) { Context.skip("ninja executable is unavailable"); return; }

    const Root = await Fs.mkdtemp(Path.join(Os.tmpdir(), "limitless-custom-incremental-"));
    Context.after(() => Fs.rm(Root, { recursive: true, force: true }));
    const ModuleRoot = Path.join(Root, "Engine", "Source", "Programs", "Entry");
    const WorkDir = Path.join(ModuleRoot, "working directory");
    const FixtureDir = Path.join(Root, "fixture");
    const Counters = Path.join(Root, "counters");
    await Promise.all([
        Fs.mkdir(WorkDir, { recursive: true }), Fs.mkdir(FixtureDir, { recursive: true }),
        Fs.mkdir(Counters, { recursive: true }),
    ]);
    const Tool = Path.join(FixtureDir, "node tool #1.mjs");
    const Explicit = Path.join(FixtureDir, "explicit input.txt");
    const Implicit = Path.join(FixtureDir, "implicit input.txt");
    const DependencyInput = Path.join(FixtureDir, "dependency input.txt");
    await Fs.writeFile(Explicit, "explicit-1");
    await Fs.writeFile(Implicit, "implicit-1");
    await Fs.writeFile(DependencyInput, "dependency-1");
    await Fs.writeFile(Tool, `
import fs from "node:fs";
import path from "node:path";
const [mode, ...args] = process.argv.slice(2);
const bump = (file) => { let n = 0; try { n = Number(fs.readFileSync(file, "utf8")); } catch {} fs.mkdirSync(path.dirname(file), {recursive:true}); fs.writeFileSync(file, String(n + 1)); };
const write = (file, data) => { fs.mkdirSync(path.dirname(file), {recursive:true}); fs.writeFileSync(file, data); };
if (mode === "argv") { write(args[0], JSON.stringify(args.slice(2))); write(args[1], process.cwd()); }
else if (mode === "copy") { bump(args[0]); write(args[2], fs.readFileSync(args[1])); }
else if (mode === "literal") { bump(args[0]); write(args[1], args[2]); }
else if (mode === "generate") { bump(args[0]); const value = args.slice(1, 4).map(p => fs.readFileSync(p, "utf8")).join("|"); write(args[4], "source:" + value); write(args[5], "header:" + value); }
else if (mode === "compile") { bump(args[0]); write(args[2], "object:" + fs.readFileSync(args[1], "utf8")); }
else if (mode === "link") { bump(args[0]); write(args[1], args.slice(2).map(p => fs.readFileSync(p, "utf8")).join("|")); }
else if (mode === "fail") process.exit(9);
else process.exit(8);
`);

    const { Resolved } = MakeTarget();
    const Paths = new EnginePaths(Root);
    const GeneratedDir = Path.join(Paths.GeneratedOutputDirectory(Resolved), "Entry");
    const Module: ModuleInstance = {
        Descriptor: new CustomEntryBuild(Tool, Counters, Explicit, Implicit, DependencyInput, WorkDir),
        SourceRoot: ModuleRoot, BuildFilePath: Path.join(ModuleRoot, "Build.ts"),
    };
    const Graph = new DependencyGraph([Module], Resolved);
    const Actions = await new IRBuilder(Paths, new NodeToolchain(Tool, Counters), Resolved, Graph)
        .Build(Graph.Build());
    Assert.ok(Actions.some((Action) => Action.Id === "Entry::custom::Generate"));
    Assert.ok(Actions.some((Action) => Action.Type === "compile" && Action.Inputs[0].endsWith("generated.cpp")));

    const Backend = new NinjaBackend(new NodeToolchain(Tool, Counters));
    const Result = await Backend.Generate(Actions, Resolved, { OutputDir: Path.join(Root, "ninja output") });
    const First = RunNinja(Ninja, Result.NinjaPath);
    Assert.equal(First.status, 0, `${First.stdout}\n${First.stderr}`);
    Assert.deepEqual(
        JSON.parse(await Fs.readFile(Path.join(GeneratedDir, "argv.json"), "utf-8")),
        ["space arg", "dollar$arg", "hash#arg", "amp&arg", "percent%PATH%arg", "caret^arg"],
    );
    Assert.equal(
        await Fs.readFile(Path.join(GeneratedDir, "cwd.txt"), "utf-8"),
        WorkDir,
    );
    Assert.equal(await Count(Path.join(Counters, "generate.count")), 1);
    Assert.equal(await Count(Path.join(Counters, "compile.count")), 1);
    Assert.equal(await Count(Path.join(Counters, "link.count")), 1);

    const Second = RunNinja(Ninja, Result.NinjaPath);
    Assert.equal(Second.status, 0, `${Second.stdout}\n${Second.stderr}`);
    Assert.equal(await Count(Path.join(Counters, "generate.count")), 1, "unchanged generator must not rerun");

    await TouchWithContent(Explicit, "-changed");
    Assert.equal(RunNinja(Ninja, Result.NinjaPath).status, 0);
    Assert.equal(await Count(Path.join(Counters, "generate.count")), 2);
    Assert.equal(await Count(Path.join(Counters, "compile.count")), 2);

    await TouchWithContent(Implicit, "-changed");
    Assert.equal(RunNinja(Ninja, Result.NinjaPath).status, 0);
    Assert.equal(await Count(Path.join(Counters, "generate.count")), 3);

    const DependencyOutput = Path.join(GeneratedDir, "dependency.stamp");
    const OrderCountBefore = await Count(Path.join(Counters, "order.count"));
    await TouchWithContent(DependencyOutput, "-direct-touch");
    Assert.equal(RunNinja(Ninja, Result.NinjaPath).status, 0);
    Assert.equal(await Count(Path.join(Counters, "generate.count")), 4, "DependsOn + ImplicitInputs must dirty the consumer");
    Assert.equal(await Count(Path.join(Counters, "order.count")), OrderCountBefore, "order-only DependsOn alone must not dirty the consumer");

    await Fs.rm(Path.join(GeneratedDir, "generated.h"));
    Assert.equal(RunNinja(Ninja, Result.NinjaPath).status, 0);
    Assert.equal(await Count(Path.join(Counters, "generate.count")), 5, "deleting a secondary output must rerun its edge");
    Assert.equal(await Count(Path.join(Counters, "compile.count")), 5, "regenerated source/header must recompile");

    const FailureOutput = Path.join(Paths.GeneratedOutputDirectory(Resolved), "failure.out");
    const FailureActions: BuildAction[] = [{
        Id: "Failure", Type: "custom", Inputs: [], Outputs: [FailureOutput],
        Command: [NodeExecutable, Tool, "fail"], WorkingDirectory: WorkDir,
        DependsOn: [], Description: "expected failure",
    }];
    const Failure = await Backend.Generate(FailureActions, Resolved, { OutputDir: Path.join(Root, "failure ninja") });
    Assert.notEqual(RunNinja(Ninja, Failure.NinjaPath).status, 0, "custom command failure must propagate a nonzero exit status");
});

Test("custom-only Ninja builds the selected action DAG without touching the ordinary graph", async (Context) => {
    if (!FindNinjaExecutable()) { Context.skip("ninja executable is unavailable"); return; }

    const Root = await Fs.mkdtemp(Path.join(Os.tmpdir(), "limitless-build-action-"));
    Context.after(() => Fs.rm(Root, { recursive: true, force: true }));
    const ModuleRoot = Path.join(Root, "Engine", "Source", "Programs", "Entry");
    const FixtureDir = Path.join(Root, "fixture");
    const Counters = Path.join(Root, "counters");
    await Promise.all([
        Fs.mkdir(ModuleRoot, { recursive: true }),
        Fs.mkdir(FixtureDir, { recursive: true }),
        Fs.mkdir(Counters, { recursive: true }),
    ]);
    const Tool = Path.join(FixtureDir, "custom-only-tool.mjs");
    const DependencyInput = Path.join(FixtureDir, "dependency.txt");
    const ImplicitInput = Path.join(FixtureDir, "implicit.txt");
    await Fs.writeFile(DependencyInput, "dependency-1");
    await Fs.writeFile(ImplicitInput, "implicit-1");
    await Fs.writeFile(Tool, `
import fs from "node:fs";
import path from "node:path";
const [mode, ...args] = process.argv.slice(2);
const bump = (file) => { let n = 0; try { n = Number(fs.readFileSync(file, "utf8")); } catch {} fs.mkdirSync(path.dirname(file), {recursive:true}); fs.writeFileSync(file, String(n + 1)); };
const write = (file, data) => { fs.mkdirSync(path.dirname(file), {recursive:true}); fs.writeFileSync(file, data); };
if (mode === "copy") { bump(args[0]); write(args[2], fs.readFileSync(args[1])); }
else if (mode === "combine") { bump(args[0]); write(args[3], fs.readFileSync(args[1]) + "|" + fs.readFileSync(args[2])); }
else if (mode === "literal") { bump(args[0]); write(args[1], args[2]); }
else process.exit(8);
`);

    const { Resolved } = MakeTarget();
    const Paths = new EnginePaths(Root);
    const Module: ModuleInstance = {
        Descriptor: new SelectedActionBuild(Tool, Counters, DependencyInput, ImplicitInput),
        SourceRoot: ModuleRoot,
        BuildFilePath: Path.join(ModuleRoot, "Build.ts"),
    };
    const Graph = new DependencyGraph([Module], Resolved);
    const Actions = await BuildCustomActionGraph(Paths, Resolved, Graph.Build());
    Assert.equal(Actions.length, 3);
    const Selected = Actions.find((Action) => Action.Id === "Entry::custom::Selected");
    Assert.ok(Selected);

    const VariantDir = Paths.TemporaryOutputDirectory(Resolved);
    await Fs.mkdir(VariantDir, { recursive: true });
    const OrdinaryNinja = Path.join(VariantDir, "build.ninja");
    const OrdinarySentinel = "ordinary graph must remain intact\n";
    await Fs.writeFile(OrdinaryNinja, OrdinarySentinel);

    const CustomDir = Path.join(VariantDir, "CustomActions");
    const Backend = new NinjaBackend(new CustomActionToolchain());
    const Result = await Backend.Generate(Actions, Resolved, { OutputDir: CustomDir });
    await Backend.Build(Result, { Targets: Selected.Outputs });
    Assert.equal(await Count(Path.join(Counters, "dependency.count")), 1);
    Assert.equal(await Count(Path.join(Counters, "selected.count")), 1);
    Assert.equal(await Count(Path.join(Counters, "unrelated.count")), 0);

    await Backend.Build(Result, { Targets: Selected.Outputs });
    Assert.equal(await Count(Path.join(Counters, "dependency.count")), 1, "unchanged dependency must not rerun");
    Assert.equal(await Count(Path.join(Counters, "selected.count")), 1, "unchanged selected action must not rerun");

    await TouchWithContent(ImplicitInput, "-changed");
    await Backend.Build(Result, { Targets: Selected.Outputs });
    Assert.equal(await Count(Path.join(Counters, "dependency.count")), 1);
    Assert.equal(await Count(Path.join(Counters, "selected.count")), 2, "implicit input must dirty selected action");

    await Fs.rm(Selected.Outputs[0]);
    await Backend.Build(Result, { Targets: Selected.Outputs });
    Assert.equal(await Count(Path.join(Counters, "selected.count")), 3, "deleted selected output must rerun its edge");

    await TouchWithContent(DependencyInput, "-changed");
    await Backend.Build(Result, { Targets: Selected.Outputs });
    Assert.equal(await Count(Path.join(Counters, "dependency.count")), 2, "selected target must execute its dependency");
    Assert.equal(await Count(Path.join(Counters, "selected.count")), 4, "changed dependency output must dirty selected action");
    Assert.equal(await Count(Path.join(Counters, "unrelated.count")), 0, "unrelated action must never execute");

    Assert.equal(await Fs.readFile(OrdinaryNinja, "utf-8"), OrdinarySentinel);
    await Fs.access(Path.join(CustomDir, "build.ninja"));
    await Fs.access(Path.join(CustomDir, "compile_commands.json"));
});
