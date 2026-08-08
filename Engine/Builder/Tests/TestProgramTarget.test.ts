import * as Assert from "node:assert/strict";
import * as Fs from "node:fs/promises";
import * as Os from "node:os";
import * as Path from "node:path";
import Test from "node:test";
import { NinjaBackend } from "../Source/Backend/NinjaBackend.ts";
import { IRBuilder } from "../Source/Build/IRBuilder.ts";
import { ModuleBuild } from "../Source/Configuration/ModuleBuild.ts";
import {
    CreateLegacyTargetDescriptor,
    ResolveTarget,
    SelectTargetDescriptor,
} from "../Source/Configuration/Target.ts";
import {
    Optimization,
    Platform,
    TargetType,
    type ModuleConfiguration,
    type ModuleInstance,
    type Target,
    type TargetDescriptor,
} from "../Source/Configuration/Types.ts";
import { TargetLoader } from "../Source/Discovery/ModuleLoader.ts";
import { DependencyGraph } from "../Source/Graph/DependencyGraph.ts";
import { EnginePaths } from "../Source/Project/EnginePaths.ts";
import type { IToolchain } from "../Source/Toolchain/IToolchain.ts";
import { GetMSVCExecutableSubsystem } from "../Source/Toolchain/MSVCToolchain.ts";

class EntryModuleBuild extends ModuleBuild {
    public readonly Name: string;

    public constructor(Name: string) {
        super();
        this.Name = Name;
    }

    public Configure(_Target: Target, _Configuration: ModuleConfiguration): void {}
}

class FakeToolchain implements IToolchain {
    public readonly Name = "Fake";
    public readonly Platform = "Test";
    public FindCompiler(): string { return "fake-cl"; }
    public FindLinker(): string { return "fake-link"; }
    public FindArchiver(): string { return "fake-lib"; }
    public GetSystemIncludePaths(): string[] { return []; }
    public GetSystemLibPaths(): string[] { return []; }
    public MakeCompileCommand(Source: string, Output: string): string[] {
        return ["/c", Source, `/Fo${Output}`];
    }
    public MakeLinkCommand(Output: string, Objects: string[], Libraries: string[]): string[] {
        return [`/OUT:${Output}`, ...Objects, ...Libraries];
    }
    public MakeArchiveCommand(Output: string, Objects: string[]): string[] {
        return [`/OUT:${Output}`, ...Objects];
    }
    public async GetEnvironment(): Promise<Record<string, string>> { return {}; }
}

function ConcreteTarget(TargetTypeValue: Target["TargetType"]): Target {
    return {
        Platform: Platform.Win64,
        Optimization: Optimization.Debug,
        TargetType: TargetTypeValue,
    };
}

function Descriptor(Name: string, EntryModule: string, Type: Target["TargetType"]): TargetDescriptor {
    return {
        Name,
        Modules: [EntryModule],
        EntryModule,
        Matrix: [{ ...ConcreteTarget(Type) }],
        OutputName: () => `${Name}Binary`,
    };
}

async function Module(Root: string, Name: string): Promise<ModuleInstance> {
    const SourceRoot = Path.join(Root, "Engine", "Source", Name);
    await Fs.mkdir(Path.join(SourceRoot, "Private"), { recursive: true });
    await Fs.writeFile(Path.join(SourceRoot, "Private", `${Name}.cpp`), "int main() { return 0; }\n");
    return {
        Descriptor: new EntryModuleBuild(Name),
        SourceRoot,
        BuildFilePath: Path.join(SourceRoot, "Build.ts"),
    };
}

Test("formal Test target coexists with the transitional Game/Editor target", async () => {
    const BuilderDirectory = Path.resolve(import.meta.dirname, "..");
    const FormalTargets = await new TargetLoader(Path.join(BuilderDirectory, "Targets")).DiscoverTargets();
    const TestTarget = ConcreteTarget(TargetType.Test);
    const GameTarget = ConcreteTarget(TargetType.Game);
    const Fallback = CreateLegacyTargetDescriptor(["Launch", "BuilderTests"]);

    Assert.deepEqual(
        Object.values(TargetType),
        [TargetType.Game, TargetType.Editor, TargetType.Program, TargetType.Test],
    );
    Assert.equal(SelectTargetDescriptor(FormalTargets, TestTarget, undefined, Fallback).Name, "LimitlessTests");
    Assert.equal(SelectTargetDescriptor(FormalTargets, GameTarget, undefined, Fallback).Name, "Limitless");
    Assert.equal(ResolveTarget(
        SelectTargetDescriptor(FormalTargets, TestTarget, "LimitlessTests", Fallback),
        TestTarget,
    ).OutputName, "LimitlessTestsDebug");
});

Test("Program/Test use console entry points while Game/Editor retain window entry points", () => {
    Assert.equal(GetMSVCExecutableSubsystem(ConcreteTarget(TargetType.Program)), "/SUBSYSTEM:CONSOLE");
    Assert.equal(GetMSVCExecutableSubsystem(ConcreteTarget(TargetType.Test)), "/SUBSYSTEM:CONSOLE");
    Assert.equal(GetMSVCExecutableSubsystem(ConcreteTarget(TargetType.Game)), "/SUBSYSTEM:WINDOWS");
    Assert.equal(GetMSVCExecutableSubsystem(ConcreteTarget(TargetType.Editor)), "/SUBSYSTEM:WINDOWS");
});

for (const Type of [TargetType.Test, TargetType.Program] as const) {
    Test(`${Type} descriptor selects, resolves, and produces an isolated executable link`, async (Context) => {
        const Root = await Fs.mkdtemp(Path.join(Os.tmpdir(), `limitless-${Type.toLowerCase()}-`));
        Context.after(() => Fs.rm(Root, { recursive: true, force: true }));
        const EntryName = `${Type}Entry`;
        const UnrelatedName = "UnrelatedModule";
        const Modules = [await Module(Root, EntryName), await Module(Root, UnrelatedName)];
        const Target = ConcreteTarget(Type);
        const TargetDescriptor = Descriptor(`${Type}Target`, EntryName, Type);
        const Selected = SelectTargetDescriptor([TargetDescriptor], Target);
        const Resolved = ResolveTarget(Selected, Target);
        const Graph = new DependencyGraph(Modules, Resolved);
        const ResolvedModules = Graph.Build();

        Assert.deepEqual(ResolvedModules.map((Module) => Module.Instance.Descriptor.Name), [EntryName]);
        Assert.equal(Graph.Get(UnrelatedName), undefined, "undeclared module must not enter the graph");

        const Actions = await new IRBuilder(
            new EnginePaths(Root),
            new FakeToolchain(),
            Resolved,
            Graph,
        ).Build(ResolvedModules);
        Assert.ok(Actions.some((Action) => Action.Id.startsWith(`${EntryName}::compile::`)));
        Assert.equal(Actions.some((Action) => Action.Id.startsWith(`${UnrelatedName}::`)), false);
        const Link = Actions.find((Action) => Action.Id === `${Type}Target::exe`);
        Assert.ok(Link);
        Assert.match(Link.Outputs[0], new RegExp(`${Type}TargetBinary\\.exe$`));

        const NinjaResult = await new NinjaBackend(new FakeToolchain()).Generate(Actions, Resolved, {
            OutputDir: Path.join(Root, "Generated"),
        });
        const NinjaFile = await Fs.readFile(NinjaResult.NinjaPath, "utf-8");
        Assert.match(NinjaFile, new RegExp(`# Target: ${Type}Target \\(`));
        Assert.match(NinjaFile, new RegExp(`${Type}TargetBinary\\.exe: link`));
    });
}

Test("target module declarations reject unknown modules", async () => {
    const Root = await Fs.mkdtemp(Path.join(Os.tmpdir(), "limitless-unknown-module-"));
    try {
        const Target = ConcreteTarget(TargetType.Program);
        const Entry = await Module(Root, "ProgramEntry");
        const BaseDescriptor = Descriptor("ProgramTarget", "ProgramEntry", TargetType.Program);
        const Resolved = ResolveTarget({
            ...BaseDescriptor,
            Modules: ["ProgramEntry", "MissingModule"],
        }, Target);
        Assert.throws(
            () => new DependencyGraph([Entry], Resolved).Build(),
            /declares unknown module "MissingModule"/,
        );
    } finally {
        await Fs.rm(Root, { recursive: true, force: true });
    }
});
