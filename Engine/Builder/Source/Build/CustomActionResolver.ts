import * as Path from "node:path";
import type { BuildAction, CustomActionDescriptor, ResolvedTarget } from "../Configuration/Types.ts";
import type { ResolvedModule } from "../Graph/DependencyGraph.ts";
import type { EnginePaths } from "../Project/EnginePaths.ts";

export function ResolveCustomActions(
    Paths: EnginePaths,
    BuildTarget: ResolvedTarget,
    Module: ResolvedModule,
): BuildAction[] {
    const Name = Module.Instance.Descriptor.Name;
    const SourceRoot = Module.Instance.SourceRoot;
    const Result: BuildAction[] = [];
    for (const Descriptor of Module.Configuration.CustomActions) {
        ValidateDescriptor(Name, Descriptor);
        const Id = `${Name}::custom::${Descriptor.Id}`;
        const Outputs = Descriptor.Outputs.map((Output) => {
            const Resolved = ResolvePath(Paths, BuildTarget, Output, SourceRoot, Name);
            if (!IsAllowedOutput(Paths, BuildTarget, Resolved)) {
                throw new Error(`Custom action "${Id}" output escapes engine generated/temp/binaries roots: ${Resolved}`);
            }
            return Resolved;
        });
        Result.push({
            Id,
            Type: "custom",
            Inputs: Descriptor.Inputs.map((Input) => ResolvePath(Paths, BuildTarget, Input, SourceRoot, Name)),
            Outputs,
            Command: Descriptor.Command.map((Token) => ExpandPathVariables(Paths, BuildTarget, Token, SourceRoot, Name)),
            WorkingDirectory: ResolvePath(
                Paths, BuildTarget, Descriptor.WorkingDirectory ?? "[engine.Root]", SourceRoot, Name,
            ),
            DependsOn: (Descriptor.DependsOn ?? []).map((Dependency) =>
                Dependency.includes("::") ? Dependency : `${Name}::custom::${Dependency}`),
            Description: Descriptor.Description ?? `CUSTOM ${Name}/${Descriptor.Id}`,
            ImplicitInputs: (Descriptor.ImplicitInputs ?? []).map((Input) =>
                ResolvePath(Paths, BuildTarget, Input, SourceRoot, Name)),
        });
    }
    return Result;
}

function ValidateDescriptor(ModuleName: string, Descriptor: CustomActionDescriptor): void {
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

function ResolvePath(
    Paths: EnginePaths,
    BuildTarget: ResolvedTarget,
    Value: string,
    SourceRoot: string,
    ModuleName: string,
): string {
    const Expanded = ExpandPathVariables(Paths, BuildTarget, Value, SourceRoot, ModuleName);
    return Path.normalize(Path.isAbsolute(Expanded) ? Expanded : Path.resolve(SourceRoot, Expanded));
}

function ExpandPathVariables(
    Paths: EnginePaths,
    BuildTarget: ResolvedTarget,
    Value: string,
    SourceRoot: string,
    ModuleName: string,
): string {
    const Temp = Paths.TemporaryOutputDirectory(BuildTarget);
    const Generated = Paths.GeneratedOutputDirectory(BuildTarget);
    const Variables: Record<string, string> = {
        "[module.SourceRoot]": SourceRoot,
        "[project.SourceRootPath]": SourceRoot,
        "[engine.Root]": Paths.Root,
        "[engine.Source]": Paths.SourceDirectory,
        "[engine.Temp]": Temp,
        "[engine.Binaries]": Paths.BinaryOutputDirectory(BuildTarget),
        "[engine.Generated]": Generated,
        "[module.Generated]": Path.join(Generated, ModuleName),
    };
    let Expanded = Value;
    for (const [Variable, Replacement] of Object.entries(Variables)) {
        Expanded = Expanded.replaceAll(Variable, Replacement);
    }
    const Unknown = Expanded.match(/\[(?:module|project|engine)\.[^\]]+\]/);
    if (Unknown) throw new Error(`Unknown Builder path variable "${Unknown[0]}".`);
    return Expanded;
}

function IsAllowedOutput(Paths: EnginePaths, BuildTarget: ResolvedTarget, Output: string): boolean {
    return [
        Paths.TemporaryOutputDirectory(BuildTarget),
        Paths.GeneratedOutputDirectory(BuildTarget),
        Paths.BinaryOutputDirectory(BuildTarget),
    ].some((Root) => IsWithin(Root, Output));
}

function IsWithin(Root: string, Candidate: string): boolean {
    const Relative = Path.relative(Path.resolve(Root), Path.resolve(Candidate));
    return Relative === "" || (!Relative.startsWith(`..${Path.sep}`) && Relative !== ".." && !Path.isAbsolute(Relative));
}
