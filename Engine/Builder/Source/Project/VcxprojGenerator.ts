import * as Path from "node:path";
import * as Fs from "node:fs/promises";
import * as FsSync from "node:fs";
import { ResolveTarget } from "../Configuration/Target.ts";
import type { ResolvedTarget, Target } from "../Configuration/Types.ts";
import type { ResolvedModule, DependencyGraph } from "../Graph/DependencyGraph.ts";
import type { IToolchain } from "../Toolchain/IToolchain.ts";
import type { EnginePaths } from "./EnginePaths.ts";

const PROJECT_GUID = "{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}";
const SLN_GUID = "{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}";
const SOLUTION_FOLDER_GUID = "{2150E333-8FDC-42A3-9474-1A3956D46DE8}";
const ENGINE_FOLDER_GUID = "{D6E0B88C-0B21-4E23-A9F4-9E64B65DE09A}";

interface ProjectFiles {
    Sources: Set<string>;
    Headers: Set<string>;
    Other: Set<string>;
}

interface IntelliSenseConfiguration {
    IncludePaths: string[];
}

interface FileIntelliSenseConfiguration {
    IncludePaths: string[];
    Defines: string[];
}

function XmlEscape(S: string): string {
    return S.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

export class VcxprojGenerator {
    private readonly Paths: EnginePaths;
    private readonly Graph: DependencyGraph;
    private readonly Toolchain: IToolchain;

    constructor(Paths: EnginePaths, Graph: DependencyGraph, Toolchain: IToolchain) {
        this.Paths = Paths;
        this.Graph = Graph;
        this.Toolchain = Toolchain;
    }

    async Generate(Modules: ResolvedModule[], BuildTarget: ResolvedTarget): Promise<string> {
        const ProjectFilesDir = this.Paths.ProjectFilesDirectory;
        await Fs.mkdir(ProjectFilesDir, { recursive: true });
        await this.RemoveObsoleteProjectFileLinks();

        const Files = this.CollectProjectFiles(Modules);
        const IntelliSense = await this.GetIntelliSenseConfiguration();
        const VcxprojPath = Path.join(ProjectFilesDir, "LimitlessEngine.vcxproj");
        const CommonPropsPath = Path.join(ProjectFilesDir, "LimitlessEngineCommon.props");
        await Fs.writeFile(CommonPropsPath, this.BuildCommonProps(), "utf-8");
        await Fs.writeFile(VcxprojPath, this.BuildVcxproj(Files, Modules, BuildTarget, IntelliSense), "utf-8");

        const SlnPath = Path.join(this.Paths.Root, "LimitlessEngine.sln");
        await Fs.writeFile(SlnPath, this.BuildSln(), "utf-8");

        const FilterPath = Path.join(ProjectFilesDir, "LimitlessEngine.vcxproj.filters");
        await Fs.writeFile(FilterPath, this.BuildFilters(Files), "utf-8");

        // These generated files briefly lived at the Engine root. Keep generated
        // IDE artefacts contained in Solution and remove the obsolete copies.
        await Fs.rm(Path.join(this.Paths.EngineDirectory, "LimitlessEngine.vcxproj"), { force: true });
        await Fs.rm(Path.join(this.Paths.EngineDirectory, "LimitlessEngine.vcxproj.filters"), { force: true });

        return SlnPath;
    }

    private BuildVcxproj(
        Files: ProjectFiles,
        Modules: ResolvedModule[],
        BuildTarget: ResolvedTarget,
        IntelliSense: IntelliSenseConfiguration,
    ): string {
        const AllIncludes = new Set<string>();
        const AllDefines = new Set<string>();

        for (const Module of Modules) {
            const Root = Module.Instance.SourceRoot;
            AllIncludes.add(Path.join(Root, "Public"));
            AllIncludes.add(Path.join(Root, "Private"));
            for (const Include of Module.Configuration.IncludePaths) {
                AllIncludes.add(this.ResolveVar(Include, Root));
            }
            for (const [Key, Value] of Object.entries(Module.Configuration.Defines)) {
                AllDefines.add(`${Key}=${Value}`);
            }
        }
        for (const IncludePath of IntelliSense.IncludePaths) {
            AllIncludes.add(IncludePath);
        }
        AllDefines.add("PLATFORM_WINDOWS=1");
        const Target = BuildTarget.Target;
        AllDefines.add(`WITH_EDITOR=${Target.TargetType === "Editor" ? "1" : "0"}`);

        const IncludeStr = [...AllIncludes].filter((PathName) => FsSync.existsSync(PathName)).join(";");
        const DefineStr = [...AllDefines].join(";");

        const Lines: string[] = [];
        Lines.push(`<?xml version="1.0" encoding="utf-8"?>`);
        Lines.push(`<Project DefaultTargets="Build" ToolsVersion="17.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">`);
        Lines.push(`  <ItemGroup Label="ProjectConfigurations">`);
        for (const Configuration of ["Debug", "Release"]) {
            Lines.push(`    <ProjectConfiguration Include="${Configuration}|x64">`);
            Lines.push(`      <Configuration>${Configuration}</Configuration>`);
            Lines.push(`      <Platform>x64</Platform>`);
            Lines.push(`    </ProjectConfiguration>`);
        }
        Lines.push(`  </ItemGroup>`);
        Lines.push(`  <PropertyGroup Label="Globals">`);
        Lines.push(`    <ProjectGuid>${PROJECT_GUID}</ProjectGuid>`);
        Lines.push(`    <RootNamespace>LimitlessEngine</RootNamespace>`);
        Lines.push(`  </PropertyGroup>`);
        Lines.push(`  <Import Project="LimitlessEngineCommon.props" />`);
        Lines.push(`  <ImportGroup Label="ExtensionSettings" />`);
        Lines.push(`  <PropertyGroup Label="UserMacros" />`);

        for (const Configuration of ["Debug", "Release"] as const) {
            const ConfigurationTarget = Target.Optimization === Configuration
                ? BuildTarget
                : ResolveTarget(BuildTarget.Descriptor, { ...Target, Optimization: Configuration });
            const OutputName = ConfigurationTarget.OutputName;
            const BinaryDirectory = this.ToProjectDirectoryPath(
                this.Paths.BinaryOutputDirectory(ConfigurationTarget),
            );
            const IntermediateDirectory = this.ToProjectDirectoryPath(
                this.Paths.TemporaryOutputDirectory(ConfigurationTarget),
            );
            const Output = `${BinaryDirectory}\\${OutputName}.exe`;
            const BuildCmd = `$(ProjectDir)..\\..\\Builder\\LimitlessBuilder.bat build --target "${BuildTarget.Descriptor.Name}" --platform Win64 --config ${Configuration} --type ${Target.TargetType}`;
            const CleanCmd = `if exist "${BinaryDirectory}" rmdir /S /Q "${BinaryDirectory}" & if exist "${IntermediateDirectory}" rmdir /S /Q "${IntermediateDirectory}"`;
            Lines.push(`  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='${Configuration}|x64'">`);
            Lines.push(`    <NMakeBuildCommandLine>${XmlEscape(BuildCmd)}</NMakeBuildCommandLine>`);
            Lines.push(`    <NMakeReBuildCommandLine>${XmlEscape(BuildCmd)}</NMakeReBuildCommandLine>`);
            Lines.push(`    <NMakeCleanCommandLine>${XmlEscape(CleanCmd)}</NMakeCleanCommandLine>`);
            Lines.push(`    <NMakeOutput>${XmlEscape(Output)}</NMakeOutput>`);
            Lines.push(`    <TargetName>${XmlEscape(OutputName)}</TargetName>`);
            Lines.push(`    <TargetExt>.exe</TargetExt>`);
            Lines.push(`    <LocalDebuggerCommand>$(NMakeOutput)</LocalDebuggerCommand>`);
            Lines.push(`    <LocalDebuggerWorkingDirectory>$(ProjectDir)..\\..</LocalDebuggerWorkingDirectory>`);
            Lines.push(`    <LocalDebuggerCommandArguments></LocalDebuggerCommandArguments>`);
            Lines.push(`    <DebuggerFlavor>WindowsLocalDebugger</DebuggerFlavor>`);
            Lines.push(`    <NMakeIncludeSearchPath>${XmlEscape(IncludeStr)}</NMakeIncludeSearchPath>`);
            Lines.push(`    <NMakePreprocessorDefinitions>${XmlEscape(DefineStr)}</NMakePreprocessorDefinitions>`);
            Lines.push(`  </PropertyGroup>`);
            Lines.push(`  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='${Configuration}|x64'">`);
            Lines.push(`    <ClCompile>`);
            Lines.push(`      <AdditionalIncludeDirectories>${XmlEscape(IncludeStr)};%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>`);
            Lines.push(`      <PreprocessorDefinitions>${XmlEscape(DefineStr)};%(PreprocessorDefinitions)</PreprocessorDefinitions>`);
            Lines.push(`      <LanguageStandard>stdcpp17</LanguageStandard>`);
            Lines.push(`    </ClCompile>`);
            Lines.push(`  </ItemDefinitionGroup>`);
        }

        this.AppendProjectItems(Lines, "ClCompile", Files.Sources);
        this.AppendProjectItems(Lines, "ClInclude", Files.Headers);
        this.AppendProjectItems(Lines, "None", Files.Other);
        Lines.push(`  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />`);
        Lines.push(`</Project>`);
        return Lines.join("\r\n");
    }

    private ToProjectDirectoryPath(AbsolutePath: string): string {
        const RelativePath = Path.relative(this.Paths.ProjectFilesDirectory, AbsolutePath);
        return `$(ProjectDir)${RelativePath.replace(/\//g, "\\")}`;
    }

    private BuildCommonProps(): string {
        return [
            `<?xml version="1.0" encoding="utf-8"?>`,
            `<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">`,
            `  <PropertyGroup Label="Globals">`,
            `    <Keyword>MakeFileProj</Keyword>`,
            `    <PlatformToolset>v143</PlatformToolset>`,
            `    <DefaultPlatformToolset>v143</DefaultPlatformToolset>`,
            `    <MinimumVisualStudioVersion>17.0</MinimumVisualStudioVersion>`,
            `    <VCProjectVersion>17.0</VCProjectVersion>`,
            `    <NMakeUseOemCodePage>true</NMakeUseOemCodePage>`,
            `    <TargetRuntime>Native</TargetRuntime>`,
            `  </PropertyGroup>`,
            `  <PropertyGroup Label="Configuration">`,
            `    <ConfigurationType>Makefile</ConfigurationType>`,
            `    <PlatformToolset>v143</PlatformToolset>`,
            `  </PropertyGroup>`,
            `  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />`,
            `  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />`,
            `  <PropertyGroup>`,
            `    <OutDir>..\\Build\\Unused\\</OutDir>`,
            `    <IntDir>..\\Build\\Unused\\</IntDir>`,
            `    <IncludePath />`,
            `    <ReferencePath />`,
            `    <LibraryPath />`,
            `    <LibraryWPath />`,
            `    <SourcePath />`,
            `    <ExcludePath />`,
            `  </PropertyGroup>`,
            `  <ImportGroup Label="PropertySheets">`,
            `    <Import Project="$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />`,
            `  </ImportGroup>`,
            `</Project>`,
            ``,
        ].join("\r\n");
    }

    private BuildSln(): string {
        const Lines: string[] = [];
        Lines.push("Microsoft Visual Studio Solution File, Format Version 12.00");
        Lines.push("# Visual Studio Version 17");
        Lines.push(`Project("${SOLUTION_FOLDER_GUID}") = "Engine", "Engine", "${ENGINE_FOLDER_GUID}"`);
        Lines.push("EndProject");
        Lines.push(`Project("${SLN_GUID}") = "LimitlessEngine", "Engine\\Intermediate\\ProjectFiles\\LimitlessEngine.vcxproj", "${PROJECT_GUID}"`);
        Lines.push("EndProject");
        Lines.push("Global");
        Lines.push("\tGlobalSection(SolutionConfigurationPlatforms) = preSolution");
        Lines.push("\t\tDebug|Win64 = Debug|Win64");
        Lines.push("\t\tRelease|Win64 = Release|Win64");
        Lines.push("\tEndGlobalSection");
        Lines.push("\tGlobalSection(ProjectConfigurationPlatforms) = postSolution");
        Lines.push(`\t\t${PROJECT_GUID}.Debug|Win64.ActiveCfg = Debug|x64`);
        Lines.push(`\t\t${PROJECT_GUID}.Debug|Win64.Build.0 = Debug|x64`);
        Lines.push(`\t\t${PROJECT_GUID}.Release|Win64.ActiveCfg = Release|x64`);
        Lines.push(`\t\t${PROJECT_GUID}.Release|Win64.Build.0 = Release|x64`);
        Lines.push("\tEndGlobalSection");
        Lines.push("\tGlobalSection(NestedProjects) = preSolution");
        Lines.push(`\t\t${PROJECT_GUID} = ${ENGINE_FOLDER_GUID}`);
        Lines.push("\tEndGlobalSection");
        Lines.push("\tGlobalSection(SolutionProperties) = preSolution");
        Lines.push("\t\tHideSolutionNode = FALSE");
        Lines.push("\tEndGlobalSection");
        Lines.push("EndGlobal");
        return Lines.join("\r\n");
    }

    private BuildFilters(Files: ProjectFiles): string {
        const Lines: string[] = [];
        Lines.push(`<?xml version="1.0" encoding="utf-8"?>`);
        Lines.push(`<Project ToolsVersion="17.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">`);
        Lines.push(`  <ItemGroup>`);
        const EmittedFilters = new Set<string>();
        for (const [ItemType, Items] of [["ClCompile", Files.Sources], ["ClInclude", Files.Headers], ["None", Files.Other]] as const) {
            for (const File of [...Items].sort()) {
                const Filter = this.GetFilter(File);
                const Segments = Filter.split("\\").filter(Boolean);
                for (let Index = 1; Index <= Segments.length; Index++) {
                    const CurrentFilter = Segments.slice(0, Index).join("\\");
                    if (EmittedFilters.has(CurrentFilter)) continue;
                    EmittedFilters.add(CurrentFilter);
                    Lines.push(`    <Filter Include="${XmlEscape(CurrentFilter)}">`);
                    Lines.push(`      <UniqueIdentifier>{${this.HashGuid(CurrentFilter)}}</UniqueIdentifier>`);
                    Lines.push(`    </Filter>`);
                }
                Lines.push(`    <${ItemType} Include="${XmlEscape(this.ToProjectRelativePath(File))}">`);
                Lines.push(`      <Filter>${XmlEscape(Filter)}</Filter>`);
                Lines.push(`    </${ItemType}>`);
            }
        }
        Lines.push(`  </ItemGroup>`);
        Lines.push(`</Project>`);
        return Lines.join("\r\n");
    }

    private CollectProjectFiles(Modules: ResolvedModule[]): ProjectFiles {
        const Files: ProjectFiles = {
            Sources: new Set<string>(),
            Headers: new Set<string>(),
            Other: new Set<string>(),
        };
        for (const Module of Modules) {
            this.CollectFiles(Module.Instance.SourceRoot, [".cpp", ".cc", ".c", ".mm", ".m"], Files.Sources);
            this.CollectFiles(Module.Instance.SourceRoot, [".h", ".hpp", ".inl"], Files.Headers);
            Files.Other.add(Module.Instance.BuildFilePath);
        }

        // Keep build configuration and shader files alongside the native source tree.
        this.CollectFiles(this.Paths.BuilderDirectory, [".ts", ".json", ".bat", ".sh"], Files.Other);
        this.CollectFiles(Path.join(this.Paths.EngineDirectory, "Content"), [".lsf"], Files.Other);
        return Files;
    }

    private async GetIntelliSenseConfiguration(): Promise<IntelliSenseConfiguration> {
        const Environment = await this.Toolchain.GetEnvironment();
        const IncludeVariable = Object.entries(Environment)
            .find(([Name]) => Name.toUpperCase() === "INCLUDE")?.[1] ?? "";
        return {
            IncludePaths: IncludeVariable
                .split(";")
                .filter(Boolean)
                .filter((PathName) => FsSync.existsSync(PathName)),
        };
    }

    private CollectFileIntelliSenseConfigurations(
        Files: ProjectFiles,
        Modules: ResolvedModule[],
        Target: Target,
        IntelliSense: IntelliSenseConfiguration,
    ): Map<string, FileIntelliSenseConfiguration> {
        const ModuleConfigurations = new Map<string, FileIntelliSenseConfiguration>();
        for (const Module of Modules) {
            ModuleConfigurations.set(
                Module.Instance.Descriptor.Name,
                this.CollectModuleIntelliSenseConfiguration(Module, Target, IntelliSense),
            );
        }

        const Result = new Map<string, FileIntelliSenseConfiguration>();
        for (const File of [...Files.Sources, ...Files.Headers]) {
            const Module = this.FindModuleForFile(File, Modules);
            if (!Module) continue;
            const Configuration = ModuleConfigurations.get(Module.Instance.Descriptor.Name);
            if (Configuration) Result.set(File, Configuration);
        }
        return Result;
    }

    private CollectModuleIntelliSenseConfiguration(
        Module: ResolvedModule,
        Target: Target,
        IntelliSense: IntelliSenseConfiguration,
    ): FileIntelliSenseConfiguration {
        const IncludePaths = new Set<string>();
        const Defines: Record<string, string> = {
            ...this.Graph.GetTransitiveExportDefines(Module),
            ...Module.Configuration.Defines,
            PLATFORM_WINDOWS: Target.Platform === "Win64" ? "1" : "0",
            WITH_EDITOR: Target.TargetType === "Editor" ? "1" : "0",
        };
        const AddModuleIncludes = (Current: ResolvedModule, IncludePrivate: boolean): void => {
            const Root = Current.Instance.SourceRoot;
            for (const Include of Current.Configuration.IncludePaths) {
                IncludePaths.add(this.ResolveVar(Include, Root));
            }
            const PublicDirectory = Path.join(Root, "Public");
            const PrivateDirectory = Path.join(Root, "Private");
            if (FsSync.existsSync(PublicDirectory)) IncludePaths.add(PublicDirectory);
            if (IncludePrivate && FsSync.existsSync(PrivateDirectory)) IncludePaths.add(PrivateDirectory);
        };

        AddModuleIncludes(Module, true);
        for (const Dependency of this.Graph.GetCompileDependencyClosure(Module)) {
            AddModuleIncludes(Dependency, false);
        }

        for (const IncludePath of IntelliSense.IncludePaths) {
            IncludePaths.add(IncludePath);
        }
        return {
            IncludePaths: [...IncludePaths].filter((PathName) => FsSync.existsSync(PathName)),
            Defines: Object.entries(Defines).map(([Name, Value]) => `${Name}=${Value}`),
        };
    }

    private FindModuleForFile(FilePath: string, Modules: ResolvedModule[]): ResolvedModule | undefined {
        return [...Modules]
            .sort((Left, Right) => Right.Instance.SourceRoot.length - Left.Instance.SourceRoot.length)
            .find((Module) => {
                const RelativePath = Path.relative(Module.Instance.SourceRoot, FilePath);
                return RelativePath !== "" && !RelativePath.startsWith("..") && !Path.isAbsolute(RelativePath);
            });
    }

    private GetFilters(Files: ProjectFiles): string[] {
        const Filters = new Set<string>();
        for (const File of [...Files.Sources, ...Files.Headers, ...Files.Other]) {
            const Filter = this.GetFilter(File);
            const Segments = Filter.split("\\");
            for (let Index = 1; Index <= Segments.length; Index++) {
                Filters.add(Segments.slice(0, Index).join("\\"));
            }
        }
        return [...Filters].sort((Left, Right) => Left.localeCompare(Right));
    }

    private GetFilter(FilePath: string): string {
        const RelativeDirectory = Path.dirname(Path.relative(this.Paths.EngineDirectory, FilePath));
        const Segments = RelativeDirectory.split(/[\\/]+/).filter(Boolean);
        return Segments.join("\\");
    }

    private AppendProjectItems(
        Lines: string[],
        ItemType: string,
        Files: Set<string>,
        Configurations?: Map<string, FileIntelliSenseConfiguration>,
    ): void {
        Lines.push(`  <ItemGroup>`);
        for (const File of [...Files].sort()) {
            const Configuration = Configurations?.get(File);
            if (!Configuration) {
                Lines.push(`    <${ItemType} Include="${XmlEscape(this.ToProjectRelativePath(File))}" />`);
                continue;
            }
            Lines.push(`    <${ItemType} Include="${XmlEscape(this.ToProjectRelativePath(File))}">`);
            Lines.push(`      <AdditionalIncludeDirectories>${XmlEscape(Configuration.IncludePaths.join(";"))};%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>`);
            Lines.push(`      <PreprocessorDefinitions>${XmlEscape(Configuration.Defines.join(";"))};%(PreprocessorDefinitions)</PreprocessorDefinitions>`);
            Lines.push(`    </${ItemType}>`);
        }
        Lines.push(`  </ItemGroup>`);
    }

    private AppendFilterItems(Lines: string[], ItemType: string, Files: Set<string>): void {
        Lines.push(`  <ItemGroup>`);
        for (const File of [...Files].sort()) {
            Lines.push(`    <${ItemType} Include="${XmlEscape(this.ToProjectRelativePath(File))}">`);
            Lines.push(`      <Filter>${XmlEscape(this.GetFilter(File))}</Filter>`);
            Lines.push(`    </${ItemType}>`);
        }
        Lines.push(`  </ItemGroup>`);
    }

    private ToProjectRelativePath(FilePath: string): string {
        return Path.relative(this.Paths.ProjectFilesDirectory, FilePath).replace(/[\\/]+/g, "\\");
    }

    private async RemoveObsoleteProjectFileLinks(): Promise<void> {
        for (const DirectoryName of ["Builder", "Content", "Source"]) {
            const LinkPath = Path.join(this.Paths.ProjectFilesDirectory, DirectoryName);
            let Stat: FsSync.Stats;
            try {
                Stat = FsSync.lstatSync(LinkPath);
            } catch {
                continue;
            }
            if (!Stat.isSymbolicLink()) {
                throw new Error(`Refusing to remove "${LinkPath}": it is not a generated directory link.`);
            }
            await Fs.unlink(LinkPath);
        }
    }

    private CollectFiles(Directory: string, Extensions: string[], Result: Set<string>): void {
        if (!FsSync.existsSync(Directory)) return;
        for (const Entry of FsSync.readdirSync(Directory, { withFileTypes: true })) {
            const FullPath = Path.join(Directory, Entry.name);
            if (Entry.isDirectory()) {
                this.CollectFiles(FullPath, Extensions, Result);
            } else if (Entry.isFile() && Extensions.includes(Path.extname(Entry.name).toLowerCase())) {
                Result.add(FullPath);
            }
        }
    }

    private ResolveVar(Value: string, Root: string): string {
        return Path.normalize(Value
            .replace("[module.SourceRoot]", Root)
            .replace("[project.SourceRootPath]", Root)
            .replace("[engine.Root]", this.Paths.Root)
            .replace("[engine.Source]", this.Paths.SourceDirectory));
    }

    private HashGuid(Name: string): string {
        let Hash = 0;
        for (let Index = 0; Index < Name.length; Index++) {
            Hash = ((Hash << 5) - Hash + Name.charCodeAt(Index)) | 0;
        }
        const Hex = Math.abs(Hash).toString(16).padStart(8, "0");
        return `${Hex.slice(0, 8)}-${Hex.slice(0, 4)}-4${Hex.slice(1, 4)}-8${Hex.slice(0, 3)}-${Hex.padEnd(12, "0")}`;
    }
}
