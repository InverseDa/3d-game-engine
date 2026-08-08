import * as Assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import * as Fs from "node:fs/promises";
import * as Os from "node:os";
import * as Path from "node:path";
import Test from "node:test";
import { NinjaBackend } from "../Source/Backend/NinjaBackend.ts";
import { ValidateAndSortActions } from "../Source/Build/ActionGraph.ts";
import { IRBuilder } from "../Source/Build/IRBuilder.ts";
import { ModuleBuild } from "../Source/Configuration/ModuleBuild.ts";
import { ResolveTarget } from "../Source/Configuration/Target.ts";
import {
    Optimization, OutputType, Platform, TargetType,
    type BuildAction, type CustomActionDescriptor, type ModuleConfiguration,
    type ModuleInstance, type ResolvedTarget, type Target, type TargetDescriptor,
} from "../Source/Configuration/Types.ts";
import { DependencyGraph } from "../Source/Graph/DependencyGraph.ts";
import { EnginePaths } from "../Source/Project/EnginePaths.ts";
import type { IToolchain } from "../Source/Toolchain/IToolchain.ts";
import { FindNinjaExecutable } from "../Source/Toolchain/ExecutableLocator.ts";

class FakeToolchain implements IToolchain {
    public readonly Name = "Fake";
    public readonly Platform = "Test";
    public FindCompiler(): string { return "fake-cc"; }
    public FindLinker(): string { return "fake-link"; }
    public FindArchiver(): string { return "fake-ar"; }
    public GetSystemIncludePaths(): string[] { return []; }
    public GetSystemLibPaths(): string[] { return []; }
    public MakeCompileCommand(Source: string, Output: string): string[] { return ["/flag", Source, Output]; }
    public MakeLinkCommand(Output: string, Objects: string[]): string[] { return [Output, ...Objects]; }
    public MakeArchiveCommand(Output: string, Objects: string[]): string[] { return [Output, ...Objects]; }
    public async GetEnvironment(): Promise<Record<string, string>> { return { ...process.env } as Record<string, string>; }
}

class ConfiguredModule extends ModuleBuild {
    public readonly Name: string;
    private readonly ConfigureModule: (Configuration: ModuleConfiguration) => void;

    public constructor(
        Name: string,
        ConfigureModule: (Configuration: ModuleConfiguration) => void,
    ) {
        super();
        this.Name = Name;
        this.ConfigureModule = ConfigureModule;
    }
    public Configure(_Target: Target, Configuration: ModuleConfiguration): void {
        this.ConfigureModule(Configuration);
    }
}

const Target: Target = {
    Platform: Platform.Win64,
    Optimization: Optimization.Debug,
    TargetType: TargetType.Program,
};

function Descriptor(Modules: string[], EntryModule: string): TargetDescriptor {
    return {
        Name: "CustomTarget",
        Modules,
        EntryModule,
        Matrix: [Target],
        OutputName: () => "CustomTargetDebug",
    };
}

async function BuildFixture(
    Root: string,
    Modules: ModuleInstance[],
): Promise<{ Actions: BuildAction[]; BuildTarget: ResolvedTarget }> {
    const BuildTarget = ResolveTarget(Descriptor(Modules.map((M) => M.Descriptor.Name), "Entry"), Target);
    const Graph = new DependencyGraph(Modules, BuildTarget);
    const Resolved = Graph.Build();
    return {
        Actions: await new IRBuilder(new EnginePaths(Root), new FakeToolchain(), BuildTarget, Graph).Build(Resolved),
        BuildTarget,
    };
}

Test("typed custom actions produce custom-only work and compile generated sources before they exist", async (Context) => {
    const Root = await Fs.mkdtemp(Path.join(Os.tmpdir(), "limitless-custom-ir-"));
    Context.after(() => Fs.rm(Root, { recursive: true, force: true }));
    const ToolsRoot = Path.join(Root, "Engine", "Source", "Programs", "ToolsLeaf");
    const EntryRoot = Path.join(Root, "Engine", "Source", "Programs", "OddLeaf");
    await Fs.mkdir(Path.join(EntryRoot, "Private"), { recursive: true });
    await Fs.writeFile(Path.join(EntryRoot, "Private", "Main.cpp"), "int main() { return 0; }\n");

    const Modules: ModuleInstance[] = [
        {
            Descriptor: new ConfiguredModule("Tools", (Configuration) => {
                Configuration.Output = OutputType.None;
                Configuration.CustomActions.push({
                    Id: "Stamp", Inputs: [], Outputs: ["[module.Generated]/tool.stamp"],
                    Command: ["tool", "/flag", "[module.Generated]/tool.stamp"],
                });
                Configuration.CustomActions.push({
                    Id: "BinaryStamp", Inputs: [], Outputs: ["[engine.Binaries]/tool.stamp"],
                    Command: ["tool", "[engine.Binaries]/tool.stamp"],
                });
            }),
            SourceRoot: ToolsRoot,
            BuildFilePath: Path.join(ToolsRoot, "Build.ts"),
        },
        {
            Descriptor: new ConfiguredModule("Entry", (Configuration) => {
                Configuration.PrivateDependencies.push("Tools");
                Configuration.CustomActions.push(
                    {
                        Id: "GenerateOne",
                        Inputs: ["[module.SourceRoot]/schema.idl"],
                        Outputs: ["[module.Generated]/one/foo.cpp", "[module.Generated]/one/foo.h"],
                        Command: ["generator", "/flag", "[module.Generated]/one/foo.cpp"],
                        WorkingDirectory: "[module.SourceRoot]",
                        RunBeforeCompile: true,
                    },
                    {
                        Id: "GenerateTwo",
                        Inputs: [],
                        Outputs: ["[engine.Temp]/other/foo.cpp"],
                        Command: ["generator", "[engine.Temp]/other/foo.cpp"],
                        DependsOn: ["GenerateOne", "Tools::custom::Stamp"],
                    },
                );
            }),
            SourceRoot: EntryRoot,
            BuildFilePath: Path.join(EntryRoot, "Build.ts"),
        },
    ];

    const { Actions } = await BuildFixture(Root, Modules);
    const ToolsAction = Actions.find((A) => A.Id === "Tools::custom::Stamp");
    const BinaryStamp = Actions.find((A) => A.Id === "Tools::custom::BinaryStamp");
    const GenerateOne = Actions.find((A) => A.Id === "Entry::custom::GenerateOne");
    const GenerateTwo = Actions.find((A) => A.Id === "Entry::custom::GenerateTwo");
    Assert.ok(ToolsAction, "Output.None modules must still emit custom actions");
    Assert.equal(
        BinaryStamp?.Outputs[0],
        Path.join(Root, "Engine", "Binaries", "Win64", "Debug", "CustomTarget", "Program", "tool.stamp"),
        "[engine.Binaries] must be scoped to the active configuration",
    );
    Assert.ok(GenerateOne);
    Assert.ok(GenerateTwo);
    Assert.equal(GenerateOne.Command[1], "/flag", "opaque command options must not be path-normalized");
    const VariantTemp = Path.join(
        Root, "Engine", "Intermediate", "Build", "Win64", "Debug", "CustomTarget", "Program",
    );
    Assert.equal(GenerateOne.Outputs[0], Path.join(VariantTemp, "Generated", "Entry", "one", "foo.cpp"));
    Assert.equal(GenerateTwo.Outputs[0], Path.join(VariantTemp, "other", "foo.cpp"));
    Assert.deepEqual(GenerateTwo.DependsOn, ["Entry::custom::GenerateOne", "Tools::custom::Stamp"]);

    const GeneratedCompiles = Actions.filter((A) =>
        A.Type === "compile" && A.Inputs[0].endsWith("foo.cpp"));
    Assert.equal(GeneratedCompiles.length, 2);
    Assert.notEqual(GeneratedCompiles[0].Id, GeneratedCompiles[1].Id);
    Assert.notEqual(GeneratedCompiles[0].Outputs[0], GeneratedCompiles[1].Outputs[0]);
    for (const Compile of GeneratedCompiles) {
        Assert.ok(Compile.DependsOn.some((Id) => Id.startsWith("Entry::custom::Generate")));
    }
    const MainCompile = Actions.find((A) => A.Type === "compile" && A.Inputs[0].endsWith("Main.cpp"));
    Assert.ok(MainCompile?.DependsOn.includes("Entry::custom::GenerateOne"));
    Assert.ok(MainCompile?.ImplicitInputs?.includes(GenerateOne.Outputs[1]));
});

async function ExpectBadDescriptor(Mutate: (Action: CustomActionDescriptor) => void, Pattern: RegExp): Promise<void> {
    const Root = await Fs.mkdtemp(Path.join(Os.tmpdir(), "limitless-custom-bad-"));
    try {
        const EntryRoot = Path.join(Root, "Engine", "Source", "Programs", "Entry");
        await Fs.mkdir(Path.join(EntryRoot, "Private"), { recursive: true });
        await Fs.writeFile(Path.join(EntryRoot, "Private", "Main.cpp"), "int main() { return 0; }\n");
        const Action: CustomActionDescriptor = {
            Id: "Generate", Inputs: [], Outputs: ["[module.Generated]/out.txt"], Command: ["tool"],
        };
        Mutate(Action);
        const Modules: ModuleInstance[] = [{
            Descriptor: new ConfiguredModule("Entry", (Configuration) => Configuration.CustomActions.push(Action)),
            SourceRoot: EntryRoot, BuildFilePath: Path.join(EntryRoot, "Build.ts"),
        }];
        await Assert.rejects(() => BuildFixture(Root, Modules), Pattern);
    } finally {
        await Fs.rm(Root, { recursive: true, force: true });
    }
}

Test("custom descriptor validation rejects unsafe schema and graph ambiguity", async () => {
    await ExpectBadDescriptor((A) => { A.Command = []; }, /empty command/);
    await ExpectBadDescriptor((A) => { A.Outputs = []; }, /no outputs/);
    await ExpectBadDescriptor((A) => { A.Id = "bad::id"; }, /unsafe custom action id/);
    await ExpectBadDescriptor((A) => { A.Outputs = ["[engine.Temp]/..\/..\/BuildEvil/out.txt"]; }, /escapes/);
    await ExpectBadDescriptor((A) => { A.DependsOn = ["Missing"]; }, /unknown action/);
    await ExpectBadDescriptor((A) => { A.Command = ["tool\nattack"]; }, /control character/);
});

function Action(Root: string, Id: string, Overrides: Partial<BuildAction> = {}): BuildAction {
    return {
        Id, Type: "custom", Inputs: [], Outputs: [Path.join(Root, `${Id}.out`)],
        Command: ["tool"], WorkingDirectory: Root, DependsOn: [], Description: Id,
        ...Overrides,
    };
}

Test("whole action graph rejects duplicate ids/outputs and self/unknown/cyclic dependencies", () => {
    const Root = Path.resolve(Os.tmpdir(), "limitless-action-graph");
    Assert.throws(() => ValidateAndSortActions([Action(Root, "A"), Action(Root, "A")]), /Duplicate build action id/);
    Assert.throws(() => ValidateAndSortActions([
        Action(Root, "A"), Action(Root, "B", { Outputs: [Path.join(Root, "A.out")] }),
    ]), /both produce/);
    Assert.throws(() => ValidateAndSortActions([Action(Root, "A", { DependsOn: ["A"] })]), /depends on itself/);
    Assert.throws(() => ValidateAndSortActions([Action(Root, "A", { DependsOn: ["Missing"] })]), /unknown action/);
    Assert.throws(() => ValidateAndSortActions([
        Action(Root, "A", { DependsOn: ["B"] }), Action(Root, "B", { DependsOn: ["A"] }),
    ]), /cycle/);
    Assert.throws(() => ValidateAndSortActions([
        Action(Root, "A", { Outputs: [Path.join(Root, "bad\0name")] }),
    ]), /control character/);
});

Test("Ninja serializes custom multi-output edges, all dependency classes, escaping, cwd, and exact default", async (Context) => {
    const Root = await Fs.mkdtemp(Path.join(Os.tmpdir(), "limitless-custom-ninja-"));
    Context.after(() => Fs.rm(Root, { recursive: true, force: true }));
    const WeirdOutput = Path.join(Root, "generated $ :|#.cpp");
    const Header = Path.join(Root, "generated header.h");
    const DependencyOutput = Path.join(Root, "dependency.stamp");
    const Object = Path.join(Root, "generated.obj");
    const Exe = Path.join(Root, "resolved.exe");
    const Actions: BuildAction[] = [
        Action(Root, "Dependency", { Outputs: [DependencyOutput] }),
        Action(Root, "Generate", {
            Outputs: [WeirdOutput, Header], Command: ["tool$bin", "arg with space"],
            DependsOn: ["Dependency"], ImplicitInputs: [DependencyOutput], Description: "GEN $files",
        }),
        Action(Root, "Compile", {
            Type: "compile", Inputs: [WeirdOutput], Outputs: [Object],
            Command: ["compiler", WeirdOutput, Object], DependsOn: ["Generate"], ImplicitInputs: [Header],
        }),
        Action(Root, "CustomTarget::exe", {
            Type: "link", Inputs: [Object], Outputs: [Exe], Command: ["linker", Object, Exe], DependsOn: ["Compile"],
        }),
    ];
    const BuildTarget = ResolveTarget(Descriptor(["Entry"], "Entry"), Target);
    const Result = await new NinjaBackend(new FakeToolchain()).Generate(Actions, BuildTarget, { OutputDir: Path.join(Root, "ninja") });
    const Ninja = await Fs.readFile(Result.NinjaPath, "utf-8");
    Assert.match(Ninja, /rule custom[\s\S]*restat = 1/);
    Assert.match(Ninja, /le_path_[0-9a-f]{8} = .*generated\$ \$\$\$ \$:\|#\.cpp/);
    Assert.match(Ninja, /build \$le_path_[0-9a-f]{8} .*generated\$ header\.h: custom/);
    Assert.match(Ninja, /\| .*dependency\.stamp/);
    Assert.match(Ninja, process.platform === "win32"
        ? /Cmd = cmd\.exe .*cd .*&& "tool\$\$bin" "arg with space"/
        : /Cmd = \/bin\/sh .*cd .*&& .*tool\$\$bin.*arg with space/);
    Assert.match(Ninja, /Desc = GEN \$\$files/);
    Assert.ok(Ninja.includes(`default ${Exe.replace(/\\/g, "/").replace(/:/g, () => "$:")}`));
    const CompileCommands = JSON.parse(await Fs.readFile(Result.CompileCommandsPath, "utf-8")) as object[];
    Assert.equal(CompileCommands.length, 1);
    await Fs.access(Path.dirname(WeirdOutput));
    const NinjaExecutable = FindNinjaExecutable();
    if (NinjaExecutable) {
        const Parse = spawnSync(NinjaExecutable, ["-f", Result.NinjaPath, "-t", "targets"], {
            cwd: Path.dirname(Result.NinjaPath), encoding: "utf-8", shell: false,
        });
        Assert.equal(Parse.status, 0, `${Parse.stdout}\n${Parse.stderr}`);
    }
});
