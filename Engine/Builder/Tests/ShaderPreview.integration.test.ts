import * as Assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import * as Fs from "node:fs/promises";
import * as Path from "node:path";
import Test, { type TestContext } from "node:test";
import { fileURLToPath, pathToFileURL } from "node:url";
import { CaptureShaderInputs, ShaderInputsMatch, WriteShaderSnapshot } from "../Source/Content/VulkanShaderPreviewSources.ts";
import { BasePassShadowPixelShader, FindVulkanShaderCompiler, FindVulkanShaderTool } from "../Source/Content/VulkanShaders.ts";

const Repository = fileURLToPath(new URL("../../../", import.meta.url));
const OriginalShaders = Path.join(Repository, "Engine", "Content", "Shaders");
const OfflineCompiler = fileURLToPath(new URL("../Source/Content/CompileVulkanShader.ts", import.meta.url));
const Worker = fileURLToPath(new URL("../Source/Content/PreviewVulkanShader.ts", import.meta.url));
const SdkMissing = !FindVulkanShaderCompiler() || ["spirv-val", "spirv-cross", "spirv-dis"].some((Tool) => !FindVulkanShaderTool(Tool));
const EntryFile = BasePassShadowPixelShader.Name + ".lsf";

function Run(Script: string, Arguments: string[], Env = process.env) {
    const Result = spawnSync(process.execPath, ["--no-warnings", "--experimental-strip-types", Script, ...Arguments], {
        env: Env, encoding: "utf8", windowsHide: true, timeout: 10_000,
    });
    Assert.ifError(Result.error);
    return Result;
}

async function Fixture(Context: TestContext) {
    const Intermediate = Path.join(Repository, "Engine", "Intermediate");
    await Fs.mkdir(Intermediate, { recursive: true });
    const Root = await Fs.mkdtemp(Path.join(Intermediate, "ShaderPreview-"));
    Context.after(() => Fs.rm(Root, { recursive: true, force: true }));
    const Checkout = Path.join(Root, "Source Checkout");
    const Sources = Path.join(Checkout, "Engine", "Content", "Shaders");
    const Indirect = Path.join(Sources, "Includes", "PreviewTint.lsi");
    await Fs.mkdir(Path.dirname(Indirect), { recursive: true });
    for (const File of [EntryFile, "BasePassPS.lsf", "BasePassConstants.lsi"]) {
        await Fs.copyFile(Path.join(OriginalShaders, File), Path.join(Sources, File));
    }
    await Fs.appendFile(Path.join(Sources, "BasePassConstants.lsi"), '\n#include "Includes/PreviewTint.lsi"\n');
    await Fs.writeFile(Indirect, "#define PREVIEW_TINT float3(0.3, 0.8, 0.4)\n");
    return { Root, Checkout, Sources, Indirect };
}

async function AfterCompile(Root: string, Name: string, Action: string): Promise<string> {
    const Wrapper = Path.join(Root, Name + ".mjs");
    await Fs.writeFile(Wrapper, [
        'import ChildProcess from "node:child_process";',
        'import * as Fs from "node:fs";',
        'import * as Path from "node:path";',
        'import { syncBuiltinESMExports } from "node:module";',
        'const Spawn = ChildProcess.spawnSync;',
        'ChildProcess.spawnSync = (Tool, Args, Options) => {',
        '    const Result = Spawn(Tool, Args, Options);',
        '    if (Result.status === 0 && /^glslangValidator(?:\\.exe)?$/.test(Path.basename(Tool))) {',
        '        ' + Action,
        '    }',
        '    return Result;',
        '};',
        'syncBuiltinESMExports();',
        'await import(' + JSON.stringify(pathToFileURL(Worker).href) + ');',
    ].join("\n"));
    return Wrapper;
}

Test("preview captures transitive sources, preserves their relative paths and detects edits", async (Context) => {
    const { Root, Sources, Indirect } = await Fixture(Context);
    const Inputs = CaptureShaderInputs(Sources);
    const Snapshot = Path.join(Root, "Snapshot");
    const SourceMap = WriteShaderSnapshot(Snapshot, Inputs);
    const Copied = Path.join(Snapshot, "Includes", "PreviewTint.lsi");
    Assert.equal(SourceMap.get(Copied), Indirect);
    Assert.equal(Inputs.length, 4);
    Assert.equal(await Fs.readFile(Copied, "utf8"), await Fs.readFile(Indirect, "utf8"));
    Assert.equal(ShaderInputsMatch(Sources, Inputs), true);

    await Fs.writeFile(Indirect, "#define PREVIEW_TINT float3(0.8, 0.3, 0.4)\n");
    Assert.equal(ShaderInputsMatch(Sources, Inputs), false);
    Assert.match(await Fs.readFile(Copied, "utf8"), /0\.3, 0\.8/);
    await Fs.writeFile(Indirect, '#include "../../../unexpected.lsi"\n');
    Assert.throws(() => CaptureShaderInputs(Sources), /escapes its source directory/);
});

Test("preview accepts only validated ABI-compatible candidates and keeps the offline package", {
    skip: SdkMissing ? "requires glslangValidator, spirv-val, spirv-cross and spirv-dis from the Vulkan SDK" : false,
}, async (Context) => {
    const { Root, Checkout, Sources, Indirect } = await Fixture(Context);
    const Packaged = Path.join(Root, "Packaged", "Shaders", "Vulkan", BasePassShadowPixelShader.Name + ".spv");
    const Offline = Run(OfflineCompiler, [Path.join(OriginalShaders, EntryFile),
        BasePassShadowPixelShader.Stage, BasePassShadowPixelShader.EntryPoint, Packaged]);
    Assert.equal(Offline.status, 0, Offline.stdout + Offline.stderr);
    const Baseline = await Fs.readFile(Packaged);
    const PixelSource = Path.join(Sources, "BasePassPS.lsf");
    const Original = await Fs.readFile(PixelSource, "utf8");
    const Tint = await Fs.readFile(Indirect, "utf8");
    let NextRequest = 0;

    interface RequestFixture {
        Edit?: string;
        Include?: string;
        Startup?: Buffer;
        Env?: NodeJS.ProcessEnv;
        Entry?: string;
    }

    async function Preview(Code: string, Options: RequestFixture = {}) {
        const { Edit = Original, Include = Tint, Startup = Baseline, Env = process.env, Entry = Worker } = Options;
        await Fs.writeFile(PixelSource, Edit);
        await Fs.writeFile(Indirect, Include);
        const Id = String(++NextRequest);
        const Request = Path.join(Root, "Request " + Id);
        await Fs.mkdir(Request);
        await Fs.writeFile(Path.join(Request, "baseline.spv"), Startup);
        const Result = Run(Entry, [Checkout, Request, Id], Env);
        Assert.equal(Result.status, Code === "ok" ? 0 : 1, Result.stdout + Result.stderr);
        Assert.equal(await Fs.readFile(Path.join(Request, "result.txt"), "utf8"), Id + "\n" + Code + "\n");
        const Json = await Fs.readFile(Path.join(Request, "diagnostics.json"), "utf8");
        Assert.ok(Buffer.byteLength(Json) <= 64 * 1024);
        const Diagnostics = JSON.parse(Json);
        Assert.equal(Diagnostics.requestId, Id);
        Assert.equal(Diagnostics.code, Code);
        Assert.deepEqual((await Fs.readdir(Request)).sort(), [
            "baseline.spv", ...(Code === "ok" ? ["candidate.spv"] : []), "diagnostics.json", "result.txt",
        ]);
        Assert.deepEqual(await Fs.readFile(Packaged), Baseline);
        return { Request, Diagnostics };
    }

    await Context.test("keeps the startup ABI when the used indirect include changes the final color", async () => {
        const Tinted = Original.replace("saturate(material * factor)", "saturate(material * factor * PREVIEW_TINT)");
        const { Request } = await Preview("ok", { Edit: Tinted, Include: "#define PREVIEW_TINT float3(0.8, 0.3, 0.4)\n" });
        Assert.notDeepEqual(await Fs.readFile(Path.join(Request, "candidate.spv")), Baseline);
    });
    await Context.test("ignores debug names", async () => {
        await Preview("ok", { Edit: Original.replaceAll("BaseColorTexture", "PreviewTexture").replaceAll("DirectionAndEnabled", "DirectionFlags") });
    });
    await Context.test("rejects a changed descriptor binding", async () => {
        await Preview("abi_incompatible", { Edit: Original.replace("register(t0, space0)", "register(t4, space0)") });
    });
    await Context.test("rejects a changed uniform member layout", async () => {
        await Preview("abi_incompatible", { Edit: Original.replace("float4 Radiance;", "float3 Radiance;") });
    });
    await Context.test("rejects a changed stage input location", async () => {
        await Preview("abi_incompatible", { Edit: Original.replace("vk::location(3)", "vk::location(4)") });
    });
    await Context.test("rejects changed interpolation decorations", async () => {
        await Preview("abi_incompatible", {
            Edit: Original.replace("float4 CameraClip : TEXCOORD1", "nointerpolation float4 CameraClip : TEXCOORD1"),
        });
    });
    await Context.test("rejects an added built-in output", async () => {
        const AddedDepth = Original.replace("float4 MainPS(PSInput input) : SV_Target0", "float4 ShadePS(PSInput input)") + [
            "",
            "struct PreviewOutput",
            "{",
            "    float4 Color : SV_Target0;",
            "    float Depth : SV_Depth;",
            "};",
            "PreviewOutput MainPS(PSInput input)",
            "{",
            "    PreviewOutput result;",
            "    result.Color = ShadePS(input);",
            "    result.Depth = saturate(input.CameraClip.z);",
            "    return result;",
            "}",
            "",
        ].join("\n");
        await Preview("abi_incompatible", { Edit: AddedDepth });
    });
    await Context.test("reports syntax diagnostics at the original indirect include and line", async () => {
        const { Diagnostics } = await Preview("compile_failed", { Include: "// Fixture error follows\ninvalid_shader_token ???\n" });
        Assert.ok(Diagnostics.diagnostics.some((Diagnostic: { file?: string; line?: number }) =>
            Diagnostic.file === Indirect && Diagnostic.line === 2), JSON.stringify(Diagnostics));
        Assert.ok(!Diagnostics.toolOutput.includes(".shader-preview-"), JSON.stringify(Diagnostics));
    });
    await Context.test("rejects a truncated startup module", async () => {
        await Preview("baseline_invalid", { Startup: Baseline.subarray(0, Baseline.length - 4) });
    });
    await Context.test("rejects a compiler output truncated before validation", async () => {
        const Entry = await AfterCompile(Root, "TruncateCandidate",
            "const Output = Args.at(-1); Fs.truncateSync(Output, Fs.statSync(Output).size - 4);");
        await Preview("invalid_spirv", { Entry });
    });
    await Context.test("rejects an indirect include edited during compilation", async () => {
        const Entry = await AfterCompile(Root, "ChangeLiveInclude",
            "Fs.appendFileSync(" + JSON.stringify(Indirect) + ", " + JSON.stringify("\n// saved during preview\n") + ");");
        await Preview("source_changed", { Entry });
    });
    await Context.test("reports a missing SDK independently of compilation", async () => {
        await Preview("tools_unavailable", { Env: { ...process.env, VULKAN_SDK: undefined, PATH: "" } });
    });
});
