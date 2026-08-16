import * as Assert from "node:assert/strict";
import * as Fs from "node:fs/promises";
import * as Os from "node:os";
import * as Path from "node:path";
import Test from "node:test";
import { NinjaBackend } from "../Source/Backend/NinjaBackend.ts";
import { IRBuilder } from "../Source/Build/IRBuilder.ts";
import { ModuleBuild } from "../Source/Configuration/ModuleBuild.ts";
import { ResolveTarget } from "../Source/Configuration/Target.ts";
import {
    Optimization,
    OutputType,
    Platform,
    TargetType,
    type ModuleConfiguration,
    type ModuleInstance,
    type ResolvedTarget,
    type Target,
    type TargetDescriptor,
} from "../Source/Configuration/Types.ts";
import { DependencyGraph } from "../Source/Graph/DependencyGraph.ts";
import { EnginePaths } from "../Source/Project/EnginePaths.ts";
import { VcxprojGenerator } from "../Source/Project/VcxprojGenerator.ts";
import { XcodeProjectGenerator } from "../Source/Project/XcodeProjectGenerator.ts";
import type { IToolchain } from "../Source/Toolchain/IToolchain.ts";

class SyntheticModuleBuild extends ModuleBuild {
    public readonly Name: string;
    private readonly Dependencies: string[];

    public constructor(Name: string, Dependencies: string[] = []) {
        super();
        this.Name = Name;
        this.Dependencies = Dependencies;
    }

    public Configure(_Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.PublicDependencies.push(...this.Dependencies);
    }
}

class FakeToolchain implements IToolchain {
    public readonly Name = "Fake";
    public readonly Platform = "Test";

    public FindCompiler(): string { return "fake-cl"; }
    public FindLinker(): string { return "fake-link"; }
    public FindArchiver(): string { return "fake-lib"; }
    public GetSystemIncludePaths(): string[] { return []; }
    public GetSystemLibPaths(): string[] { return []; }

    public MakeCompileCommand(
        SourceFile: string,
        OutputFile: string,
        _IncludePaths: string[],
        _Defines: Record<string, string>,
        _Target: Target,
    ): string[] {
        return ["/c", SourceFile, `/Fo${OutputFile}`];
    }

    public MakeLinkCommand(
        OutputFile: string,
        InputObjects: string[],
        LibraryFiles: string[],
        _LibraryPaths: string[],
        _Target: Target,
        _IsDll: boolean,
    ): string[] {
        return [`/OUT:${OutputFile}`, ...InputObjects, ...LibraryFiles];
    }

    public MakeArchiveCommand(OutputFile: string, InputObjects: string[]): string[] {
        return [`/OUT:${OutputFile}`, ...InputObjects];
    }

    public async GetEnvironment(): Promise<Record<string, string>> {
        return {};
    }
}

function MakeConcreteTarget(PlatformValue: Target["Platform"]): Target {
    return {
        Platform: PlatformValue,
        Optimization: Optimization.Debug,
        TargetType: TargetType.Editor,
    };
}

function MakeDescriptor(
    Target: Target,
    OutputName: (ConcreteTarget: Target) => string,
): TargetDescriptor {
    return {
        Name: "Nebula",
        Modules: ["Foundation", "Bootstrap"],
        EntryModule: "Bootstrap",
        Matrix: [
            { ...Target, Optimization: Optimization.Debug },
            { ...Target, Optimization: Optimization.Release },
        ],
        OutputName,
    };
}

async function CreateSyntheticModules(Root: string, WithEntrySource = true): Promise<ModuleInstance[]> {
    const FoundationRoot = Path.join(Root, "Engine", "Source", "Runtime", "Foundation");
    const BootstrapRoot = Path.join(Root, "Engine", "Source", "Runtime", "Bootstrap");
    await Fs.mkdir(Path.join(FoundationRoot, "Private"), { recursive: true });
    await Fs.mkdir(Path.join(BootstrapRoot, "Private"), { recursive: true });
    await Fs.writeFile(Path.join(FoundationRoot, "Private", "Foundation.cpp"), "int Foundation() { return 7; }\n");
    if (WithEntrySource) {
        await Fs.writeFile(Path.join(BootstrapRoot, "Private", "Bootstrap.cpp"), "int main() { return 0; }\n");
    }

    return [
        {
            Descriptor: new SyntheticModuleBuild("Foundation"),
            SourceRoot: FoundationRoot,
            BuildFilePath: Path.join(FoundationRoot, "Build.ts"),
        },
        {
            Descriptor: new SyntheticModuleBuild("Bootstrap", ["Foundation"]),
            SourceRoot: BootstrapRoot,
            BuildFilePath: Path.join(BootstrapRoot, "Build.ts"),
        },
    ];
}

Test("non-default EntryModule and OutputName drive IR, Ninja, VCXProj, and Xcode", async (Context) => {
    const Root = await Fs.mkdtemp(Path.join(Os.tmpdir(), "limitless-target-"));
    Context.after(() => Fs.rm(Root, { recursive: true, force: true }));
    const Paths = new EnginePaths(Root);
    const Toolchain = new FakeToolchain();
    const Modules = await CreateSyntheticModules(Root);
    const WinTarget = MakeConcreteTarget(Platform.Win64);
    let OutputNameCalls = 0;
    const Descriptor = MakeDescriptor(WinTarget, (ConcreteTarget) => {
        OutputNameCalls++;
        return ConcreteTarget.Optimization === Optimization.Debug
            ? "NebulaEditorDebug"
            : "NebulaEditorRelease";
    });
    const BuildTarget = ResolveTarget(Descriptor, WinTarget);
    const Graph = new DependencyGraph(Modules, BuildTarget);
    const ResolvedModules = Graph.Build();

    Assert.equal(OutputNameCalls, 1, "OutputName must be resolved exactly once");
    Assert.equal(Graph.Get("Bootstrap")?.Configuration.Output, OutputType.Lib);
    Assert.equal(Graph.Get("Foundation")?.Configuration.Output, OutputType.Dll);

    const Actions = await new IRBuilder(Paths, Toolchain, BuildTarget, Graph).Build(ResolvedModules);
    const EntryCompile = Actions.find((Action) => Action.Id.startsWith("Bootstrap::compile::"));
    const Executable = Actions.find((Action) => Action.Id === "Nebula::exe");
    Assert.ok(EntryCompile);
    Assert.ok(Executable);
    Assert.equal(Executable.Outputs[0], Path.join(
        Root, "Engine", "Binaries", "Win64", "Debug", "Nebula", "Editor", "NebulaEditorDebug.exe",
    ));
    Assert.ok(Executable.Inputs.includes(EntryCompile.Outputs[0]));
    Assert.equal(Actions.some((Action) => Action.Id === "Bootstrap::archive"), false);

    const NinjaResult = await new NinjaBackend(Toolchain).Generate(Actions, BuildTarget, {
        OutputDir: Path.join(Root, "ninja"),
    });
    const Ninja = await Fs.readFile(NinjaResult.NinjaPath, "utf-8");
    Assert.match(Ninja, /# Entry: Bootstrap; Output: NebulaEditorDebug/);
    Assert.match(Ninja, /NebulaEditorDebug\.exe: link/);

    const SolutionPath = await new VcxprojGenerator(Paths, Graph, Toolchain)
        .Generate(ResolvedModules, BuildTarget);
    const Vcxproj = await Fs.readFile(
        Path.join(Paths.ProjectFilesDirectory, "LimitlessEngine.vcxproj"),
        "utf-8",
    );
    Assert.equal(SolutionPath, Path.join(Root, "LimitlessEngine.sln"));
    const DebugPropertyGroup = Vcxproj.match(
        /<PropertyGroup Condition="'\$\(Configuration\)\|\$\(Platform\)'=='Debug\|x64'">([\s\S]*?)<\/PropertyGroup>/,
    )?.[1];
    const ReleasePropertyGroup = Vcxproj.match(
        /<PropertyGroup Condition="'\$\(Configuration\)\|\$\(Platform\)'=='Release\|x64'">([\s\S]*?)<\/PropertyGroup>/,
    )?.[1];
    Assert.ok(DebugPropertyGroup);
    Assert.ok(ReleasePropertyGroup);
    Assert.match(DebugPropertyGroup, /<NMakeOutput>[^<]*NebulaEditorDebug\.exe<\/NMakeOutput>/);
    Assert.match(DebugPropertyGroup, /Binaries\\Win64\\Debug\\Nebula\\Editor\\NebulaEditorDebug\.exe/);
    Assert.match(DebugPropertyGroup, /<TargetName>NebulaEditorDebug<\/TargetName>/);
    Assert.match(DebugPropertyGroup, /Binaries\\Win64\\Debug\\Nebula\\Editor[^<]*&amp;[^<]*\.\.\\Build\\Win64\\Debug\\Nebula\\Editor/);
    Assert.doesNotMatch(DebugPropertyGroup, /Binaries\\Win64\\Release/);
    Assert.match(ReleasePropertyGroup, /<NMakeOutput>[^<]*NebulaEditorRelease\.exe<\/NMakeOutput>/);
    Assert.match(ReleasePropertyGroup, /Binaries\\Win64\\Release\\Nebula\\Editor\\NebulaEditorRelease\.exe/);
    Assert.match(ReleasePropertyGroup, /<TargetName>NebulaEditorRelease<\/TargetName>/);
    Assert.match(ReleasePropertyGroup, /Binaries\\Win64\\Release\\Nebula\\Editor[^<]*&amp;[^<]*\.\.\\Build\\Win64\\Release\\Nebula\\Editor/);
    Assert.doesNotMatch(ReleasePropertyGroup, /Binaries\\Win64\\Debug/);
    Assert.match(Vcxproj, /--target "Nebula"/);
    Assert.equal(OutputNameCalls, 2, "each generated VCX configuration resolves OutputName once");

    const MacTarget = MakeConcreteTarget(Platform.Mac);
    const MacDescriptor = MakeDescriptor(
        MacTarget,
        (ConcreteTarget) => ConcreteTarget.Optimization === Optimization.Debug
            ? "NebulaEditorDebug"
            : "NebulaEditorRelease",
    );
    const MacBuildTarget = ResolveTarget(MacDescriptor, MacTarget);
    const MacGraph = new DependencyGraph(Modules, MacBuildTarget);
    const MacReleaseTarget = ResolveTarget(MacDescriptor, {
        ...MacTarget,
        Optimization: Optimization.Release,
    });
    const XcodePath = await new XcodeProjectGenerator(Paths)
        .Generate({
            Debug: MacGraph.Build(),
            Release: new DependencyGraph(Modules, MacReleaseTarget).Build(),
        }, MacBuildTarget);
    const Project = await Fs.readFile(Path.join(XcodePath, "project.pbxproj"), "utf-8");
    const Scheme = await Fs.readFile(
        Path.join(XcodePath, "xcshareddata", "xcschemes", "Nebula.xcscheme"),
        "utf-8",
    );
    Assert.match(Project, /PRODUCT_NAME = "NebulaEditorDebug";/);
    Assert.match(Project, /PRODUCT_NAME = "NebulaEditorRelease";/);
    Assert.match(Project, /CONFIGURATION_BUILD_DIR = "\$\(SRCROOT\)\/Engine\/Binaries\/Mac\/Debug\/Nebula\/Editor";/);
    Assert.match(Project, /CONFIGURATION_BUILD_DIR = "\$\(SRCROOT\)\/Engine\/Binaries\/Mac\/Release\/Nebula\/Editor";/);
    Assert.match(Project, /path = "\$\(PRODUCT_NAME\)"; sourceTree = BUILT_PRODUCTS_DIR/);
    const LaunchAction = Scheme.match(/<LaunchAction[\s\S]*?<\/LaunchAction>/)?.[0];
    const ProfileAction = Scheme.match(/<ProfileAction[\s\S]*?<\/ProfileAction>/)?.[0];
    Assert.ok(LaunchAction);
    Assert.ok(ProfileAction);
    Assert.match(LaunchAction, /buildConfiguration="Debug"/);
    Assert.match(LaunchAction, /BuildableName="NebulaEditorDebug"/);
    Assert.match(ProfileAction, /buildConfiguration="Release"/);
    Assert.match(ProfileAction, /BuildableName="NebulaEditorRelease"/);
    Assert.match(Scheme, /BlueprintName="Nebula"/);
});

Test("Debug and Release IR use disjoint module and executable binary roots", async (Context) => {
    const Root = await Fs.mkdtemp(Path.join(Os.tmpdir(), "limitless-target-config-binaries-"));
    Context.after(() => Fs.rm(Root, { recursive: true, force: true }));
    const Paths = new EnginePaths(Root);
    const Toolchain = new FakeToolchain();
    const Modules = await CreateSyntheticModules(Root);
    const BaseTarget = MakeConcreteTarget(Platform.Win64);
    const Descriptor = MakeDescriptor(
        BaseTarget,
        (ConcreteTarget) => `NebulaEditor${ConcreteTarget.Optimization}`,
    );

    const BuildOutputs = async (ConcreteTarget: Target): Promise<string[]> => {
        const BuildTarget = ResolveTarget(Descriptor, ConcreteTarget);
        const Graph = new DependencyGraph(Modules, BuildTarget);
        const Actions = await new IRBuilder(Paths, Toolchain, BuildTarget, Graph).Build(Graph.Build());
        return Actions
            .filter((Action) => Action.Type === "archive" || Action.Type === "link")
            .flatMap((Action) => Action.Outputs);
    };

    const DebugBuildTarget = ResolveTarget(Descriptor, { ...BaseTarget, Optimization: Optimization.Debug });
    const ReleaseBuildTarget = ResolveTarget(Descriptor, { ...BaseTarget, Optimization: Optimization.Release });
    const DebugOutputs = await BuildOutputs(DebugBuildTarget.Target);
    const ReleaseOutputs = await BuildOutputs(ReleaseBuildTarget.Target);
    const DebugRoot = Paths.BinaryOutputDirectory(DebugBuildTarget);
    const ReleaseRoot = Paths.BinaryOutputDirectory(ReleaseBuildTarget);

    Assert.ok(DebugOutputs.some((Output) => Output.endsWith("Foundation.dll")), "module output is covered");
    Assert.ok(DebugOutputs.some((Output) => Output.endsWith("NebulaEditorDebug.exe")), "target output is covered");
    Assert.ok(ReleaseOutputs.some((Output) => Output.endsWith("Foundation.dll")), "release module output is covered");
    Assert.ok(ReleaseOutputs.some((Output) => Output.endsWith("NebulaEditorRelease.exe")), "release target output is covered");
    Assert.ok(DebugOutputs.every((Output) => Path.relative(DebugRoot, Output).split(Path.sep)[0] !== ".."));
    Assert.ok(ReleaseOutputs.every((Output) => Path.relative(ReleaseRoot, Output).split(Path.sep)[0] !== ".."));
    Assert.equal(DebugOutputs.some((Output) => ReleaseOutputs.includes(Output)), false);
});

Test("target variant roots isolate type, descriptor, and optimization independently of OutputName", () => {
    const Paths = new EnginePaths(Path.resolve("synthetic-root"));
    const Matrix: Target[] = [
        { Platform: Platform.Win64, Optimization: Optimization.Debug, TargetType: TargetType.Game },
        { Platform: Platform.Win64, Optimization: Optimization.Debug, TargetType: TargetType.Editor },
        { Platform: Platform.Win64, Optimization: Optimization.Release, TargetType: TargetType.Game },
    ];
    const MakeVariantDescriptor = (Name: string): TargetDescriptor => ({
        Name,
        Modules: ["Bootstrap"],
        EntryModule: "Bootstrap",
        Matrix,
        OutputName: () => "SharedOutputName",
    });
    const Nebula = MakeVariantDescriptor("Nebula");
    const Orion = MakeVariantDescriptor("Orion");
    const GameDebug = ResolveTarget(Nebula, Matrix[0]);
    const EditorDebug = ResolveTarget(Nebula, Matrix[1]);
    const OtherGameDebug = ResolveTarget(Orion, Matrix[0]);
    const GameRelease = ResolveTarget(Nebula, Matrix[2]);

    const BinaryRoots = [GameDebug, EditorDebug, OtherGameDebug, GameRelease]
        .map((BuildTarget) => Paths.BinaryOutputDirectory(BuildTarget));
    const TemporaryRoots = [GameDebug, EditorDebug, OtherGameDebug, GameRelease]
        .map((BuildTarget) => Paths.TemporaryOutputDirectory(BuildTarget));

    Assert.equal(new Set(BinaryRoots).size, BinaryRoots.length);
    Assert.equal(new Set(TemporaryRoots).size, TemporaryRoots.length);
    Assert.equal(BinaryRoots[0], Path.join(
        Paths.Root, "Engine", "Binaries", "Win64", "Debug", "Nebula", "Game",
    ));
    Assert.equal(TemporaryRoots[1], Path.join(
        Paths.Root, "Engine", "Intermediate", "Build", "Win64", "Debug", "Nebula", "Editor",
    ));
    Assert.equal(Paths.GeneratedOutputDirectory(OtherGameDebug), Path.join(TemporaryRoots[2], "Generated"));
});

Test("missing entry modules are rejected by dependency graph validation", async (Context) => {
    const Root = await Fs.mkdtemp(Path.join(Os.tmpdir(), "limitless-target-missing-"));
    Context.after(() => Fs.rm(Root, { recursive: true, force: true }));
    const Modules = await CreateSyntheticModules(Root);
    const Target = MakeConcreteTarget(Platform.Win64);
    const Descriptor: TargetDescriptor = {
        ...MakeDescriptor(Target, () => "NebulaEditor"),
        Modules: ["Foundation", "GhostBootstrap"],
        EntryModule: "GhostBootstrap",
    };
    const BuildTarget = ResolveTarget(Descriptor, Target);
    Assert.throws(
        () => new DependencyGraph(Modules, BuildTarget).Build(),
        /entry module "GhostBootstrap" was not discovered/,
    );
});

Test("entry modules without compiled objects are rejected by IRBuilder", async (Context) => {
    const Root = await Fs.mkdtemp(Path.join(Os.tmpdir(), "limitless-target-empty-"));
    Context.after(() => Fs.rm(Root, { recursive: true, force: true }));
    const Paths = new EnginePaths(Root);
    const Modules = await CreateSyntheticModules(Root, false);
    const Target = MakeConcreteTarget(Platform.Win64);
    const BuildTarget = ResolveTarget(MakeDescriptor(Target, () => "NebulaEditor"), Target);
    const Graph = new DependencyGraph(Modules, BuildTarget);
    await Assert.rejects(
        () => new IRBuilder(Paths, new FakeToolchain(), BuildTarget, Graph).Build(Graph.Build()),
        /entry module "Bootstrap" produced no compiled object files/,
    );
});

Test("invalid descriptor output names fail before reaching a backend", () => {
    const Target = MakeConcreteTarget(Platform.Win64);
    Assert.throws(
        () => ResolveTarget(MakeDescriptor(Target, () => "../NebulaEditor"), Target),
        /must be a file name without a path/,
    );
});

Test("unsafe descriptor names fail before reaching command or file generators", () => {
    const Target = MakeConcreteTarget(Platform.Win64);
    for (const UnsafeName of ["../Nebula", "Nebula/Editor", "Nebula\" & calc", "Nebula%PATH%", "Nebula\nEditor"]) {
        const Descriptor = {
            ...MakeDescriptor(Target, () => "NebulaEditor"),
            Name: UnsafeName,
        };
        Assert.throws(
            () => ResolveTarget(Descriptor, Target),
            /must be a safe identifier/,
        );
    }
});
