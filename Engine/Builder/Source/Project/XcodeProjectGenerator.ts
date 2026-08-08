import * as Crypto from "node:crypto";
import * as FsSync from "node:fs";
import * as Fs from "node:fs/promises";
import * as Path from "node:path";
import { SourceScanner } from "../Build/SourceScanner.ts";
import { ResolveTarget } from "../Configuration/Target.ts";
import type { ResolvedTarget, Target } from "../Configuration/Types.ts";
import type { ResolvedModule } from "../Graph/DependencyGraph.ts";
import type { EnginePaths } from "./EnginePaths.ts";

interface ProjectFile {
    AbsolutePath: string;
    ProjectRelativePath: string;
    FileType: string;
}

interface GroupNode {
    Name: string;
    Key: string;
    Path?: string;
    Files: ProjectFile[];
    Children: Map<string, GroupNode>;
}

interface XcodeSettings {
    IncludePaths: string[];
    LibraryPaths: string[];
    LibraryFlags: string[];
    Frameworks: string[];
    Defines: string[];
}

interface XcodeConfigurationTargets {
    Debug: ResolvedTarget;
    Release: ResolvedTarget;
}

function PbxId(Kind: string, Key: string): string {
    return Crypto.createHash("sha1").update(`${Kind}\0${Key}`).digest("hex").slice(0, 24).toUpperCase();
}

function PbxQuote(Value: string): string {
    return `"${Value.replace(/\\/g, "\\\\").replace(/"/g, "\\\"").replace(/\n/g, "\\n")}"`;
}

function XmlEscape(Value: string): string {
    return Value
        .replace(/&/g, "&amp;")
        .replace(/"/g, "&quot;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;");
}

function Comment(Value: string): string {
    return Value.replace(/\*\//g, "");
}

export class XcodeProjectGenerator {
    private readonly Paths: EnginePaths;
    private readonly Scanner = new SourceScanner();

    public constructor(Paths: EnginePaths) {
        this.Paths = Paths;
    }

    public async Generate(Modules: ResolvedModule[], BuildTarget: ResolvedTarget): Promise<string> {
        const Target = BuildTarget.Target;
        const ConfigurationTargets = this.ResolveConfigurationTargets(BuildTarget);
        const ProjectFiles = this.CollectProjectFiles();
        const CompiledSources = new Set<string>();
        for (const Module of Modules) {
            for (const SourceFile of await this.Scanner.Scan(
                Module.Instance.SourceRoot,
                Module.Configuration,
            )) {
                CompiledSources.add(Path.resolve(SourceFile));
            }
        }

        const ProjectDirectory = Path.join(this.Paths.Root, "LimitlessEngine.xcodeproj");
        const SchemeDirectory = Path.join(ProjectDirectory, "xcshareddata", "xcschemes");
        await Fs.mkdir(SchemeDirectory, { recursive: true });

        const Settings = this.CollectSettings(Modules, Target);
        await this.WriteFileIfChanged(
            Path.join(ProjectDirectory, "project.pbxproj"),
            this.BuildProject(ProjectFiles, CompiledSources, Settings, BuildTarget, ConfigurationTargets),
        );
        await this.WriteFileIfChanged(
            Path.join(SchemeDirectory, `${BuildTarget.Descriptor.Name}.xcscheme`),
            this.BuildScheme(BuildTarget, ConfigurationTargets),
        );
        return ProjectDirectory;
    }

    private BuildProject(
        Files: ProjectFile[],
        CompiledSources: Set<string>,
        Settings: XcodeSettings,
        BuildTarget: ResolvedTarget,
        ConfigurationTargets: XcodeConfigurationTargets,
    ): string {
        const Target = BuildTarget.Target;
        const TargetName = BuildTarget.Descriptor.Name;
        const MainGroupId = PbxId("group", "main");
        const ProductsGroupId = PbxId("group", "products");
        const FrameworksGroupId = PbxId("group", "frameworks");
        const ProductReferenceName = "$(PRODUCT_NAME)";
        const ProductFileId = PbxId("file", `product:${TargetName}`);
        const TargetId = PbxId("target", TargetName);
        const ProjectId = PbxId("project", "LimitlessEngine");
        const SourcesPhaseId = PbxId("phase", "sources");
        const FrameworksPhaseId = PbxId("phase", "frameworks");
        const ResourcesPhaseId = PbxId("phase", "resources");
        const ProjectConfigListId = PbxId("config-list", "project");
        const TargetConfigListId = PbxId("config-list", "target");

        const FileByPath = new Map(Files.map((File) => [Path.resolve(File.AbsolutePath), File]));
        const SourceFiles = [...CompiledSources]
            .map((SourcePath) => FileByPath.get(Path.resolve(SourcePath)))
            .filter((File): File is ProjectFile => File !== undefined)
            .sort((Left, Right) => Left.ProjectRelativePath.localeCompare(Right.ProjectRelativePath));
        if (SourceFiles.length !== CompiledSources.size) {
            throw new Error("Some compiled source files were not collected for the Xcode project.");
        }

        const SourceGroup = this.BuildGroupTree("Source", "Engine/Source", Files);
        const BuilderGroup = this.BuildGroupTree("Build System", "Engine/Builder", Files);
        const ContentGroup = this.BuildGroupTree("Content", "Engine/Content", Files);
        const RootGroups = [SourceGroup, BuilderGroup, ContentGroup]
            .filter((Group) => Group.Files.length > 0 || Group.Children.size > 0);

        const FrameworkNames = [...Settings.Frameworks].sort((Left, Right) => Left.localeCompare(Right));
        const Lines: string[] = [
            "// !$*UTF8*$!",
            "{",
            "\tarchiveVersion = 1;",
            "\tclasses = {",
            "\t};",
            "\tobjectVersion = 56;",
            "\tobjects = {",
            "",
            "/* Begin PBXBuildFile section */",
        ];

        for (const File of SourceFiles) {
            const FileId = PbxId("file", File.ProjectRelativePath);
            const BuildFileId = PbxId("build-file", File.ProjectRelativePath);
            Lines.push(`\t\t${BuildFileId} /* ${Comment(Path.basename(File.AbsolutePath))} in Sources */ = {isa = PBXBuildFile; fileRef = ${FileId} /* ${Comment(Path.basename(File.AbsolutePath))} */; };`);
        }
        for (const Framework of FrameworkNames) {
            const FileId = PbxId("framework-file", Framework);
            const BuildFileId = PbxId("framework-build-file", Framework);
            Lines.push(`\t\t${BuildFileId} /* ${Comment(Framework)}.framework in Frameworks */ = {isa = PBXBuildFile; fileRef = ${FileId} /* ${Comment(Framework)}.framework */; };`);
        }
        Lines.push("/* End PBXBuildFile section */", "", "/* Begin PBXFileReference section */");

        for (const File of Files.sort((Left, Right) => Left.ProjectRelativePath.localeCompare(Right.ProjectRelativePath))) {
            const FileId = PbxId("file", File.ProjectRelativePath);
            Lines.push(`\t\t${FileId} /* ${Comment(Path.basename(File.AbsolutePath))} */ = {isa = PBXFileReference; lastKnownFileType = ${File.FileType}; path = ${PbxQuote(Path.basename(File.AbsolutePath))}; sourceTree = "<group>"; };`);
        }
        for (const Framework of FrameworkNames) {
            const FileId = PbxId("framework-file", Framework);
            Lines.push(`\t\t${FileId} /* ${Comment(Framework)}.framework */ = {isa = PBXFileReference; lastKnownFileType = wrapper.framework; name = ${PbxQuote(`${Framework}.framework`)}; path = ${PbxQuote(`System/Library/Frameworks/${Framework}.framework`)}; sourceTree = SDKROOT; };`);
        }
        Lines.push(`\t\t${ProductFileId} /* Product */ = {isa = PBXFileReference; explicitFileType = "compiled.mach-o.executable"; includeInIndex = 0; path = ${PbxQuote(ProductReferenceName)}; sourceTree = BUILT_PRODUCTS_DIR; };`);
        Lines.push("/* End PBXFileReference section */", "", "/* Begin PBXFrameworksBuildPhase section */");
        Lines.push(`\t\t${FrameworksPhaseId} /* Frameworks */ = {`);
        Lines.push("\t\t\tisa = PBXFrameworksBuildPhase;", "\t\t\tbuildActionMask = 2147483647;", "\t\t\tfiles = (");
        for (const Framework of FrameworkNames) {
            Lines.push(`\t\t\t\t${PbxId("framework-build-file", Framework)} /* ${Comment(Framework)}.framework in Frameworks */,`);
        }
        Lines.push("\t\t\t);", "\t\t\trunOnlyForDeploymentPostprocessing = 0;", "\t\t};");
        Lines.push("/* End PBXFrameworksBuildPhase section */", "", "/* Begin PBXGroup section */");

        Lines.push(`\t\t${MainGroupId} = {`);
        Lines.push("\t\t\tisa = PBXGroup;", "\t\t\tchildren = (");
        for (const Group of RootGroups) {
            Lines.push(`\t\t\t\t${PbxId("group", Group.Key)} /* ${Comment(Group.Name)} */,`);
        }
        Lines.push(`\t\t\t\t${FrameworksGroupId} /* Frameworks */,`);
        Lines.push(`\t\t\t\t${ProductsGroupId} /* Products */,`);
        Lines.push("\t\t\t);", "\t\t\tsourceTree = \"<group>\";", "\t\t};");
        for (const Group of RootGroups) {
            this.AppendGroupObjects(Lines, Group);
        }
        Lines.push(`\t\t${FrameworksGroupId} /* Frameworks */ = {`);
        Lines.push("\t\t\tisa = PBXGroup;", "\t\t\tchildren = (");
        for (const Framework of FrameworkNames) {
            Lines.push(`\t\t\t\t${PbxId("framework-file", Framework)} /* ${Comment(Framework)}.framework */,`);
        }
        Lines.push("\t\t\t);", "\t\t\tname = Frameworks;", "\t\t\tsourceTree = \"<group>\";", "\t\t};");
        Lines.push(`\t\t${ProductsGroupId} /* Products */ = {`);
        Lines.push("\t\t\tisa = PBXGroup;", "\t\t\tchildren = (");
        Lines.push(`\t\t\t\t${ProductFileId} /* Product */,`);
        Lines.push("\t\t\t);", "\t\t\tname = Products;", "\t\t\tsourceTree = \"<group>\";", "\t\t};");
        Lines.push("/* End PBXGroup section */", "", "/* Begin PBXNativeTarget section */");
        Lines.push(`\t\t${TargetId} /* ${Comment(TargetName)} */ = {`);
        Lines.push("\t\t\tisa = PBXNativeTarget;");
        Lines.push(`\t\t\tbuildConfigurationList = ${TargetConfigListId} /* Build configuration list for PBXNativeTarget \"${Comment(TargetName)}\" */;`);
        Lines.push("\t\t\tbuildPhases = (");
        Lines.push(`\t\t\t\t${SourcesPhaseId} /* Sources */,`, `\t\t\t\t${FrameworksPhaseId} /* Frameworks */,`, `\t\t\t\t${ResourcesPhaseId} /* Resources */,`);
        Lines.push("\t\t\t);", "\t\t\tbuildRules = (", "\t\t\t);", "\t\t\tdependencies = (", "\t\t\t);");
        Lines.push(`\t\t\tname = ${PbxQuote(TargetName)};`, `\t\t\tproductName = ${PbxQuote(ProductReferenceName)};`);
        Lines.push(`\t\t\tproductReference = ${ProductFileId} /* Product */;`, "\t\t\tproductType = \"com.apple.product-type.tool\";", "\t\t};");
        Lines.push("/* End PBXNativeTarget section */", "", "/* Begin PBXProject section */");
        Lines.push(`\t\t${ProjectId} /* Project object */ = {`);
        Lines.push("\t\t\tisa = PBXProject;", "\t\t\tattributes = {", "\t\t\t\tBuildIndependentTargetsInParallel = 1;", "\t\t\t\tLastUpgradeCheck = 2640;", "\t\t\t};");
        Lines.push(`\t\t\tbuildConfigurationList = ${ProjectConfigListId} /* Build configuration list for PBXProject \"LimitlessEngine\" */;`);
        Lines.push("\t\t\tcompatibilityVersion = \"Xcode 14.0\";", "\t\t\tdevelopmentRegion = en;", "\t\t\thasScannedForEncodings = 0;", "\t\t\tknownRegions = (", "\t\t\t\ten,", "\t\t\t\tBase,", "\t\t\t);");
        Lines.push(`\t\t\tmainGroup = ${MainGroupId};`, `\t\t\tproductRefGroup = ${ProductsGroupId} /* Products */;`);
        Lines.push("\t\t\tprojectDirPath = \"\";", "\t\t\tprojectRoot = \"\";", "\t\t\ttargets = (", `\t\t\t\t${TargetId} /* ${Comment(TargetName)} */,`, "\t\t\t);", "\t\t};");
        Lines.push("/* End PBXProject section */", "", "/* Begin PBXResourcesBuildPhase section */");
        Lines.push(`\t\t${ResourcesPhaseId} /* Resources */ = {isa = PBXResourcesBuildPhase; buildActionMask = 2147483647; files = (); runOnlyForDeploymentPostprocessing = 0; };`);
        Lines.push("/* End PBXResourcesBuildPhase section */", "", "/* Begin PBXSourcesBuildPhase section */");
        Lines.push(`\t\t${SourcesPhaseId} /* Sources */ = {`);
        Lines.push("\t\t\tisa = PBXSourcesBuildPhase;", "\t\t\tbuildActionMask = 2147483647;", "\t\t\tfiles = (");
        for (const File of SourceFiles) {
            Lines.push(`\t\t\t\t${PbxId("build-file", File.ProjectRelativePath)} /* ${Comment(Path.basename(File.AbsolutePath))} in Sources */,`);
        }
        Lines.push("\t\t\t);", "\t\t\trunOnlyForDeploymentPostprocessing = 0;", "\t\t};");
        Lines.push("/* End PBXSourcesBuildPhase section */", "", "/* Begin XCBuildConfiguration section */");

        for (const Configuration of ["Debug", "Release"] as const) {
            this.AppendProjectBuildConfiguration(Lines, Configuration, Settings, Target);
            this.AppendTargetBuildConfiguration(
                Lines,
                Configuration,
                ConfigurationTargets[Configuration].OutputName,
            );
        }
        Lines.push("/* End XCBuildConfiguration section */", "", "/* Begin XCConfigurationList section */");
        Lines.push(`\t\t${ProjectConfigListId} /* Build configuration list for PBXProject \"LimitlessEngine\" */ = {`);
        Lines.push("\t\t\tisa = XCConfigurationList;", "\t\t\tbuildConfigurations = (", `\t\t\t\t${PbxId("project-config", "Debug")} /* Debug */,`, `\t\t\t\t${PbxId("project-config", "Release")} /* Release */,`, "\t\t\t);", "\t\t\tdefaultConfigurationIsVisible = 0;", "\t\t\tdefaultConfigurationName = Debug;", "\t\t};");
        Lines.push(`\t\t${TargetConfigListId} /* Build configuration list for PBXNativeTarget \"${Comment(TargetName)}\" */ = {`);
        Lines.push("\t\t\tisa = XCConfigurationList;", "\t\t\tbuildConfigurations = (", `\t\t\t\t${PbxId("target-config", "Debug")} /* Debug */,`, `\t\t\t\t${PbxId("target-config", "Release")} /* Release */,`, "\t\t\t);", "\t\t\tdefaultConfigurationIsVisible = 0;", "\t\t\tdefaultConfigurationName = Debug;", "\t\t};");
        Lines.push("/* End XCConfigurationList section */", "", "\t};", `\trootObject = ${ProjectId} /* Project object */;`, "}", "");
        return Lines.join("\n");
    }

    private AppendProjectBuildConfiguration(
        Lines: string[],
        Configuration: "Debug" | "Release",
        Settings: XcodeSettings,
        Target: Target,
    ): void {
        const ConfigId = PbxId("project-config", Configuration);
        const Defines = [
            "$(inherited)",
            ...Settings.Defines,
            `WITH_EDITOR=${Target.TargetType === "Editor" ? "1" : "0"}`,
            "PLATFORM_MAC=1",
            Configuration === "Debug" ? "DEBUG=1" : "NDEBUG=1",
        ];
        Lines.push(`\t\t${ConfigId} /* ${Configuration} */ = {`, "\t\t\tisa = XCBuildConfiguration;", "\t\t\tbuildSettings = {");
        Lines.push("\t\t\t\tALWAYS_SEARCH_USER_PATHS = NO;", "\t\t\t\tARCHS = \"$(ARCHS_STANDARD)\";", "\t\t\t\tCLANG_CXX_LANGUAGE_STANDARD = \"c++17\";", "\t\t\t\tCLANG_CXX_LIBRARY = \"libc++\";", "\t\t\t\tCLANG_ENABLE_MODULES = NO;", "\t\t\t\tCODE_SIGNING_ALLOWED = NO;", "\t\t\t\tGCC_ENABLE_CPP_EXCEPTIONS = NO;", "\t\t\t\tGCC_ENABLE_CPP_RTTI = NO;");
        this.AppendBuildSettingArray(Lines, "GCC_PREPROCESSOR_DEFINITIONS", Defines);
        this.AppendBuildSettingArray(Lines, "HEADER_SEARCH_PATHS", ["$(inherited)", ...Settings.IncludePaths]);
        this.AppendBuildSettingArray(Lines, "LIBRARY_SEARCH_PATHS", ["$(inherited)", ...Settings.LibraryPaths]);
        this.AppendBuildSettingArray(Lines, "OTHER_LDFLAGS", ["$(inherited)", "-ObjC", ...Settings.LibraryFlags]);
        Lines.push(`\t\t\t\tGCC_OPTIMIZATION_LEVEL = ${Configuration === "Debug" ? "0" : "3"};`);
        Lines.push(`\t\t\t\tONLY_ACTIVE_ARCH = ${Configuration === "Debug" ? "YES" : "NO"};`);
        Lines.push("\t\t\t\tSDKROOT = macosx;", "\t\t\t\tUSE_HEADERMAP = NO;", "\t\t\t};", `\t\t\tname = ${Configuration};`, "\t\t};");
    }

    private AppendTargetBuildConfiguration(
        Lines: string[],
        Configuration: "Debug" | "Release",
        ProductName: string,
    ): void {
        const ConfigId = PbxId("target-config", Configuration);
        Lines.push(`\t\t${ConfigId} /* ${Configuration} */ = {`, "\t\t\tisa = XCBuildConfiguration;", "\t\t\tbuildSettings = {");
        Lines.push("\t\t\t\tCONFIGURATION_BUILD_DIR = \"$(SRCROOT)/Engine/Binaries/Mac\";", `\t\t\t\tCOPY_PHASE_STRIP = ${Configuration === "Debug" ? "NO" : "YES"};`, `\t\t\t\tDEBUG_INFORMATION_FORMAT = ${Configuration === "Debug" ? "dwarf" : "\"dwarf-with-dsym\""};`, "\t\t\t\tGENERATE_INFOPLIST_FILE = NO;", "\t\t\t\tMACH_O_TYPE = mh_execute;", `\t\t\t\tPRODUCT_NAME = ${PbxQuote(ProductName)};`, "\t\t\t\tSKIP_INSTALL = NO;", "\t\t\t};", `\t\t\tname = ${Configuration};`, "\t\t};");
    }

    private AppendBuildSettingArray(Lines: string[], Name: string, Values: string[]): void {
        const UniqueValues = [...new Set(Values)];
        Lines.push(`\t\t\t\t${Name} = (`);
        for (const Value of UniqueValues) {
            Lines.push(`\t\t\t\t\t${PbxQuote(Value)},`);
        }
        Lines.push("\t\t\t\t);");
    }

    private AppendGroupObjects(Lines: string[], Group: GroupNode): void {
        const GroupId = PbxId("group", Group.Key);
        Lines.push(`\t\t${GroupId} /* ${Comment(Group.Name)} */ = {`, "\t\t\tisa = PBXGroup;", "\t\t\tchildren = (");
        for (const Child of [...Group.Children.values()].sort((Left, Right) => Left.Name.localeCompare(Right.Name))) {
            Lines.push(`\t\t\t\t${PbxId("group", Child.Key)} /* ${Comment(Child.Name)} */,`);
        }
        for (const File of Group.Files.sort((Left, Right) => Path.basename(Left.AbsolutePath).localeCompare(Path.basename(Right.AbsolutePath)))) {
            Lines.push(`\t\t\t\t${PbxId("file", File.ProjectRelativePath)} /* ${Comment(Path.basename(File.AbsolutePath))} */,`);
        }
        Lines.push("\t\t\t);");
        if (Group.Path) {
            Lines.push(`\t\t\tpath = ${PbxQuote(Group.Path)};`);
        } else {
            Lines.push(`\t\t\tname = ${PbxQuote(Group.Name)};`);
        }
        Lines.push(`\t\t\tsourceTree = ${Group.Key.includes(":") ? "\"<group>\"" : "SOURCE_ROOT"};`, "\t\t};");
        for (const Child of [...Group.Children.values()].sort((Left, Right) => Left.Name.localeCompare(Right.Name))) {
            this.AppendGroupObjects(Lines, Child);
        }
    }

    private BuildGroupTree(Name: string, RootRelativePath: string, Files: ProjectFile[]): GroupNode {
        const Root: GroupNode = {
            Name,
            Key: RootRelativePath,
            Path: RootRelativePath,
            Files: [],
            Children: new Map<string, GroupNode>(),
        };
        const Prefix = `${RootRelativePath}/`;
        for (const File of Files.filter((Entry) => Entry.ProjectRelativePath.startsWith(Prefix))) {
            const RelativePath = File.ProjectRelativePath.slice(Prefix.length);
            const Segments = RelativePath.split("/");
            let Current = Root;
            for (const Segment of Segments.slice(0, -1)) {
                let Child = Current.Children.get(Segment);
                if (!Child) {
                    Child = {
                        Name: Segment,
                        Key: `${Current.Key}:${Segment}`,
                        Path: Segment,
                        Files: [],
                        Children: new Map<string, GroupNode>(),
                    };
                    Current.Children.set(Segment, Child);
                }
                Current = Child;
            }
            Current.Files.push(File);
        }
        return Root;
    }

    private CollectProjectFiles(): ProjectFile[] {
        const Files: ProjectFile[] = [];
        this.CollectFiles(this.Paths.SourceDirectory, new Set([".c", ".cc", ".cpp", ".h", ".hpp", ".inl", ".m", ".mm", ".ts"]), Files);
        this.CollectFiles(this.Paths.BuilderDirectory, new Set([".bat", ".json", ".sh", ".ts"]), Files);
        this.CollectFiles(Path.join(this.Paths.EngineDirectory, "Content"), new Set([".lsf", ".spv"]), Files);
        return Files.sort((Left, Right) => Left.ProjectRelativePath.localeCompare(Right.ProjectRelativePath));
    }

    private CollectFiles(Directory: string, Extensions: Set<string>, Result: ProjectFile[]): void {
        if (!FsSync.existsSync(Directory)) {
            return;
        }
        for (const Entry of FsSync.readdirSync(Directory, { withFileTypes: true })) {
            const FullPath = Path.join(Directory, Entry.name);
            if (Entry.isDirectory()) {
                this.CollectFiles(FullPath, Extensions, Result);
            } else if (Entry.isFile() && Extensions.has(Path.extname(Entry.name).toLowerCase())) {
                Result.push({
                    AbsolutePath: FullPath,
                    ProjectRelativePath: Path.relative(this.Paths.Root, FullPath).replace(/\\/g, "/"),
                    FileType: this.GetFileType(Entry.name),
                });
            }
        }
    }

    private CollectSettings(Modules: ResolvedModule[], Target: Target): XcodeSettings {
        const IncludePaths = new Set<string>();
        const LibraryPaths = new Set<string>();
        const LibraryFlags = new Set<string>();
        const Frameworks = new Set<string>();
        const Defines = new Set<string>();

        for (const Module of Modules) {
            const SourceRoot = Module.Instance.SourceRoot;
            for (const Directory of [Path.join(SourceRoot, "Public"), Path.join(SourceRoot, "Private")]) {
                if (FsSync.existsSync(Directory)) {
                    IncludePaths.add(this.ToBuildSettingPath(Directory));
                }
            }
            for (const IncludePath of Module.Configuration.IncludePaths) {
                const Resolved = this.ResolvePathVar(IncludePath, SourceRoot);
                if (FsSync.existsSync(Resolved)) {
                    IncludePaths.add(this.ToBuildSettingPath(Resolved));
                }
            }
            for (const LibraryPath of Module.Configuration.LibraryPaths) {
                LibraryPaths.add(this.ToBuildSettingPath(this.ResolvePathVar(LibraryPath, SourceRoot)));
            }
            for (const LibraryFile of Module.Configuration.LibraryFiles) {
                LibraryFlags.add(this.ToLibraryFlag(LibraryFile));
            }
            for (const Framework of Module.Configuration.SystemFrameworks) {
                Frameworks.add(Framework.replace(/\.framework$/i, ""));
            }
            for (const [DefineName, DefineValue] of Object.entries({
                ...Module.Configuration.ExportDefines,
                ...Module.Configuration.Defines,
            })) {
                Defines.add(`${DefineName}=${DefineValue}`);
            }
        }

        if (Target.Platform !== "Mac") {
            throw new Error(`Xcode projects require the Mac platform, got ${Target.Platform}.`);
        }
        return {
            IncludePaths: [...IncludePaths].sort(),
            LibraryPaths: [...LibraryPaths].sort(),
            LibraryFlags: [...LibraryFlags].sort(),
            Frameworks: [...Frameworks].sort(),
            Defines: [...Defines].sort(),
        };
    }

    private ResolvePathVar(Value: string, SourceRoot: string): string {
        return Path.normalize(Value
            .replace("[module.SourceRoot]", SourceRoot)
            .replace("[project.SourceRootPath]", SourceRoot)
            .replace("[engine.Root]", this.Paths.Root)
            .replace("[engine.Source]", this.Paths.SourceDirectory));
    }

    private ToBuildSettingPath(Value: string): string {
        const RelativePath = Path.relative(this.Paths.Root, Value);
        if (RelativePath !== "" && !RelativePath.startsWith("..") && !Path.isAbsolute(RelativePath)) {
            return `$(SRCROOT)/${RelativePath.replace(/\\/g, "/")}`;
        }
        return Value.replace(/\\/g, "/");
    }

    private ToLibraryFlag(LibraryFile: string): string {
        if (LibraryFile.startsWith("-") || Path.isAbsolute(LibraryFile)) {
            return LibraryFile;
        }
        const Name = Path.basename(LibraryFile).replace(/^lib/, "").replace(/\.(a|dylib|so|lib)$/i, "");
        return `-l${Name}`;
    }

    private GetFileType(FileName: string): string {
        switch (Path.extname(FileName).toLowerCase()) {
            case ".c": return "sourcecode.c.c";
            case ".cc":
            case ".cpp": return "sourcecode.cpp.cpp";
            case ".m": return "sourcecode.c.objc";
            case ".mm": return "sourcecode.cpp.objcpp";
            case ".h":
            case ".hpp":
            case ".inl": return "sourcecode.c.h";
            case ".ts": return "sourcecode.typescript";
            case ".json": return "text.json";
            case ".sh": return "text.script.sh";
            case ".spv": return "file";
            default: return "text";
        }
    }

    private BuildScheme(
        BuildTarget: ResolvedTarget,
        ConfigurationTargets: XcodeConfigurationTargets,
    ): string {
        const TargetName = BuildTarget.Descriptor.Name;
        const TargetId = PbxId("target", TargetName);
        const DebugProductName = ConfigurationTargets.Debug.OutputName;
        const ReleaseProductName = ConfigurationTargets.Release.OutputName;
        return `<?xml version="1.0" encoding="UTF-8"?>
<Scheme LastUpgradeVersion="2640" version="1.7">
   <BuildAction parallelizeBuildables="YES" buildImplicitDependencies="YES">
      <BuildActionEntries>
         <BuildActionEntry buildForTesting="YES" buildForRunning="YES" buildForProfiling="YES" buildForArchiving="YES" buildForAnalyzing="YES">
            <BuildableReference BuildableIdentifier="primary" BlueprintIdentifier="${TargetId}" BuildableName="${XmlEscape(DebugProductName)}" BlueprintName="${XmlEscape(TargetName)}" ReferencedContainer="container:LimitlessEngine.xcodeproj"/>
         </BuildActionEntry>
      </BuildActionEntries>
   </BuildAction>
   <TestAction buildConfiguration="Debug" selectedDebuggerIdentifier="Xcode.DebuggerFoundation.Debugger.LLDB" selectedLauncherIdentifier="Xcode.DebuggerFoundation.Launcher.LLDB" shouldUseLaunchSchemeArgsEnv="YES"/>
   <LaunchAction buildConfiguration="Debug" selectedDebuggerIdentifier="Xcode.DebuggerFoundation.Debugger.LLDB" selectedLauncherIdentifier="Xcode.DebuggerFoundation.Launcher.LLDB" launchStyle="0" useCustomWorkingDirectory="YES" customWorkingDirectory="${XmlEscape(this.Paths.Root)}" ignoresPersistentStateOnLaunch="NO" debugDocumentVersioning="YES" allowLocationSimulation="YES">
      <BuildableProductRunnable runnableDebuggingMode="0">
         <BuildableReference BuildableIdentifier="primary" BlueprintIdentifier="${TargetId}" BuildableName="${XmlEscape(DebugProductName)}" BlueprintName="${XmlEscape(TargetName)}" ReferencedContainer="container:LimitlessEngine.xcodeproj"/>
      </BuildableProductRunnable>
   </LaunchAction>
   <ProfileAction buildConfiguration="Release" shouldUseLaunchSchemeArgsEnv="YES" savedToolIdentifier="" useCustomWorkingDirectory="YES" customWorkingDirectory="${XmlEscape(this.Paths.Root)}" debugDocumentVersioning="YES">
      <BuildableProductRunnable runnableDebuggingMode="0">
         <BuildableReference BuildableIdentifier="primary" BlueprintIdentifier="${TargetId}" BuildableName="${XmlEscape(ReleaseProductName)}" BlueprintName="${XmlEscape(TargetName)}" ReferencedContainer="container:LimitlessEngine.xcodeproj"/>
      </BuildableProductRunnable>
   </ProfileAction>
   <AnalyzeAction buildConfiguration="Debug"/>
   <ArchiveAction buildConfiguration="Release" revealArchiveInOrganizer="YES"/>
</Scheme>
`;
    }

    private ResolveConfigurationTargets(BuildTarget: ResolvedTarget): XcodeConfigurationTargets {
        const ResolveConfiguration = (Optimization: "Debug" | "Release"): ResolvedTarget =>
            BuildTarget.Target.Optimization === Optimization
                ? BuildTarget
                : ResolveTarget(BuildTarget.Descriptor, {
                    ...BuildTarget.Target,
                    Optimization,
                });
        return {
            Debug: ResolveConfiguration("Debug"),
            Release: ResolveConfiguration("Release"),
        };
    }

    private async WriteFileIfChanged(FilePath: string, Contents: string): Promise<void> {
        let ExistingContents: string | undefined;
        try {
            ExistingContents = await Fs.readFile(FilePath, "utf-8");
        } catch (CaughtError) {
            const ErrorCode = (CaughtError as NodeJS.ErrnoException).code;
            if (ErrorCode !== "ENOENT") {
                throw CaughtError;
            }
        }
        if (ExistingContents !== Contents) {
            await Fs.writeFile(FilePath, Contents, "utf-8");
        }
    }
}
