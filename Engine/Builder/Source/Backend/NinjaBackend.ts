import * as ChildProcess from "node:child_process";
import * as Fs from "node:fs/promises";
import * as Path from "node:path";
import type { BuildAction, ResolvedTarget } from "../Configuration/Types.ts";
import { ValidateAndSortActions } from "../Build/ActionGraph.ts";
import { FindNinjaExecutable } from "../Toolchain/ExecutableLocator.ts";
import type { IToolchain } from "../Toolchain/IToolchain.ts";
import type { BackendResult, BuildOptions, GenerateOptions, IBackend } from "./IBackend.ts";

function QuoteToken(Token: string): string {
    if (process.platform !== "win32") {
        return `'${Token.replaceAll("'", "'\"'\"'")}'`;
    }

    // Windows CommandLineToArgvW/CRT quoting. Quoting every token also keeps
    // cmd.exe metacharacters such as &, |, < and > inert.
    let Result = '"';
    let Backslashes = 0;
    for (const Character of Token) {
        if (Character === "\\") {
            Backslashes++;
            continue;
        }
        if (Character === '"') Result += "\\".repeat(Backslashes * 2 + 1) + '"';
        else Result += "\\".repeat(Backslashes) + Character;
        Backslashes = 0;
    }
    return Result + "\\".repeat(Backslashes * 2) + '"';
}

function ToNinjaPath(FilePath: string): string {
    if (FilePath.includes("|")) throw new Error("Ninja pipe paths must be emitted through a path variable.");
    return FilePath.replace(/\\/g, "/")
        .replace(/\$/g, () => "$$")
        .replace(/ /g, () => "$ ")
        .replace(/:/g, () => "$:");
}

function ToNinjaPathVariableValue(FilePath: string): string {
    return FilePath.replace(/\\/g, "/")
        .replace(/\$/g, () => "$$")
        .replace(/ /g, () => "$ ")
        .replace(/:/g, () => "$:");
}

function EscapeNinjaValue(Value: string): string {
    if (/\r|\n/.test(Value)) throw new Error("Ninja variable values must not contain newlines.");
    return Value.replace(/\$/g, () => "$$");
}

function BuildShellCommand(WorkingDirectory: string, Command: string[]): string {
    if (process.platform === "win32") {
        // cmd.exe expands %NAME% even inside quotes. A caret before each percent
        // suppresses that expansion only outside quotes. Quote each segment and
        // join it with an escaped, unquoted percent; cmd concatenates the pieces
        // into one argv token and removes the carets.
        const QuoteCmdToken = (Value: string): string =>
            Value.split("%").map(QuoteToken).join("^%");
        const Inner = `cd /d ${QuoteCmdToken(WorkingDirectory)} && ${Command.map(QuoteCmdToken).join(" ")}`;
        return `cmd.exe /d /s /c "${Inner}"`;
    }
    const Inner = `cd -- '${WorkingDirectory.replaceAll("'", "'\"'\"'")}' && ${Command.map(QuoteToken).join(" ")}`;
    return `/bin/sh -c ${QuoteToken(Inner)}`;
}

export class NinjaBackend implements IBackend {
    public readonly Name = "Ninja";
    private readonly Toolchain: IToolchain;

    public constructor(Toolchain: IToolchain) {
        this.Toolchain = Toolchain;
    }

    public async Generate(
        Actions: BuildAction[],
        BuildTarget: ResolvedTarget,
        Options: GenerateOptions,
    ): Promise<BackendResult> {
        const SortedActions = ValidateAndSortActions(Actions);
        await Fs.mkdir(Options.OutputDir, { recursive: true });
        const OutputDirectories = new Set(SortedActions.flatMap((Action) =>
            Action.Outputs.map((Output) => Path.dirname(Output))));
        await Promise.all([...OutputDirectories].map((Directory) =>
            Fs.mkdir(Directory, { recursive: true })));

        const NinjaPath = Path.join(Options.OutputDir, "build.ninja");
        await Fs.writeFile(NinjaPath, this.BuildNinjaFile(SortedActions, BuildTarget), "utf-8");

        const CompileCommandsPath = Options.CompileCommandsPath
            ?? Path.join(Options.OutputDir, "compile_commands.json");
        await Fs.writeFile(
            CompileCommandsPath,
            JSON.stringify(this.BuildCompileCommands(SortedActions), null, 2),
            "utf-8",
        );

        return {
            Actions: SortedActions,
            NinjaPath,
            CompileCommandsPath,
            CompileCount: SortedActions.filter((Action) => Action.Type === "compile").length,
            LinkCount: SortedActions.filter((Action) => Action.Type === "link" || Action.Type === "archive").length,
        };
    }

    public async Build(Result: BackendResult, Options: BuildOptions): Promise<number> {
        const NinjaExecutable = FindNinjaExecutable();
        if (!NinjaExecutable) {
            throw new Error("ninja was not found on PATH. Install Ninja or set LIMITLESS_BUILDER_NINJA to its executable path.");
        }

        const Environment = await this.Toolchain.GetEnvironment();
        const Arguments = ["-f", Result.NinjaPath];
        if (Options.Jobs) {
            Arguments.push("-j", String(Options.Jobs));
        }
        if (Options.Verbose) {
            Arguments.push("-v");
        }

        return new Promise((Resolve) => {
            const Process = ChildProcess.spawn(NinjaExecutable, Arguments, {
                cwd: Path.dirname(Result.NinjaPath),
                env: Environment,
                shell: false,
                stdio: "inherit",
            });
            Process.on("close", (ExitCode) => Resolve(ExitCode ?? 1));
            Process.on("error", (Error) => {
                console.error(`Failed to start ninja: ${Error.message}`);
                Resolve(1);
            });
        });
    }

    private BuildNinjaFile(Actions: BuildAction[], BuildTarget: ResolvedTarget): string {
        const Target = BuildTarget.Target;
        const SpecialPathVariables = new Map<string, string>();
        const UsedVariableNames = new Map<string, string>();
        for (const FilePath of Actions.flatMap((Action) => [
            ...Action.Inputs, ...Action.Outputs, ...(Action.ImplicitInputs ?? []),
        ])) {
            if (FilePath.includes("|") && !SpecialPathVariables.has(FilePath)) {
                const StablePath = Path.normalize(FilePath).replace(/\\/g, "/");
                let Hash = 2166136261;
                for (const Character of StablePath) {
                    Hash ^= Character.charCodeAt(0);
                    Hash = Math.imul(Hash, 16777619);
                }
                const BaseName = `le_path_${(Hash >>> 0).toString(16).padStart(8, "0")}`;
                let Variable = BaseName;
                let Suffix = 1;
                while (UsedVariableNames.has(Variable) && UsedVariableNames.get(Variable) !== FilePath) {
                    Variable = `${BaseName}_${Suffix++}`;
                }
                UsedVariableNames.set(Variable, FilePath);
                SpecialPathVariables.set(FilePath, Variable);
            }
        }
        const RenderPath = (FilePath: string): string => {
            const Variable = SpecialPathVariables.get(FilePath);
            return Variable ? `$${Variable}` : ToNinjaPath(FilePath);
        };
        const Lines: string[] = [
            "# Auto-generated by LimitlessBuilder. Do not edit.",
            `# Target: ${BuildTarget.Descriptor.Name} (${Target.Platform} / ${Target.Optimization} / ${Target.TargetType})`,
            `# Entry: ${BuildTarget.Descriptor.EntryModule}; Output: ${BuildTarget.OutputName}`,
            "ninja_required_version = 1.10",
            "",
            ...[...SpecialPathVariables].flatMap(([FilePath, Variable]) => [
                `${Variable} = ${ToNinjaPathVariableValue(FilePath)}`,
            ]),
            ...(SpecialPathVariables.size > 0 ? [""] : []),
            "rule cc",
            "    command = $Cmd",
            "    description = $Desc",
            "    deps = msvc",
            "    msvc_deps_prefix = Note: including file:",
            "",
            "rule link",
            "    command = $Cmd",
            "    description = $Desc",
            "",
            "rule archive",
            "    command = $Cmd",
            "    description = $Desc",
            "",
            "rule custom",
            "    command = $Cmd",
            "    description = $Desc",
            "    restat = 1",
            "",
        ];

        const ById = new Map(Actions.map((Action) => [Action.Id, Action]));

        for (const Action of Actions) {
            const RuleName = Action.Type === "compile" ? "cc"
                : Action.Type === "link" ? "link"
                : Action.Type === "archive" ? "archive" : "custom";
            const Outputs = Action.Outputs.map(RenderPath).join(" ");
            const ExplicitInputs = new Set(Action.Inputs.map((Input) => Path.normalize(Input)));
            const ImplicitInputs = [...new Set(Action.ImplicitInputs ?? [])]
                .filter((Input) => !ExplicitInputs.has(Path.normalize(Input)));
            const AlreadyOrdered = new Set([
                ...Action.Inputs,
                ...ImplicitInputs,
            ].map((Input) => Path.normalize(Input)));
            const OrderOnly = [...new Set(Action.DependsOn.flatMap((DependencyId) => {
                const Dependency = ById.get(DependencyId);
                if (!Dependency) throw new Error(`Build action "${Action.Id}" has unknown dependency "${DependencyId}".`);
                return Dependency.Outputs;
            }))].filter((Output) => !AlreadyOrdered.has(Path.normalize(Output)));
            const ExplicitText = Action.Inputs.map(RenderPath).join(" ");
            const ImplicitText = ImplicitInputs.length > 0
                ? ` | ${ImplicitInputs.map(RenderPath).join(" ")}` : "";
            const OrderOnlyText = OrderOnly.length > 0
                ? ` || ${OrderOnly.map(RenderPath).join(" ")}` : "";
            const Command = EscapeNinjaValue(
                BuildShellCommand(Action.WorkingDirectory, Action.Command),
            );
            Lines.push(`build ${Outputs}: ${RuleName}${ExplicitText ? ` ${ExplicitText}` : ""}${ImplicitText}${OrderOnlyText}`);
            Lines.push(`    Cmd = ${Command}`);
            Lines.push(`    Desc = ${EscapeNinjaValue(Action.Description)}`);
            Lines.push("");
        }
        const Executable = Actions.find((Action) => Action.Id === `${BuildTarget.Descriptor.Name}::exe`);
        if (Executable) {
            Lines.push(`default ${RenderPath(Executable.Outputs[0])}`);
        } else {
            const Consumed = new Set(Actions.flatMap((Action) => [
                ...Action.Inputs,
                ...(Action.ImplicitInputs ?? []),
                ...Action.DependsOn.flatMap((Id) => ById.get(Id)?.Outputs ?? []),
            ]).map((Input) => Path.normalize(Input)));
            const Sinks = Actions.flatMap((Action) => Action.Outputs)
                .filter((Output) => !Consumed.has(Path.normalize(Output)));
            if (Sinks.length > 0) {
                Lines.push(`build __limitless_default: phony ${Sinks.map(RenderPath).join(" ")}`);
                Lines.push("default __limitless_default");
            }
        }
        return Lines.join("\n") + "\n";
    }

    private BuildCompileCommands(Actions: BuildAction[]): object[] {
        return Actions
            .filter((Action) => Action.Type === "compile")
            .map((Action) => ({
                directory: Action.WorkingDirectory,
                command: Action.Command.map(QuoteToken).join(" "),
                file: Action.Inputs[0],
            }));
    }
}
