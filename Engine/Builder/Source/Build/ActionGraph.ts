import * as Path from "node:path";
import type { BuildAction } from "../Configuration/Types.ts";

function PathKey(FilePath: string): string {
    const Normalized = Path.normalize(FilePath);
    return process.platform === "win32" ? Normalized.toLowerCase() : Normalized;
}

/** Validates the complete action graph and returns a stable topological order. */
export function ValidateAndSortActions(Actions: BuildAction[]): BuildAction[] {
    const ById = new Map<string, BuildAction>();
    const ProducerByOutput = new Map<string, BuildAction>();

    for (const Action of Actions) {
        const AssertSerializable = (Value: string, Field: string): void => {
            if (/[\0\r\n]/.test(Value)) {
                throw new Error(`Build action "${Action.Id}" ${Field} contains a forbidden control character.`);
            }
        };
        if (!Action.Id.trim()) throw new Error("Build action id must not be empty.");
        AssertSerializable(Action.Id, "id");
        AssertSerializable(Action.Description, "description");
        if (ById.has(Action.Id)) throw new Error(`Duplicate build action id "${Action.Id}".`);
        if (Action.Command.length === 0 || Action.Command.some((Token) => Token.length === 0)) {
            throw new Error(`Build action "${Action.Id}" has an empty command or command token.`);
        }
        if (Action.Outputs.length === 0) throw new Error(`Build action "${Action.Id}" has no outputs.`);
        if (!Path.isAbsolute(Action.WorkingDirectory)) {
            throw new Error(`Build action "${Action.Id}" working directory is not absolute.`);
        }
        AssertSerializable(Action.WorkingDirectory, "working directory");
        for (const Token of Action.Command) AssertSerializable(Token, "command token");
        ById.set(Action.Id, Action);
        for (const Output of Action.Outputs) {
            AssertSerializable(Output, "output");
            if (!Path.isAbsolute(Output)) throw new Error(`Build action "${Action.Id}" output is not absolute: ${Output}`);
            const Key = PathKey(Output);
            const Existing = ProducerByOutput.get(Key);
            if (Existing) {
                throw new Error(`Build actions "${Existing.Id}" and "${Action.Id}" both produce "${Output}".`);
            }
            ProducerByOutput.set(Key, Action);
        }
        for (const Input of [...Action.Inputs, ...(Action.ImplicitInputs ?? [])]) {
            AssertSerializable(Input, "input");
            if (!Path.isAbsolute(Input)) throw new Error(`Build action "${Action.Id}" input is not absolute: ${Input}`);
        }
    }

    const Dependencies = new Map<string, Set<string>>();
    for (const Action of Actions) {
        const Edges = new Set<string>();
        for (const DependencyId of Action.DependsOn) {
            if (DependencyId === Action.Id) throw new Error(`Build action "${Action.Id}" depends on itself.`);
            if (!ById.has(DependencyId)) {
                throw new Error(`Build action "${Action.Id}" depends on unknown action "${DependencyId}".`);
            }
            Edges.add(DependencyId);
        }
        for (const Input of [...Action.Inputs, ...(Action.ImplicitInputs ?? [])]) {
            const Producer = ProducerByOutput.get(PathKey(Input));
            if (!Producer) continue;
            if (Producer.Id === Action.Id) {
                throw new Error(`Build action "${Action.Id}" consumes its own output "${Input}".`);
            }
            Edges.add(Producer.Id);
        }
        Dependencies.set(Action.Id, Edges);
    }

    const Sorted: BuildAction[] = [];
    const Visiting = new Set<string>();
    const Visited = new Set<string>();
    const Visit = (Id: string): void => {
        if (Visited.has(Id)) return;
        if (Visiting.has(Id)) throw new Error(`Build action dependency cycle detected at "${Id}".`);
        Visiting.add(Id);
        for (const Dependency of Dependencies.get(Id) ?? []) Visit(Dependency);
        Visiting.delete(Id);
        Visited.add(Id);
        Sorted.push(ById.get(Id)!);
    };
    for (const Action of Actions) Visit(Action.Id);
    return Sorted;
}
