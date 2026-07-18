import * as Fs from "node:fs/promises";
import * as Path from "node:path";
import { pathToFileURL } from "node:url";
import { ModuleBuild } from "../Configuration/ModuleBuild.ts";
import type {
    ModuleInstance,
    TargetDescriptor,
} from "../Configuration/Types.ts";

type ModuleBuildConstructor = new () => ModuleBuild;

export class ModuleLoader {
    private readonly SourceDirectory: string;

    public constructor(SourceDirectory: string) {
        this.SourceDirectory = SourceDirectory;
    }

    public async DiscoverModules(): Promise<ModuleInstance[]> {
        const Files = await this.WalkDirectory(this.SourceDirectory);
        const BuildFiles = Files.filter((FilePath) => Path.basename(FilePath) === "Build.ts");
        const Modules: ModuleInstance[] = [];

        for (const BuildFilePath of BuildFiles) {
            const Module = await this.LoadModule(BuildFilePath);
            if (Module) {
                Modules.push(Module);
            }
        }
        return Modules;
    }

    private async LoadModule(BuildFilePath: string): Promise<ModuleInstance | null> {
        try {
            const Imported = await import(pathToFileURL(BuildFilePath).href);
            const BuildConstructor = Imported.default as ModuleBuildConstructor;
            if (typeof BuildConstructor !== "function") {
                throw new Error("The default export must be a ModuleBuild class.");
            }

            const Descriptor = new BuildConstructor();
            if (!Descriptor.Name || typeof Descriptor.Configure !== "function") {
                throw new Error("The ModuleBuild class must provide Name and Configure.");
            }

            return {
                Descriptor,
                SourceRoot: Path.dirname(BuildFilePath),
                BuildFilePath,
            };
        } catch (Error) {
            console.error(`Failed to load module: ${BuildFilePath}`);
            console.error(Error);
            return null;
        }
    }

    private async WalkDirectory(Directory: string): Promise<string[]> {
        const Results: string[] = [];
        let Entries: import("node:fs").Dirent[];
        try {
            Entries = await Fs.readdir(Directory, { withFileTypes: true });
        } catch {
            return Results;
        }

        for (const Entry of Entries) {
            const FullPath = Path.join(Directory, Entry.name);
            if (Entry.isDirectory()) {
                Results.push(...await this.WalkDirectory(FullPath));
            } else if (Entry.isFile()) {
                Results.push(FullPath);
            }
        }
        return Results;
    }
}

export class TargetLoader {
    private readonly TargetsDirectory: string;

    public constructor(TargetsDirectory: string) {
        this.TargetsDirectory = TargetsDirectory;
    }

    public async DiscoverTargets(): Promise<TargetDescriptor[]> {
        const Targets: TargetDescriptor[] = [];
        let Entries: import("node:fs").Dirent[];
        try {
            Entries = await Fs.readdir(this.TargetsDirectory, { withFileTypes: true });
        } catch {
            return Targets;
        }

        for (const Entry of Entries) {
            if (!Entry.isFile() || !Entry.name.endsWith(".target.ts")) {
                continue;
            }

            const FilePath = Path.join(this.TargetsDirectory, Entry.name);
            const Imported = await import(pathToFileURL(FilePath).href);
            const Descriptor = Imported.default as TargetDescriptor;
            if (Descriptor?.Name) {
                Targets.push(Descriptor);
            }
        }
        return Targets;
    }
}
