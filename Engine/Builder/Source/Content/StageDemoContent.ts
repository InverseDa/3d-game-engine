import { spawnSync } from "node:child_process";
import { cp, mkdir, mkdtemp, readFile, readdir, rm } from "node:fs/promises";
import * as Path from "node:path";

const [Root, Destination, Configuration, ToolRoot] = process.argv.slice(2);
if (!Root || !Destination || !ToolRoot || !["Debug", "Release"].includes(Configuration)) {
    throw new Error("Usage: StageDemoContent ROOT DESTINATION Debug|Release TOOL_ROOT");
}

function Run(Command: string, Arguments: string[]): void {
    const Result = spawnSync(Command, Arguments, { cwd: Root, stdio: "inherit" });
    if (Result.error) throw Result.error;
    if (Result.status !== 0) throw new Error(`${Path.basename(Command)} failed (${Result.status ?? Result.signal})`);
}

const Builder = Path.join(Root, "Engine/Builder/Source/Cli/CommandLine.ts");
function BuildTool(Name: string): string {
    Run(process.execPath, [
        "--no-warnings", "--experimental-strip-types", Builder,
        "build", "--platform", "Win64", "--config", Configuration, "--type", "Program", "--target", Name,
        "--program-output-root", ToolRoot,
    ]);
    return Path.join(ToolRoot, "Binaries/Win64", Configuration, Name, "Program", `${Name}.exe`);
}

const Sources = Path.join(Root, "Engine/Content/Examples/AssetImport");
const Manifest = JSON.parse(await readFile(Path.join(Sources, "Imports.json"), "utf-8"));
const Import = BuildTool("AssetImport");
const SceneTool = BuildTool("SceneTool");
await mkdir(ToolRoot, { recursive: true });
const Candidate = await mkdtemp(Path.join(ToolRoot, "stage-"));
try {
    const Cooked = Path.join(Candidate, "Assets");
    const ExpectedFiles = new Set<string>();
    for (const Asset of Manifest.assets) {
        if (!["mesh", "material", "texture"].includes(Asset.kind) ||
            typeof Asset.path !== "string" || !Asset.path.startsWith(`${Manifest.mount}/`) ||
            typeof Asset.source !== "string" || Path.basename(Asset.source) !== Asset.source) {
            throw new Error("Invalid demo asset import entry");
        }
        const Relative = Asset.path.slice(Manifest.mount.length + 1);
        if (!Relative.split("/").every((Segment: string) => /^[A-Za-z0-9_-]+$/.test(Segment))) {
            throw new Error(`Invalid demo asset path ${Asset.path}`);
        }
        const Output = Path.join(Cooked, `${Relative}.leasset`);
        const FinalPath = Path.join(Destination, Path.relative(Candidate, Output));
        if (ExpectedFiles.has(FinalPath)) throw new Error(`Duplicate demo output ${Asset.path}`);
        ExpectedFiles.add(FinalPath);
        await mkdir(Path.dirname(Output), { recursive: true });
        Run(Import, [
            Asset.kind, "--source", Path.join(Sources, Asset.source), "--id", Asset.id,
            "--path", Asset.path, "--output", Output,
        ]);
    }

    const Scene = Path.join(Sources, "Scene.json");
    Run(SceneTool, ["check", Scene, Manifest.mount, Cooked]);
    await cp(Scene, Path.join(Candidate, "Scene.json"));
    await cp(Candidate, Destination, { recursive: true });

    const OutputAssets = Path.join(Destination, "Assets");
    for (const Entry of await readdir(OutputAssets, { recursive: true, withFileTypes: true })) {
        if (!Entry.isFile() || !Entry.name.endsWith(".leasset")) continue;
        const File = Path.join(Entry.parentPath, Entry.name);
        if (!ExpectedFiles.has(File)) await rm(File);
    }
} finally {
    if (Path.dirname(Path.resolve(Candidate)) !== Path.resolve(ToolRoot)) {
        throw new Error("Invalid staging directory");
    }
    await rm(Candidate, { recursive: true, force: true });
}
