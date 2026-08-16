import * as Assert from "node:assert/strict";
import * as Fs from "node:fs/promises";
import * as Os from "node:os";
import * as Path from "node:path";
import Test from "node:test";
import { ModuleBuild } from "../Source/Configuration/ModuleBuild.ts";
import { ResolveTarget } from "../Source/Configuration/Target.ts";
import {
    Optimization, OutputType, Platform, TargetType,
    type ModuleConfiguration, type ModuleInstance, type Target, type TargetDescriptor,
} from "../Source/Configuration/Types.ts";
import { DependencyGraph } from "../Source/Graph/DependencyGraph.ts";
import { EnginePaths } from "../Source/Project/EnginePaths.ts";
import { XcodeProjectGenerator } from "../Source/Project/XcodeProjectGenerator.ts";

class GeneratedBuild extends ModuleBuild {
    public readonly Name = "Generated";
    private readonly Mismatch: boolean;
    public constructor(Mismatch = false) { super(); this.Mismatch = Mismatch; }
    public Configure(Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.CustomActions.push({
            Id: "Generate-Code.v1",
            Inputs: ["[module.SourceRoot]/Public/Input.h"],
            Outputs: [
                Target.Optimization === Optimization.Release && this.Mismatch
                    ? "[module.Generated]/Generated.cc"
                    : "[module.Generated]/Generated.cpp",
                "[module.Generated]/Generated.mm",
            ],
            Command: [process.execPath, "tool.mjs", "[module.Generated]/Generated.cpp"],
            WorkingDirectory: "[engine.Root]",
            RunBeforeCompile: true,
        });
    }
}

class EntryBuild extends ModuleBuild {
    public readonly Name = "Entry";
    public Configure(_Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.Output = OutputType.Lib;
        Configuration.PublicDependencies.push("Generated");
    }
}

function Descriptor(): TargetDescriptor {
    return {
        Name: "XcodeCustom",
        Modules: ["Generated", "Entry"],
        EntryModule: "Entry",
        Matrix: [
            { Platform: Platform.Mac, Optimization: Optimization.Debug, TargetType: TargetType.Game },
            { Platform: Platform.Mac, Optimization: Optimization.Release, TargetType: TargetType.Game },
        ],
        OutputName: () => "XcodeCustomGame",
    };
}

async function Modules(Root: string, Mismatch = false): Promise<ModuleInstance[]> {
    const GeneratedRoot = Path.join(Root, "Engine", "Source", "Runtime", "Generated");
    const EntryRoot = Path.join(Root, "Engine", "Source", "Runtime", "Entry");
    await Fs.mkdir(Path.join(GeneratedRoot, "Public"), { recursive: true });
    await Fs.mkdir(Path.join(EntryRoot, "Private"), { recursive: true });
    await Fs.writeFile(Path.join(GeneratedRoot, "Public", "Input.h"), "#pragma once\n");
    await Fs.writeFile(Path.join(EntryRoot, "Private", "Main.cpp"), "int main() { return 0; }\n");
    return [
        { Descriptor: new GeneratedBuild(Mismatch), SourceRoot: GeneratedRoot, BuildFilePath: Path.join(GeneratedRoot, "Build.ts") },
        { Descriptor: new EntryBuild(), SourceRoot: EntryRoot, BuildFilePath: Path.join(EntryRoot, "Build.ts") },
    ];
}

Test("Xcode projects run typed actions before Sources and compile a stable Debug/Release bridge", async (Context) => {
    const Root = await Fs.mkdtemp(Path.join(Os.tmpdir(), "limitless-xcode-custom-"));
    Context.after(() => Fs.rm(Root, { recursive: true, force: true }));
    const Paths = new EnginePaths(Root);
    const ModuleInstances = await Modules(Root);
    const TargetDescriptor = Descriptor();
    const DebugTarget = ResolveTarget(TargetDescriptor, TargetDescriptor.Matrix[0]);
    const ReleaseTarget = ResolveTarget(TargetDescriptor, TargetDescriptor.Matrix[1]);
    const DebugModules = new DependencyGraph(ModuleInstances, DebugTarget).Build();
    const ReleaseModules = new DependencyGraph(ModuleInstances, ReleaseTarget).Build();
    const MissingDebugOutput = Path.join(
        Paths.GeneratedOutputDirectory(DebugTarget), "Generated", "Generated.cpp",
    );
    Assert.equal(await Fs.access(MissingDebugOutput).then(() => true, () => false), false);

    const ProjectPath = await new XcodeProjectGenerator(Paths).Generate({
        Debug: DebugModules, Release: ReleaseModules,
    }, DebugTarget);
    const Project = await Fs.readFile(Path.join(ProjectPath, "project.pbxproj"), "utf-8");
    const BridgeDirectory = Path.join(
        Paths.ProjectFilesDirectory, "XcodeGenerated", "XcodeCustom", "Game",
    );
    const AllBridgeNames = await Fs.readdir(BridgeDirectory);
    const BridgeNames = AllBridgeNames.filter((Name) => Name.endsWith(".bridge.cpp"));
    Assert.equal(BridgeNames.length, 1);
    Assert.equal(AllBridgeNames.filter((Name) => Name.endsWith(".bridge.mm")).length, 1);
    const BridgePath = Path.join(BridgeDirectory, BridgeNames[0]);
    const Bridge = await Fs.readFile(BridgePath, "utf-8");
    Assert.match(Bridge, /\.\.\/\.\.\/\.\.\/\.\.\/Build\/Mac\/Debug\/XcodeCustom\/Game\/Generated\/Generated\/Generated\.cpp/);
    Assert.match(Bridge, /\.\.\/\.\.\/\.\.\/\.\.\/Build\/Mac\/Release\/XcodeCustom\/Game\/Generated\/Generated\/Generated\.cpp/);
    Assert.match(Bridge, /Xcode generated-source bridge requires DEBUG or NDEBUG/);
    Assert.equal(Bridge.includes(Root.replaceAll("\\", "/")), false);
    Assert.doesNotMatch(Bridge, /[A-Za-z]:\//);
    Assert.match(Project, /PBXShellScriptBuildPhase/);
    Assert.match(Project, /alwaysOutOfDate = 1/);
    Assert.match(Project, /ENABLE_USER_SCRIPT_SANDBOXING = NO/);
    Assert.ok(Project.includes(
        "build-action --target 'XcodeCustom' --platform Mac --config \\\"${CONFIGURATION}\\\" --type 'Game' --id 'Generated::custom::Generate-Code.v1'",
    ));
    Assert.match(Project, /Unsupported Xcode configuration/);
    const BuildPhases = Project.match(/buildPhases = \(([\s\S]*?)\);/)?.[1] ?? "";
    Assert.ok(BuildPhases.indexOf("Generated::custom::Generate-Code.v1") < BuildPhases.indexOf("Sources"));
    Assert.match(Project, /Generated__custom__Generate-Code\.v1\.0\.[0-9A-F]{8}\.bridge\.cpp in Sources/);
    Assert.match(Project, /Generated__custom__Generate-Code\.v1\.1\.[0-9A-F]{8}\.bridge\.mm in Sources/);
    Assert.equal(await Fs.access(MissingDebugOutput).then(() => true, () => false), false,
        "project generation must not eagerly run the custom action");
});

Test("Xcode rejects Debug/Release custom action output shape drift", async (Context) => {
    const Root = await Fs.mkdtemp(Path.join(Os.tmpdir(), "limitless-xcode-custom-mismatch-"));
    Context.after(() => Fs.rm(Root, { recursive: true, force: true }));
    const ModuleInstances = await Modules(Root, true);
    const TargetDescriptor = Descriptor();
    const DebugTarget = ResolveTarget(TargetDescriptor, TargetDescriptor.Matrix[0]);
    const ReleaseTarget = ResolveTarget(TargetDescriptor, TargetDescriptor.Matrix[1]);
    await Assert.rejects(() => new XcodeProjectGenerator(new EnginePaths(Root)).Generate({
        Debug: new DependencyGraph(ModuleInstances, DebugTarget).Build(),
        Release: new DependencyGraph(ModuleInstances, ReleaseTarget).Build(),
    }, DebugTarget), /different Debug\/Release output shape/);
});
