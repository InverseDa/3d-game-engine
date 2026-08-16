#!/usr/bin/env node
import * as Fs from "node:fs/promises";
import * as Path from "node:path";
import {
    EmitReflectionModule, ParseReflectionModule, ReflectionGeneratorError, WriteFileIfChanged,
    type ReflectionGeneratorOptions, type ReflectionSource,
} from "./ReflectionGenerator.ts";

interface Arguments extends ReflectionGeneratorOptions {
    ModuleRoot: string;
    Output: string;
    Inputs: string[];
}

function ParseArguments(Values: string[]): Arguments {
    const Result: Partial<Arguments> & { Inputs: string[] } = { Inputs: [] };
    for (let Index = 0; Index < Values.length; Index++) {
        const Name = Values[Index];
        const Value = Values[++Index];
        if (!Value) throw new Error(`Missing value for ${Name}.`);
        if (Name === "--module") Result.ModuleName = Value;
        else if (Name === "--module-root") Result.ModuleRoot = Path.resolve(Value);
        else if (Name === "--api-macro") Result.ApiMacro = Value;
        else if (Name === "--entry-header") Result.EntryHeader = Value.replaceAll("\\", "/");
        else if (Name === "--output") Result.Output = Path.resolve(Value);
        else if (Name === "--input") Result.Inputs.push(Path.resolve(Value));
        else throw new Error(`Unknown argument ${Name}.`);
    }
    if (!Result.ModuleName || !Result.ModuleRoot || !Result.EntryHeader || !Result.Output || Result.Inputs.length === 0) {
        throw new Error("Required arguments: --module --module-root --entry-header --output and one or more --input.");
    }
    return Result as Arguments;
}

async function Main(): Promise<void> {
    const Args = ParseArguments(process.argv.slice(2));
    const Sources: ReflectionSource[] = [];
    for (const Input of [...new Set(Args.Inputs)].sort()) {
        const Relative = Path.relative(Args.ModuleRoot, Input).replaceAll("\\", "/");
        if (Relative.startsWith("../") || Path.isAbsolute(Relative)) throw new Error(`Input escapes module root: ${Input}`);
        let Include = Relative;
        if (Include.startsWith("Public/")) Include = Include.slice("Public/".length);
        else if (Include.startsWith("Private/")) Include = Include.slice("Private/".length);
        else throw new Error(`Reflection input must be under Public/ or Private/: ${Input}`);
        Sources.push({ Path: Relative, Include, Text: await Fs.readFile(Input, "utf-8") });
    }
    const Module = ParseReflectionModule(Sources, Args);
    const Changed = await WriteFileIfChanged(Args.Output, EmitReflectionModule(Module));
    console.log(`${Changed ? "Generated" : "Unchanged"} reflection: ${Args.Output}`);
}

try { await Main(); }
catch (Error) {
    console.error(Error instanceof ReflectionGeneratorError || Error instanceof Error ? Error.message : String(Error));
    process.exitCode = 1;
}
