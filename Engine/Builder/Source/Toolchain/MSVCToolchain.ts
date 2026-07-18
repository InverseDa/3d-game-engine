import * as ChildProcess from "node:child_process";
import * as Fs from "node:fs";
import * as Path from "node:path";
import type { Target } from "../Configuration/Types.ts";
import { Optimization } from "../Configuration/Types.ts";
import { FindExecutable } from "./ExecutableLocator.ts";
import type { IToolchain } from "./IToolchain.ts";

export class MSVCToolchain implements IToolchain {
    public readonly Name = "MSVC";
    public readonly Platform = "Win64";

    private readonly Architecture = "x64";
    private readonly VisualStudioDirectory: string;
    private readonly MsvcVersion: string;

    public constructor() {
        this.VisualStudioDirectory = this.FindVisualStudioInstallation();
        this.MsvcVersion = this.FindMsvcVersion();
    }

    public FindCompiler(): string {
        return this.FindTool("cl.exe");
    }

    public FindLinker(): string {
        return this.FindTool("link.exe");
    }

    public FindArchiver(): string {
        return this.FindTool("lib.exe");
    }

    public GetSystemIncludePaths(): string[] {
        return [Path.join(this.MsvcRoot, "include")];
    }

    public GetSystemLibPaths(): string[] {
        return [Path.join(this.MsvcRoot, "lib", this.Architecture)];
    }

    public MakeCompileCommand(
        SourceFile: string,
        OutputFile: string,
        IncludePaths: string[],
        Defines: Record<string, string>,
        Target: Target,
    ): string[] {
        const Arguments = ["/nologo", "/c", "/std:c++17", "/utf-8", "/EHsc", "/GR", "/showIncludes"];
        if (Target.Optimization === Optimization.Debug) {
            Arguments.push("/Od", "/Zi", "/MDd", "/DDEBUG=1");
        } else {
            Arguments.push("/O2", "/MD", "/DNDEBUG=1");
        }

        for (const [Name, Value] of Object.entries(Defines)) {
            Arguments.push(`/D${Name}=${Value}`);
        }
        for (const IncludePath of IncludePaths) {
            Arguments.push(`/I${IncludePath}`);
        }

        const ProgramDatabaseFile = OutputFile.replace(/\.obj$/, ".pdb");
        Arguments.push(`/Fo${OutputFile}`, `/Fd${ProgramDatabaseFile}`, SourceFile);
        return Arguments;
    }

    public MakeLinkCommand(
        OutputFile: string,
        InputObjects: string[],
        LibraryFiles: string[],
        LibraryPaths: string[],
        Target: Target,
        IsDll: boolean,
    ): string[] {
        const Arguments = ["/nologo", IsDll ? "/DLL" : "/SUBSYSTEM:WINDOWS"];
        if (Target.Optimization === Optimization.Debug) {
            Arguments.push("/DEBUG");
        }
        for (const LibraryPath of LibraryPaths) {
            Arguments.push(`/LIBPATH:${LibraryPath}`);
        }
        Arguments.push(`/OUT:${OutputFile}`, ...InputObjects, ...LibraryFiles);
        return Arguments;
    }

    public MakeArchiveCommand(OutputFile: string, InputObjects: string[]): string[] {
        return ["/nologo", `/OUT:${OutputFile}`, ...InputObjects];
    }

    public async GetEnvironment(): Promise<Record<string, string>> {
        const Environment = Object.fromEntries(
            Object.entries(process.env).filter(([, Value]): Value is string => Value !== undefined),
        );
        const WindowsSdkDirectory = this.FindWindowsSdkDirectory();
        const SdkVersion = this.FindLatestSdkVersion(WindowsSdkDirectory);
        const SdkIncludeDirectory = Path.join(WindowsSdkDirectory, "Include", SdkVersion);
        const SdkLibraryDirectory = Path.join(WindowsSdkDirectory, "Lib", SdkVersion);

        const IncludeDirectories = [
            ...this.GetSystemIncludePaths(),
            Path.join(SdkIncludeDirectory, "um"),
            Path.join(SdkIncludeDirectory, "shared"),
            Path.join(SdkIncludeDirectory, "ucrt"),
        ];
        const LibraryDirectories = [
            ...this.GetSystemLibPaths(),
            Path.join(SdkLibraryDirectory, "um", this.Architecture),
            Path.join(SdkLibraryDirectory, "ucrt", this.Architecture),
        ];
        const PathDirectories = [
            this.BinDirectory,
            Path.join(WindowsSdkDirectory, "bin", SdkVersion, this.Architecture),
        ];

        Environment.INCLUDE = IncludeDirectories.join(";");
        Environment.LIB = LibraryDirectories.join(";");
        Environment.PATH = [...PathDirectories, this.GetPathEnvironment(Environment)].join(";");
        Environment.VSLANG = "1033";
        return Environment;
    }

    private get MsvcRoot(): string {
        return Path.join(this.VisualStudioDirectory, "VC", "Tools", "MSVC", this.MsvcVersion);
    }

    private get BinDirectory(): string {
        return Path.join(this.MsvcRoot, "bin", "Hostx64", this.Architecture);
    }

    private FindVisualStudioInstallation(): string {
        const VsWherePath = this.FindVsWhere();
        const Result = ChildProcess.spawnSync(VsWherePath, [
            "-latest",
            "-products", "*",
            "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property", "installationPath",
        ], { encoding: "utf-8" });
        const InstallationPath = Result.stdout?.trim();
        if (Result.status !== 0 || !InstallationPath || !Fs.existsSync(InstallationPath)) {
            throw new Error("No Visual Studio installation with the MSVC C++ workload was found.");
        }
        return InstallationPath;
    }

    private FindVsWhere(): string {
        const FromPath = FindExecutable("vswhere.exe", process.env.LIMITLESS_BUILDER_VSWHERE);
        if (FromPath) {
            return FromPath;
        }

        const ProgramFilesX86 = process.env["ProgramFiles(x86)"] ?? process.env.ProgramFiles;
        const InstallerPath = ProgramFilesX86
            ? Path.join(ProgramFilesX86, "Microsoft Visual Studio", "Installer", "vswhere.exe")
            : "";
        if (InstallerPath && Fs.existsSync(InstallerPath)) {
            return InstallerPath;
        }
        throw new Error("vswhere.exe was not found. Install Visual Studio Build Tools or set LIMITLESS_BUILDER_VSWHERE.");
    }

    private FindMsvcVersion(): string {
        const MsvcDirectory = Path.join(this.VisualStudioDirectory, "VC", "Tools", "MSVC");
        const Versions = this.GetDirectories(MsvcDirectory);
        if (Versions.length === 0) {
            throw new Error(`No MSVC toolset was found in ${MsvcDirectory}.`);
        }
        return Versions.sort((Left, Right) => Left.localeCompare(Right, undefined, { numeric: true })).at(-1)!;
    }

    private FindTool(Name: string): string {
        const ToolPath = Path.join(this.BinDirectory, Name);
        if (!Fs.existsSync(ToolPath)) {
            throw new Error(`MSVC tool not found: ${ToolPath}`);
        }
        return ToolPath;
    }

    private FindWindowsSdkDirectory(): string {
        const FromEnvironment = process.env.WindowsSdkDir;
        if (FromEnvironment && Fs.existsSync(FromEnvironment)) {
            return FromEnvironment;
        }

        const RegistryResult = ChildProcess.spawnSync(this.GetRegistryExecutable(), [
            "query",
            "HKLM\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots",
            "/v",
            "KitsRoot10",
        ], { encoding: "utf-8" });
        const RegistryLine = RegistryResult.stdout?.split(/\r?\n/)
            .find((Line) => Line.includes("KitsRoot10"));
        const RegistryPath = RegistryLine?.replace(/^.*KitsRoot10\s+REG_\w+\s+/, "").trim();
        if (RegistryPath && Fs.existsSync(RegistryPath)) {
            return RegistryPath;
        }
        throw new Error("Windows 10 or 11 SDK was not found. Install it with the Visual Studio C++ workload.");
    }

    private FindLatestSdkVersion(WindowsSdkDirectory: string): string {
        const IncludeDirectory = Path.join(WindowsSdkDirectory, "Include");
        const Versions = this.GetDirectories(IncludeDirectory)
            .filter((Version) => Version.startsWith("10."))
            .filter((Version) => Fs.existsSync(Path.join(IncludeDirectory, Version, "um")));
        if (Versions.length === 0) {
            throw new Error(`No Windows SDK version was found in ${IncludeDirectory}.`);
        }
        return Versions.sort((Left, Right) => Left.localeCompare(Right, undefined, { numeric: true })).at(-1)!;
    }

    private GetDirectories(Directory: string): string[] {
        if (!Fs.existsSync(Directory)) {
            return [];
        }
        return Fs.readdirSync(Directory, { withFileTypes: true })
            .filter((Entry) => Entry.isDirectory())
            .map((Entry) => Entry.name);
    }

    private GetPathEnvironment(Environment: Record<string, string>): string {
        const PathKey = Object.keys(Environment).find((Key) => Key.toUpperCase() === "PATH");
        return PathKey ? Environment[PathKey] : "";
    }

    private GetRegistryExecutable(): string {
        const WindowsDirectory = process.env.SystemRoot ?? process.env.WINDIR;
        const RegistryExecutable = WindowsDirectory
            ? Path.join(WindowsDirectory, "System32", "reg.exe")
            : "reg.exe";
        return Fs.existsSync(RegistryExecutable) ? RegistryExecutable : "reg.exe";
    }
}
