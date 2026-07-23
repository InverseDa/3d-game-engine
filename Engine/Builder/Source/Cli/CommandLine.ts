import * as Path from "node:path";
import { NinjaBackend } from "../Backend/NinjaBackend.ts";
import type { BackendResult, IBackend } from "../Backend/IBackend.ts";
import { IRBuilder } from "../Build/IRBuilder.ts";
import { Optimization, Platform, TargetType } from "../Configuration/Types.ts";
import type { BuildAction, Target } from "../Configuration/Types.ts";
import { ModuleLoader, TargetLoader } from "../Discovery/ModuleLoader.ts";
import { DependencyGraph } from "../Graph/DependencyGraph.ts";
import { EnginePaths, FindProjectRoot, ToPosixPath } from "../Project/EnginePaths.ts";
import { VcxprojGenerator } from "../Project/VcxprojGenerator.ts";
import { XcodeProjectGenerator } from "../Project/XcodeProjectGenerator.ts";
import { MSVCToolchain } from "../Toolchain/MSVCToolchain.ts";
import type { IToolchain } from "../Toolchain/IToolchain.ts";
import { Logger } from "../Utilities/Logger.ts";

interface ParsedArguments {
    Command: string;
    Subcommand: string;
    Flags: Record<string, string>;
}

function ParseArguments(Arguments: string[]): ParsedArguments {
    const Values = Arguments.slice(2);
    const Command = Values[0] ?? "help";
    const HasSubcommand = Command === "list";
    const Subcommand = HasSubcommand ? Values[1] ?? "" : "";
    const Flags: Record<string, string> = {};

    for (let Index = HasSubcommand ? 2 : 1; Index < Values.length; Index++) {
        const Argument = Values[Index];
        if (!Argument.startsWith("--")) {
            continue;
        }

        const Key = Argument.slice(2);
        const NextArgument = Values[Index + 1];
        if (NextArgument && !NextArgument.startsWith("--")) {
            Flags[Key] = NextArgument;
            Index++;
        } else {
            Flags[Key] = "true";
        }
    }
    return { Command, Subcommand, Flags };
}

async function ListModules(Paths: EnginePaths): Promise<void> {
    const Loader = new ModuleLoader(Paths.SourceDirectory);
    Logger.Info(`Scanning ${ToPosixPath(Paths.SourceDirectory)} for Build.ts ...`);
    const Modules = await Loader.DiscoverModules();
    if (Modules.length === 0) {
        Logger.Warn("No modules found.");
        Logger.Dim("Hint: add a Build.ts file that exports a ModuleBuild class.");
        return;
    }

    Logger.Success(`Found ${Modules.length} module(s):`);
    console.log("");
    const SortedModules = Modules.sort((Left, Right) =>
        Left.Descriptor.Name.localeCompare(Right.Descriptor.Name),
    );
    for (const Module of SortedModules) {
        const Kind = Module.Descriptor.ThirdParty ? "3rd-party" : "runtime";
        const RelativePath = ToPosixPath(Path.relative(Paths.Root, Module.BuildFilePath));
        console.log(`    ${Module.Descriptor.Name.padEnd(14)} ${`(${Kind})`.padEnd(12)} ${RelativePath}`);
    }
    console.log("");
    Logger.Dim(`Total: ${Modules.length} modules`);
}

async function ListTargets(Paths: EnginePaths): Promise<void> {
    const Loader = new TargetLoader(Paths.TargetsDirectory);
    Logger.Info(`Scanning ${ToPosixPath(Paths.TargetsDirectory)} for *.target.ts ...`);
    const Targets = await Loader.DiscoverTargets();
    if (Targets.length === 0) {
        Logger.Warn("No targets found.");
        Logger.Dim(`Hint: create ${ToPosixPath(Paths.TargetsDirectory)}/Limitless.target.ts`);
        return;
    }

    Logger.Success(`Found ${Targets.length} target(s):`);
    console.log("");
    for (const Target of Targets) {
        console.log(`    ${Target.Name}`);
        console.log(`        Entry:    ${Target.EntryModule}`);
        console.log(`        Modules:  ${Target.Modules.join(", ")}`);
        console.log(`        Matrix:   ${Target.Matrix.length} configuration(s)`);
        console.log("");
    }
}

function PrintHelp(): void {
    console.log(`
LimitlessBuilder - Limitless Engine Build System

Usage:
    limitless-builder <command> [options]

Commands:
    list modules              List all discovered Build.ts modules
    list targets              List all discovered *.target.ts targets
    generate [options]        Generate build.ninja + compile_commands.json
    build [options]           Generate and run ninja
    sln [options]             Generate Visual Studio .sln + .vcxproj
    xcode [options]           Generate a native Xcode project for macOS
    help                      Show this help message

Options:
    --platform <name>         Win64 | Mac
    --config <name>           Debug | Release
    --type <name>             Game | Editor
    --jobs <n>                Parallel jobs (build only)
    --verbose                 Verbose output

Examples:
    limitless-builder list modules
    limitless-builder generate --platform Win64 --config Debug --type Game
    limitless-builder build --platform Win64 --config Debug --type Game
    limitless-builder xcode --platform Mac --config Debug --type Game
`);
}

function ParseTarget(Flags: Record<string, string>): Target {
    const RequestedPlatform = (Flags.platform ?? "Win64") as Platform;
    const RequestedOptimization = (Flags.config ?? "Debug") as Optimization;
    const RequestedTargetType = (Flags.type ?? "Game") as TargetType;
    if (!Object.values(Platform).includes(RequestedPlatform)) {
        throw new Error(`Invalid platform: ${RequestedPlatform} (Win64|Mac)`);
    }
    if (!Object.values(Optimization).includes(RequestedOptimization)) {
        throw new Error(`Invalid config: ${RequestedOptimization} (Debug|Release)`);
    }
    if (!Object.values(TargetType).includes(RequestedTargetType)) {
        throw new Error(`Invalid type: ${RequestedTargetType} (Game|Editor)`);
    }
    return {
        Platform: RequestedPlatform,
        Optimization: RequestedOptimization,
        TargetType: RequestedTargetType,
    };
}

async function GenerateIR(
    Paths: EnginePaths,
    Target: Target,
): Promise<{ Actions: BuildAction[]; Backend: IBackend; Result: BackendResult }> {
    const Loader = new ModuleLoader(Paths.SourceDirectory);
    const Modules = await Loader.DiscoverModules();
    if (Modules.length === 0) {
        throw new Error("No modules found.");
    }
    Logger.Success(`Loaded ${Modules.length} modules`);

    const Graph = new DependencyGraph(Modules, Target);
    const SortedModules = Graph.Build();
    Logger.Success(`Dependency graph resolved (${SortedModules.length} modules, topo-sorted)`);
    for (const Module of SortedModules) {
        const Dependencies = Module.Configuration.PublicDependencies.map((Dependency) =>
            typeof Dependency === "string" ? Dependency : `${Dependency.Name}~`,
        ).join(", ");
        Logger.Dim(`    ${Module.Instance.Descriptor.Name.padEnd(12)} -> ${Module.Configuration.Output.padEnd(6)} deps: ${Dependencies || "none"}`);
    }

    let Toolchain: IToolchain;
    if (Target.Platform === Platform.Win64) {
        Toolchain = new MSVCToolchain();
    } else {
        throw new Error(`Platform ${Target.Platform} is not implemented yet.`);
    }
    Logger.Success(`Toolchain: ${Toolchain.Name}`);

    const Builder = new IRBuilder(Paths, Toolchain, Target, Graph);
    const Actions = await Builder.Build(SortedModules);
    const OutputDirectory = Paths.TemporaryOutputDirectory(Target.Platform, Target.Optimization);
    const Backend = new NinjaBackend(Toolchain);
    const Result = await Backend.Generate(Actions, Target, { OutputDir: OutputDirectory });
    return { Actions, Backend, Result };
}

async function Generate(Paths: EnginePaths, Flags: Record<string, string>): Promise<void> {
    const Target = ParseTarget(Flags);
    Logger.Info(`Target: ${Target.Platform} / ${Target.Optimization} / ${Target.TargetType}`);
    const { Actions, Result } = await GenerateIR(Paths, Target);

    console.log("");
    Logger.Success(`IR generated: ${Actions.length} actions -> build.ninja`);
    Logger.Dim(`    Compile: ${Result.CompileCount}, Link/Archive: ${Result.LinkCount}`);
    Logger.Dim(`    build.ninja:      ${ToPosixPath(Result.NinjaPath)}`);
    Logger.Dim(`    compile_commands: ${ToPosixPath(Result.CompileCommandsPath)}`);
    if (Flags.verbose === "true") {
        console.log("");
        for (const Action of Actions) {
            console.log(`    [${Action.Type.padEnd(7)}] ${Action.Description}`);
        }
    }
}

async function Build(Paths: EnginePaths, Flags: Record<string, string>): Promise<void> {
    const Target = ParseTarget(Flags);
    Logger.Info(`Target: ${Target.Platform} / ${Target.Optimization} / ${Target.TargetType}`);
    const { Backend, Result } = await GenerateIR(Paths, Target);

    console.log("");
    Logger.Success(`build.ninja: ${ToPosixPath(Result.NinjaPath)}`);
    Logger.Dim(`    Compile: ${Result.CompileCount}, Link/Archive: ${Result.LinkCount}`);
    Logger.Info("Building...");
    const ExitCode = await Backend.Build?.(Result, {
        Jobs: Flags.jobs ? Number.parseInt(Flags.jobs, 10) : undefined,
        Verbose: Flags.verbose === "true",
    });
    if (ExitCode === 0) {
        Logger.Success("Build succeeded.");
        return;
    }
    throw new Error(`Build failed (exit code ${ExitCode ?? 1}).`);
}

async function GenerateSolution(Paths: EnginePaths, Flags: Record<string, string>): Promise<void> {
    const Target = ParseTarget(Flags);
    Logger.Info(`Target: ${Target.Platform} / ${Target.Optimization} / ${Target.TargetType}`);
    const Modules = await new ModuleLoader(Paths.SourceDirectory).DiscoverModules();
    if (Modules.length === 0) {
        throw new Error("No modules found.");
    }

    const Graph = new DependencyGraph(Modules, Target);
    if (Target.Platform !== Platform.Win64) {
        throw new Error(`Visual Studio solution generation is not implemented for ${Target.Platform}.`);
    }
    const Toolchain = new MSVCToolchain();
    const SolutionPath = await new VcxprojGenerator(Paths, Graph, Toolchain).Generate(Graph.Build(), Target);
    console.log("");
    Logger.Success("Generated Visual Studio solution:");
    Logger.Dim(`    ${ToPosixPath(SolutionPath)}`);
    Logger.Dim("    Open in Visual Studio and press F7 to build.");
}

async function GenerateXcodeProject(Paths: EnginePaths, Flags: Record<string, string>): Promise<void> {
    const Target = ParseTarget(Flags);
    Logger.Info(`Target: ${Target.Platform} / ${Target.Optimization} / ${Target.TargetType}`);
    if (Target.Platform !== Platform.Mac) {
        throw new Error(`Xcode project generation requires --platform Mac.`);
    }

    const Modules = await new ModuleLoader(Paths.SourceDirectory).DiscoverModules();
    if (Modules.length === 0) {
        throw new Error("No modules found.");
    }
    const Graph = new DependencyGraph(Modules, Target);
    const ProjectPath = await new XcodeProjectGenerator(Paths).Generate(Graph.Build(), Target);
    console.log("");
    Logger.Success("Generated Xcode project:");
    Logger.Dim(`    ${ToPosixPath(ProjectPath)}`);
    Logger.Dim("    Open it in Xcode or run xcodebuild -project LimitlessEngine.xcodeproj -scheme LimitlessEngine.");
}

async function Main(): Promise<void> {
    const Arguments = ParseArguments(process.argv);
    const Paths = new EnginePaths(FindProjectRoot(process.cwd()));
    switch (Arguments.Command) {
        case "list":
            if (Arguments.Subcommand === "modules") {
                await ListModules(Paths);
            } else if (Arguments.Subcommand === "targets") {
                await ListTargets(Paths);
            } else {
                PrintHelp();
            }
            break;
        case "generate":
            await Generate(Paths, Arguments.Flags);
            break;
        case "build":
            await Build(Paths, Arguments.Flags);
            break;
        case "sln":
            await GenerateSolution(Paths, Arguments.Flags);
            break;
        case "xcode":
            await GenerateXcodeProject(Paths, Arguments.Flags);
            break;
        case "help":
        case "--help":
        case "-h":
            PrintHelp();
            break;
        default:
            throw new Error(`Unknown command: ${Arguments.Command}`);
    }
}

Main().catch((CaughtError) => {
    Logger.Error(CaughtError instanceof Error ? CaughtError.message : String(CaughtError));
    process.exit(1);
});
