import * as Assert from "node:assert/strict";
import * as Fs from "node:fs/promises";
import * as Os from "node:os";
import * as Path from "node:path";
import Test from "node:test";
import AssetBuild from "../../Source/Runtime/Asset/Build.ts";
import DemoApplicationBuild from "../../Source/Runtime/DemoApplication/Build.ts";
import RendererBuild from "../../Source/Runtime/Renderer/Build.ts";
import WorldBuild from "../../Source/Runtime/World/Build.ts";
import WorldRendererBuild from "../../Source/Runtime/WorldRenderer/Build.ts";
import { NinjaBackend } from "../Source/Backend/NinjaBackend.ts";
import { APIMacroName } from "../Source/Build/APIMacro.ts";
import { IRBuilder } from "../Source/Build/IRBuilder.ts";
import { CreateModuleConfiguration, ModuleBuild } from "../Source/Configuration/ModuleBuild.ts";
import { ResolveTarget } from "../Source/Configuration/Target.ts";
import {
    Optimization,
    OutputType,
    Platform,
    TargetType,
    type DependencyRef,
    type ModuleConfiguration,
    type ModuleInstance,
    type Target,
    type TargetDescriptor,
} from "../Source/Configuration/Types.ts";
import { DependencyGraph } from "../Source/Graph/DependencyGraph.ts";
import { EnginePaths } from "../Source/Project/EnginePaths.ts";
import type { IToolchain } from "../Source/Toolchain/IToolchain.ts";
import { GetMSVCLinkOutputs } from "../Source/Toolchain/MSVCToolchain.ts";

interface SyntheticModuleOptions {
    Public?: DependencyRef[];
    Private?: DependencyRef[];
    ThirdParty?: boolean;
    Defines?: Record<string, string>;
    ExportDefines?: Record<string, string>;
    Output?: ModuleConfiguration["Output"];
    LibraryFiles?: string[];
}

class SyntheticModuleBuild extends ModuleBuild {
    public readonly Name: string;
    public override readonly ThirdParty: boolean;
    private readonly Options: SyntheticModuleOptions;

    public constructor(Name: string, Options: SyntheticModuleOptions = {}) {
        super();
        this.Name = Name;
        this.Options = Options;
        this.ThirdParty = Options.ThirdParty ?? false;
    }

    public Configure(_Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.PublicDependencies.push(...(this.Options.Public ?? []));
        Configuration.PrivateDependencies.push(...(this.Options.Private ?? []));
        Object.assign(Configuration.Defines, this.Options.Defines);
        Object.assign(Configuration.ExportDefines, this.Options.ExportDefines);
        Configuration.LibraryFiles.push(...(this.Options.LibraryFiles ?? []));
        if (this.Options.Output) Configuration.Output = this.Options.Output;
    }
}

class RecordingToolchain implements IToolchain {
    public readonly Name = "Recording";
    public readonly Platform = "Win64";
    public FindCompiler(): string { return "fake-cl"; }
    public FindLinker(): string { return "fake-link"; }
    public FindArchiver(): string { return "fake-lib"; }
    public GetSystemIncludePaths(): string[] { return []; }
    public GetSystemLibPaths(): string[] { return []; }
    public MakeCompileCommand(
        Source: string,
        Output: string,
        _Includes: string[],
        Defines: Record<string, string>,
    ): string[] {
        return [
            ...Object.entries(Defines).map(([Name, Value]) => `/D${Name}=${Value}`),
            "/c", Source, `/Fo${Output}`,
        ];
    }
    public MakeLinkCommand(
        Output: string,
        Objects: string[],
        Libraries: string[],
        _LibraryPaths: string[],
        _Target: Target,
        IsDll: boolean,
    ): string[] {
        const Outputs = GetMSVCLinkOutputs(Output, IsDll);
        return [
            `/OUT:${Output}`,
            ...(IsDll ? [`/IMPLIB:${Outputs[1]}`] : []),
            ...Objects,
            ...Libraries,
        ];
    }
    public GetLinkOutputs(Output: string, IsDll: boolean): string[] {
        return GetMSVCLinkOutputs(Output, IsDll);
    }
    public MakeArchiveCommand(Output: string, Objects: string[]): string[] {
        return [`/OUT:${Output}`, ...Objects];
    }
    public async GetEnvironment(): Promise<Record<string, string>> { return {}; }
}

async function MakeModule(
    Root: string,
    Name: string,
    Options: SyntheticModuleOptions = {},
): Promise<ModuleInstance> {
    const SourceRoot = Path.join(Root, "Engine", "Source", Name);
    await Fs.mkdir(Path.join(SourceRoot, "Private"), { recursive: true });
    await Fs.writeFile(Path.join(SourceRoot, "Private", `${Name}.cpp`), `int ${Name}Symbol() { return 1; }\n`);
    return {
        Descriptor: new SyntheticModuleBuild(Name, Options),
        SourceRoot,
        BuildFilePath: Path.join(SourceRoot, "Build.ts"),
    };
}

function HasDefine(Command: string[], Name: string, Value: string): boolean {
    return Command.includes(`/D${Name}=${Value}`);
}

Test("Editor DLL consumers receive export macros and link through producer import libraries", async (Context) => {
    const Root = await Fs.mkdtemp(Path.join(Os.tmpdir(), "limitless-editor-dll-"));
    Context.after(() => Fs.rm(Root, { recursive: true, force: true }));
    const Modules = [
        await MakeModule(Root, "Vendor", {
            ThirdParty: true,
            Output: OutputType.Lib,
            Defines: { VENDOR_BUILD_ONLY: "1" },
            ExportDefines: { VENDOR_PUBLIC: "1" },
            LibraryFiles: ["vendor-system.lib"],
        }),
        await MakeModule(Root, "Base", { Public: ["Vendor"] }),
        await MakeModule(Root, "PrivateBase"),
        await MakeModule(Root, "Skipped"),
        await MakeModule(Root, "Unrelated"),
        await MakeModule(Root, "Entry", {
            Public: ["Base", { Name: "Skipped", WithoutLinking: true }],
            Private: ["PrivateBase"],
        }),
    ];
    const Target: Target = {
        Platform: Platform.Win64,
        Optimization: Optimization.Debug,
        TargetType: TargetType.Editor,
    };
    const Descriptor: TargetDescriptor = {
        Name: "SyntheticEditor",
        Modules: Modules.map((Module) => Module.Descriptor.Name),
        EntryModule: "Entry",
        Matrix: [Target],
        OutputName: () => "SyntheticEditorDebug",
    };
    const BuildTarget = ResolveTarget(Descriptor, Target);
    const Graph = new DependencyGraph(Modules, BuildTarget);
    const ResolvedModules = Graph.Build();
    const Paths = new EnginePaths(Root);
    const Toolchain = new RecordingToolchain();
    const Actions = await new IRBuilder(Paths, Toolchain, BuildTarget, Graph).Build(ResolvedModules);

    const EntryCompile = Actions.find((Action) => Action.Id.startsWith("Entry::compile::"));
    const BaseCompile = Actions.find((Action) => Action.Id.startsWith("Base::compile::"));
    Assert.ok(EntryCompile);
    Assert.ok(BaseCompile);
    Assert.equal(HasDefine(BaseCompile.Command, "BASE_API", "__declspec(dllexport)"), true);
    Assert.equal(HasDefine(EntryCompile.Command, "BASE_API", "__declspec(dllimport)"), true);
    Assert.equal(HasDefine(EntryCompile.Command, "PRIVATEBASE_API", "__declspec(dllimport)"), true);
    Assert.equal(HasDefine(EntryCompile.Command, "SKIPPED_API", "__declspec(dllimport)"), true,
        "WithoutLinking must not block compile-visible export defines");
    Assert.equal(HasDefine(EntryCompile.Command, "VENDOR_PUBLIC", "1"), true);
    Assert.equal(EntryCompile.Command.some((Token) => Token.startsWith("/DVENDOR_BUILD_ONLY=")), false,
        "dependency self defines must never leak to consumers");
    Assert.equal(EntryCompile.Command.some((Token) => Token.includes("BASE_API=__declspec(dllexport)")), false);

    const BinaryRoot = Paths.BinaryOutputDirectory(BuildTarget);
    const BaseDll = Path.join(BinaryRoot, "Base.dll");
    const BaseImportLibrary = Path.join(BinaryRoot, "Base.lib");
    const VendorLibrary = Path.join(BinaryRoot, "Vendor.lib");
    const PrivateImportLibrary = Path.join(BinaryRoot, "PrivateBase.lib");
    const SkippedImportLibrary = Path.join(BinaryRoot, "Skipped.lib");
    const UnrelatedImportLibrary = Path.join(BinaryRoot, "Unrelated.lib");
    const BaseLink = Actions.find((Action) => Action.Id === "Base::link");
    Assert.ok(BaseLink);
    Assert.deepEqual(BaseLink.Outputs, [BaseDll, BaseImportLibrary]);
    Assert.ok(BaseLink.Command.includes(`/IMPLIB:${BaseImportLibrary}`));
    Assert.ok(BaseLink.Inputs.includes(VendorLibrary));
    Assert.ok(BaseLink.Command.includes("vendor-system.lib"),
        "external link settings from public third-party dependencies must reach a DLL linker");

    const Executable = Actions.find((Action) => Action.Id === "SyntheticEditor::exe");
    Assert.ok(Executable);
    Assert.ok(Executable.Inputs.includes(BaseImportLibrary));
    Assert.ok(Executable.Inputs.includes(PrivateImportLibrary), "direct private dependencies are link-visible");
    Assert.equal(Executable.Inputs.includes(BaseDll), false, "a DLL is not itself a linker input on MSVC");
    Assert.equal(Executable.Inputs.includes(SkippedImportLibrary), false);
    Assert.equal(Executable.Inputs.includes(UnrelatedImportLibrary), false);
    Assert.ok(Executable.Command.includes("vendor-system.lib"));

    const BaseIndex = Actions.indexOf(BaseLink);
    const ExecutableIndex = Actions.indexOf(Executable);
    Assert.ok(BaseIndex >= 0 && BaseIndex < ExecutableIndex,
        "an explicit import-library input must form a producer edge in ActionGraph");

    const NinjaResult = await new NinjaBackend(Toolchain).Generate(Actions, BuildTarget, {
        OutputDir: Path.join(Root, "Ninja"),
    });
    const Ninja = await Fs.readFile(NinjaResult.NinjaPath, "utf-8");
    Assert.match(Ninja, /rule link\r?\n    command = \$Cmd\r?\n    description = \$Desc\r?\n    restat = 1/);
    const NormalizedImportLibrary = BaseImportLibrary.replace(/\\/g, "/").replace(/:/g, "$:");
    const ExecutableBuildLine = Ninja.split(/\r?\n/)
        .find((Line) => Line.startsWith("build ") && Line.includes("SyntheticEditorDebug.exe: link"));
    Assert.ok(ExecutableBuildLine);
    Assert.ok(ExecutableBuildLine.includes(NormalizedImportLibrary));
    Assert.doesNotMatch(ExecutableBuildLine, /Base\.dll/);
    Assert.doesNotMatch(ExecutableBuildLine, /Skipped\.lib|Unrelated\.lib/);
});

Test("MSVC DLL output contract pairs the DLL with its explicitly named import library", () => {
    const Dll = Path.resolve("Engine", "Binaries", "Win64", "Debug", "Core.dll");
    Assert.deepEqual(GetMSVCLinkOutputs(Dll, true), [
        Dll,
        Path.resolve("Engine", "Binaries", "Win64", "Debug", "Core.lib"),
    ]);
    Assert.deepEqual(GetMSVCLinkOutputs(Dll, false), [Dll]);
});

Test("Asset remains a Core-only runtime module with its own public ABI macro", () => {
    const Module = new AssetBuild();
    const Configuration = CreateModuleConfiguration();
    Module.Configure({
        Platform: Platform.Win64,
        Optimization: Optimization.Debug,
        TargetType: TargetType.Editor,
    }, Configuration);

    Assert.equal(Module.Name, "Asset");
    Assert.equal(APIMacroName(Module.Name), "ASSET_API");
    Assert.deepEqual(Configuration.PublicDependencies, ["Core"]);
    Assert.deepEqual(Configuration.PrivateDependencies, []);
    Assert.deepEqual(Configuration.CustomActions, []);
});

Test("World render assets preserve one-way module dependencies", () => {
    const Target: Target = {
        Platform: Platform.Win64,
        Optimization: Optimization.Debug,
        TargetType: TargetType.Editor,
    };
    const WorldConfiguration = CreateModuleConfiguration();
    new WorldBuild().Configure(Target, WorldConfiguration);
    Assert.deepEqual(WorldConfiguration.PublicDependencies,
        ["Core", "Asset", "Reflection"]);
    Assert.deepEqual(WorldConfiguration.PrivateDependencies, []);

    const RendererConfiguration = CreateModuleConfiguration();
    new RendererBuild().Configure(Target, RendererConfiguration);
    Assert.deepEqual(RendererConfiguration.PublicDependencies,
        ["Core", "Asset", "RHI", "RFG"]);
    Assert.deepEqual(RendererConfiguration.PrivateDependencies, []);

    const WorldRendererConfiguration = CreateModuleConfiguration();
    const WorldRendererModule = new WorldRendererBuild();
    WorldRendererModule.Configure(Target, WorldRendererConfiguration);
    Assert.equal(WorldRendererModule.Name, "WorldRenderer");
    Assert.equal(APIMacroName(WorldRendererModule.Name), "WORLDRENDERER_API");
    Assert.deepEqual(WorldRendererConfiguration.PublicDependencies,
        ["Core", "Asset", "Renderer", "World"]);
    Assert.deepEqual(WorldRendererConfiguration.PrivateDependencies, []);

    const DemoConfiguration = CreateModuleConfiguration();
    new DemoApplicationBuild().Configure(Target, DemoConfiguration);
    Assert.equal(DemoConfiguration.PublicDependencies.includes("Asset"), true);
    Assert.equal(DemoConfiguration.PublicDependencies.includes("World"), true);
    Assert.equal(DemoConfiguration.PublicDependencies.includes("Renderer"), true);
    Assert.equal(DemoConfiguration.PublicDependencies.includes("WorldRenderer"), true);
});

Test("World source tree has no RHI or Renderer surface", async () => {
    const WorldRoot = Path.resolve(
        import.meta.dirname, "..", "..", "Source", "Runtime", "World");
    const Pending = [WorldRoot];
    const Sources: string[] = [];
    while (Pending.length > 0) {
        const Directory = Pending.pop()!;
        for (const Entry of await Fs.readdir(Directory, { withFileTypes: true })) {
            const EntryPath = Path.join(Directory, Entry.name);
            if (Entry.isDirectory()) Pending.push(EntryPath);
            else if (/\.(?:h|hpp|cpp|cc|cxx)$/.test(Entry.name)) Sources.push(EntryPath);
        }
    }
    Assert.ok(Sources.length > 0);
    for (const SourcePath of Sources) {
        const Source = await Fs.readFile(SourcePath, "utf-8");
        Assert.doesNotMatch(Source, /#\s*include\s*[<"](?:RHI|Renderer)\//,
            SourcePath);
        Assert.doesNotMatch(Source, /\b(?:FRHI|FRender|ERender)[A-Za-z0-9_]*/,
            SourcePath);
    }
    await Assert.rejects(Fs.access(Path.join(
        WorldRoot, "Public", "World", "WorldRenderSceneExtractor.h")));
});

Test("Demo WorldRenderer bridge preserves frame and asset lifetime order", async () => {
    const DemoSourcePath = Path.resolve(
        import.meta.dirname, "..", "..", "Source", "Runtime",
        "DemoApplication", "Private", "DemoApplication.cpp");
    const Source = await Fs.readFile(DemoSourcePath, "utf-8");
    const TransitionalBridgePath = Path.join(
        Path.dirname(DemoSourcePath), "DemoWorldRenderSceneExtraction.h");
    await Assert.rejects(Fs.access(TransitionalBridgePath));

    const RenderBegin = Source.indexOf("EApplicationTickResult Render()");
    const RenderEnd = Source.indexOf("static bool StartupThunk", RenderBegin);
    const Render = Source.slice(RenderBegin, RenderEnd);
    const Snapshot = Render.indexOf("FWorldRenderSnapshot RenderSnapshot");
    const BuildSnapshot = Render.indexOf("WorldRenderer.BuildSnapshot(");
    const ScheduledFrame = Render.indexOf("ScheduledFrame.GetFrameIndex()", BuildSnapshot);
    const ConsumeSnapshot = Render.indexOf(
        "&RenderSnapshot.GetRenderScene()", BuildSnapshot);
    Assert.ok(RenderBegin >= 0 && RenderEnd > RenderBegin && Snapshot >= 0 &&
        BuildSnapshot > Snapshot && ScheduledFrame > BuildSnapshot &&
        ConsumeSnapshot > ScheduledFrame,
    "Demo must build and synchronously consume one stack-owned snapshot per frame callback");

    const ResizeBegin = Source.indexOf("EApplicationTickResult UpdateRenderResources()");
    const ResizeEnd = Source.indexOf("EApplicationTickResult Update(", ResizeBegin);
    const Resize = Source.slice(ResizeBegin, ResizeEnd);
    const DrainFrames = Resize.indexOf("FrameScheduler.WaitForAllFrames()");
    const CreateCandidate = Resize.indexOf("TryCreateDemoRenderResources(");
    const Replace = Resize.indexOf("ReplaceBasePassMaterial(Candidate.BasePassPipeline)");
    const SwapCandidate = Resize.indexOf("RenderResources = Candidate");
    const DestroyPrevious = Resize.indexOf("DestroyDemoRenderResources(Previous)");
    Assert.ok(ResizeBegin >= 0 && ResizeEnd > ResizeBegin && DrainFrames >= 0 &&
        CreateCandidate > DrainFrames && Replace > CreateCandidate &&
        SwapCandidate > Replace && DestroyPrevious > SwapCandidate,
    "resize must drain frames, complete a candidate, publish, swap, then destroy old resources");
    Assert.doesNotMatch(Render, /WaitForAllFrames\s*\(/,
        "normal frame rendering must not globally drain frames");

    const ShutdownBegin = Source.indexOf("void Shutdown() noexcept");
    const ShutdownEnd = Source.indexOf("EApplicationTickResult UpdateRenderResources()", ShutdownBegin);
    const Shutdown = Source.slice(ShutdownBegin, ShutdownEnd);
    const ResetWorld = Shutdown.indexOf("World.Reset()");
    const UnregisterAssets = Shutdown.indexOf("UnregisterRenderAssets()");
    const StopFrames = Shutdown.indexOf("FrameScheduler.Shutdown()");
    const DestroyMesh = Shutdown.indexOf("DestroyResource(VertexBuffer)");
    const DestroyIndices = Shutdown.indexOf("DestroyResource(IndexBuffer)");
    Assert.ok(ShutdownBegin >= 0 && ShutdownEnd > ShutdownBegin &&
        ResetWorld >= 0 && UnregisterAssets > ResetWorld &&
        StopFrames > UnregisterAssets && DestroyMesh > StopFrames &&
        DestroyIndices > DestroyMesh,
    "shutdown must clear World, unregister borrowed assets, stop frames, then destroy GPU resources");

    Assert.match(Source, /MeshAsset\.IndexBuffer\s*=\s*IndexBuffer/);
    Assert.match(Source, /MeshAsset\.IndexFormat\s*=\s*EPixelFormat::R16_UINT/);
    Assert.match(Source, /BootstrapCameraToken/);
    Assert.match(Source, /RenderResources\.SceneColorTexture->GetDesc\(\)\.Width/);
});

Test("Vulkan buffer creation honors the usage bitmask used by indexed meshes", async () => {
    const SourcePath = Path.resolve(
        import.meta.dirname, "..", "..", "Source", "Runtime", "RHI",
        "Private", "Vulkan", "VulkanBuffer.cpp");
    const Source = await Fs.readFile(SourcePath, "utf-8");
    Assert.match(Source, /ToVkBufferUsage\(InDesc\.UsageFlag\)/,
        "buffer creation must translate the UsageFlag bitmask");
    Assert.doesNotMatch(Source, /ToVkBufferUsage\(InDesc\.Usage\)/,
        "legacy single Usage must not silently turn IndexBuffer into VertexBuffer");
});
