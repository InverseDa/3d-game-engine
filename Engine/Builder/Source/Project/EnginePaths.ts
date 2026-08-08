import * as Fs from "node:fs";
import * as Path from "node:path";

export class EnginePaths {
    private readonly RootDirectory: string;

    public constructor(RootDirectory: string) {
        this.RootDirectory = RootDirectory;
    }

    public get Root(): string { return this.RootDirectory; }
    public get EngineDirectory(): string { return Path.join(this.RootDirectory, "Engine"); }
    public get SourceDirectory(): string { return Path.join(this.EngineDirectory, "Source"); }
    public get BinariesDirectory(): string { return Path.join(this.EngineDirectory, "Binaries"); }
    public get BuilderDirectory(): string { return Path.join(this.EngineDirectory, "Builder"); }
    public get TargetsDirectory(): string { return Path.join(this.BuilderDirectory, "Targets"); }
    // Ninja's generated build graph and object files are disposable build intermediates.
    // Keep them with the other engine-generated artefacts, rather than at the project root.
    public get TemporaryDirectory(): string { return Path.join(this.IntermediateDirectory, "Build"); }
    public get SolutionDirectory(): string { return Path.join(this.RootDirectory, "Solution"); }
    public get IntermediateDirectory(): string { return Path.join(this.EngineDirectory, "Intermediate"); }
    public get ProjectFilesDirectory(): string { return Path.join(this.IntermediateDirectory, "ProjectFiles"); }

    public BinaryOutputDirectory(Platform: string): string {
        return Path.join(this.BinariesDirectory, Platform);
    }

    public TemporaryOutputDirectory(Platform: string, Configuration: string): string {
        return Path.join(this.TemporaryDirectory, Platform, Configuration);
    }

    public GeneratedOutputDirectory(Platform: string, Configuration: string): string {
        return Path.join(this.TemporaryOutputDirectory(Platform, Configuration), "Generated");
    }
}

export function FindProjectRoot(StartDirectory: string): string {
    let CurrentDirectory = Path.resolve(StartDirectory);
    for (let Index = 0; Index < 20; Index++) {
        if (Fs.existsSync(Path.join(CurrentDirectory, "Engine", "Source"))) {
            return CurrentDirectory;
        }
        const ParentDirectory = Path.dirname(CurrentDirectory);
        if (ParentDirectory === CurrentDirectory) {
            break;
        }
        CurrentDirectory = ParentDirectory;
    }
    throw new Error(`Cannot find project root from: ${StartDirectory}`);
}

export function ToPosixPath(FilePath: string): string {
    return FilePath.replace(/\\/g, "/");
}
