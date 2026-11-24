import { spawnSync } from "node:child_process";
import { randomUUID } from "node:crypto";
import { lstat, mkdir, realpath, rm, writeFile } from "node:fs/promises";
import * as Path from "node:path";
import { fileURLToPath } from "node:url";
import {
    AssetSelector,
    CookedRelativePath,
    GltfImportError,
    ReadGltfJson,
    ReadGltfManifest,
    Uuid,
    VerifyReimportIdentity,
    type ImportManifest,
    type ImportReport,
    type PreviewAsset,
} from "./GltfImportManifest.ts";

type Transform = { translation: number[]; rotation: number[]; scale: number[] };
type SourcePrimitive = { mesh: number; primitive: number; material: number | null };
type SourceNode = {
    index: number;
    parent: number | null;
    name: string;
    transform: Transform;
    primitives: SourcePrimitive[];
};
type SourceMaterial = { index: number | null; name: string; image: number | null };
type SourceImage = { index: number; name: string };
type Inspection = ImportReport & {
    sourceDocumentSha256: string;
    nodes: SourceNode[];
    materials: SourceMaterial[];
    images: SourceImage[];
};
type SceneEntity = {
    id: string;
    parent?: string;
    transform?: Transform;
    mesh?: { mesh: string; material: string; debugName: string; sortKey: string };
};
type SceneDocument = { version: 1 | 2; entities: SceneEntity[] };

export type ImportGltfOptions = {
    assetImport: string;
    manifest: string;
    output?: string;
    sceneTool?: string;
    authorScene?: string;
};

const MaxWorkerOutput = 64 * 1024 * 1024;
const MaxSceneBytes = 64 * 1024 * 1024;
const ManifestSnapshot = "Import.json";

function IsMissing(reason: unknown): boolean {
    return (reason as NodeJS.ErrnoException | undefined)?.code === "ENOENT";
}

function RunTool(executable: string, args: string[], input?: string) {
    const result = spawnSync(Path.resolve(executable), args, {
        encoding: "utf8", input, maxBuffer: MaxWorkerOutput, windowsHide: true,
    });
    if (result.error) {
        throw new GltfImportError("tool_process_error", result.error.message, { file: executable });
    }
    return result;
}

function RunJsonTool(executable: string, args: string[], input?: string): ImportReport & Record<string, unknown> {
    const result = RunTool(executable, args, input);
    let report: ImportReport & Record<string, unknown>;
    try {
        report = JSON.parse(result.stdout);
        if (!report || typeof report.ok !== "boolean" || typeof report.code !== "string") throw new Error();
    } catch {
        throw new GltfImportError("tool_protocol_error", `Invalid report from ${Path.basename(executable)} (${result.status ?? result.signal}). ${result.stderr.trim()}`, { file: executable });
    }
    if (!report.ok || result.status !== 0) {
        throw new GltfImportError(report.ok ? "tool_process_error" : report.code,
            report.message ?? `${Path.basename(executable)} failed.`, report);
    }
    return report;
}

function InspectSource(tool: string, root: string, manifest: ImportManifest): Inspection {
    const report = RunJsonTool(tool, ["gltf", "inspect", root, manifest.source, String(manifest.scene)]) as unknown as Inspection;
    if (!/^[0-9a-f]{64}$/.test(report.sourceDocumentSha256) || !Array.isArray(report.nodes) ||
        !Array.isArray(report.materials) || !Array.isArray(report.images)) {
        throw new GltfImportError("tool_protocol_error", "AssetImport returned an invalid glTF inspection.", { file: tool });
    }
    return report;
}

function CheckSceneReferences(seed: SceneDocument, manifest: ImportManifest, file: string): void {
    if ((seed.version !== 1 && seed.version !== 2) || !Array.isArray(seed.entities)) {
        throw new GltfImportError("invalid_preview_scene", "Expected a Scene v1 or v2 document.", { file });
    }
    const kinds = new Map([...manifest.assets, ...(manifest.preview?.assets ?? [])].map(asset => [asset.id, asset.kind]));
    for (const entity of seed.entities) {
        if (!entity || typeof entity.id !== "string" || (entity.mesh &&
            (kinds.get(entity.mesh.mesh) !== "mesh" || kinds.get(entity.mesh.material) !== "material"))) {
            throw new GltfImportError("invalid_preview_reference", "Preview references a mesh or material outside the candidate.", { file, key: entity?.id });
        }
    }
}

function Required<T>(value: T | undefined, selector: string): T {
    if (value === undefined) {
        throw new GltfImportError("missing_binding", "No binding exists for this glTF source element.", { selector });
    }
    return value;
}

function Take<T>(map: Map<string, T>, selector: string): T {
    const value = Required(map.get(selector), selector);
    map.delete(selector);
    return value;
}

async function BuildRecipe(root: string, manifest: ImportManifest, inspection: Inspection): Promise<string> {
    if (inspection.sourceDocumentSha256 !== manifest.sourceDocumentSha256) {
        throw new GltfImportError("source_document_changed", "Inspect the new source, verify the selectors, and update sourceDocumentSha256.", {
            file: Path.join(root, manifest.source), key: "sourceDocumentSha256",
        });
    }

    let scene: SceneDocument = { version: 1, entities: [] };
    if (manifest.preview) {
        const file = await ResolveLocalFile(root, manifest.preview.scene);
        scene = await ReadGltfJson(file, MaxSceneBytes) as SceneDocument;
        if (!scene || typeof scene !== "object") throw new GltfImportError("invalid_preview_scene", "Expected a scene object.", { file });
        CheckSceneReferences(scene, manifest, file);
    }

    const assetsBySelector = new Map(manifest.assets.map(asset => [AssetSelector(asset), asset]));
    const nodesByIndex = new Map(manifest.nodes.map(node => [node.selector.node, node]));
    const draws = new Map(manifest.draws.map(draw => [`${draw.selector.node}/${draw.selector.mesh}`, draw]));
    const usedAssets = new Set<string>();
    const sourceMaterials = new Map(inspection.materials.map(material => [material.index, material]));
    const usedNodes = new Set<number>();
    const LookupAsset = (selector: string) => {
        usedAssets.add(selector);
        return Required(assetsBySelector.get(selector), selector);
    };

    for (const node of inspection.nodes) {
        const binding = Required(nodesByIndex.get(node.index), `node/${node.index}`);
        usedNodes.add(node.index);
        const parent = node.parent === null ? manifest.preview?.parent :
            Required(nodesByIndex.get(node.parent), `node/${node.parent}`).id;
        scene.entities.push({ id: binding.id, ...(parent && { parent }), transform: node.transform });
        for (const primitive of node.primitives) {
            const mesh = LookupAsset(`mesh/${primitive.mesh}/${primitive.primitive}`);
            const material = LookupAsset(`material/${primitive.material ?? "default"}`);
            const draw = Take(draws, `${binding.key}/${mesh.key}`);
            const sourceMaterial = Required(sourceMaterials.get(primitive.material), AssetSelector(material));
            if (sourceMaterial.image !== null) LookupAsset(`image/${sourceMaterial.image}`);
            scene.entities.push({
                id: draw.id,
                parent: binding.id,
                transform: { translation: [0, 0, 0], rotation: [0, 0, 0, 1], scale: [1, 1, 1] },
                mesh: { mesh: mesh.id, material: material.id, debugName: node.name || binding.key, sortKey: "0" },
            });
        }
    }

    const unused = manifest.assets.find(asset => !usedAssets.has(AssetSelector(asset))) ??
        manifest.nodes.find(node => !usedNodes.has(node.selector.node)) ?? draws.values().next().value;
    if (unused) {
        throw new GltfImportError("unused_binding", "Binding refers to an element outside the selected scene.", { key: unused.key });
    }

    const lines = [inspection.sourceDocumentSha256];
    for (const asset of manifest.assets) {
        if (asset.kind === "mesh") {
            lines.push([asset.kind, asset.selector.mesh, asset.selector.primitive, asset.id, asset.path].join("\t"));
        } else if (asset.kind === "material") {
            const material = Required(sourceMaterials.get(asset.selector.material), AssetSelector(asset));
            const image = material.image === null ? "-" : LookupAsset(`image/${material.image}`).id;
            lines.push([asset.kind, asset.selector.material ?? -1, image, asset.id, asset.path].join("\t"));
        } else {
            lines.push([asset.kind, asset.selector.image, "-", asset.id, asset.path].join("\t"));
        }
    }
    lines.push(`scene\t${JSON.stringify(scene)}`, "");
    return lines.join("\n");
}

async function ResolveLocalFile(root: string, name: string): Promise<string> {
    const requested = Path.join(root, name);
    const stat = await lstat(requested);
    if (!stat.isFile() || await realpath(requested) !== requested) {
        throw new GltfImportError("unsafe_preview_file", "Expected an ordinary preview file in the input root.", { file: requested });
    }
    return requested;
}

async function ReadGeneration(output: string): Promise<string | undefined> {
    const file = Path.join(output, "current.json");
    try {
        if (!(await lstat(file)).isFile()) {
            throw new GltfImportError("invalid_current", "Expected an ordinary current.json file.", { file });
        }
        const result = await ReadGltfJson(file, 1024) as { generation?: string } | null;
        if (!result || typeof result.generation !== "string" || !Uuid.test(result.generation)) {
            throw new GltfImportError("invalid_current", "Invalid current content generation.", { file });
        }
        return result.generation;
    } catch (reason) {
        if (IsMissing(reason)) return undefined;
        throw reason;
    }
}

async function ExistingOutput(output: string): Promise<string> {
    try {
        if (!(await lstat(output)).isDirectory()) {
            throw new GltfImportError("unsafe_output", "Expected an ordinary output directory.", { file: output });
        }
        return await realpath(output);
    } catch (reason) {
        if (IsMissing(reason)) return output;
        throw reason;
    }
}

async function PrepareOutput(output: string, generation: string): Promise<string> {
    await mkdir(output, { recursive: true });
    const root = await ExistingOutput(output);
    const parent = Path.join(root, "generations");
    await mkdir(parent, { recursive: true });
    if (!(await lstat(parent)).isDirectory() || await realpath(parent) !== parent) {
        throw new GltfImportError("unsafe_output", "The generations directory must not redirect.", { file: parent });
    }
    const candidate = Path.join(parent, generation);
    await mkdir(candidate);
    return candidate;
}

async function StagePreviewAsset(tool: string, root: string, asset: PreviewAsset, mount: string, candidate?: string): Promise<void> {
    const source = await ResolveLocalFile(root, asset.source);
    const args = [asset.kind, "--source", source, "--id", asset.id, "--path", asset.path];
    if (candidate) {
        const output = Path.join(candidate, "Assets", CookedRelativePath(asset.path, mount));
        await mkdir(Path.dirname(output), { recursive: true });
        args.push("--output", output);
    } else {
        args.push("--check");
    }
    const result = RunTool(tool, args);
    if (result.status !== 0) {
        throw new GltfImportError("preview_asset_error", result.stderr.trim() || result.stdout.trim() || "Preview asset validation failed.", { file: source });
    }
}

export async function RunGltfImport(mode: "inspect" | "check" | "import", options: ImportGltfOptions): Promise<ImportReport> {
    const manifestFile = Path.resolve(options.manifest);
    const manifest = await ReadGltfManifest(manifestFile);
    const sourceRoot = await realpath(Path.dirname(manifestFile));
    const inspection = InspectSource(options.assetImport, sourceRoot, manifest);
    if (mode === "inspect") return inspection;

    const output = options.output && await ExistingOutput(Path.resolve(options.output));
    const previous = output ? await ReadGeneration(output) : undefined;
    if (previous && output) {
        const prior = Path.join(output, "generations", previous);
        if (await realpath(prior) !== prior) {
            throw new GltfImportError("unsafe_output", "Published content must not redirect.", { file: prior });
        }
        VerifyReimportIdentity(manifest, await ReadGltfManifest(Path.join(prior, ManifestSnapshot)));
    }
    const recipe = await BuildRecipe(sourceRoot, manifest, inspection);
    const sourceArgs = [sourceRoot, manifest.source, String(manifest.scene), manifest.mount];
    if (mode === "check") {
        for (const asset of manifest.preview?.assets ?? []) {
            await StagePreviewAsset(options.assetImport, sourceRoot, asset, manifest.mount);
        }
        return RunJsonTool(options.assetImport, ["gltf", "check", ...sourceArgs], recipe);
    }

    if (!output || !options.sceneTool) {
        throw new GltfImportError("invalid_arguments", "Import requires --output and --scene-tool.");
    }
    const generation = randomUUID();
    const candidate = await PrepareOutput(output, generation);
    let publishing = false;
    try {
        const assetsRoot = Path.join(candidate, "Assets");
        await mkdir(assetsRoot);
        for (const asset of manifest.assets) {
            await mkdir(Path.dirname(Path.join(assetsRoot, CookedRelativePath(asset.path, manifest.mount))), { recursive: true });
        }
        const cooked = RunJsonTool(options.assetImport, ["gltf", "cook", ...sourceArgs, candidate], recipe);
        for (const asset of manifest.preview?.assets ?? []) {
            await StagePreviewAsset(options.assetImport, sourceRoot, asset, manifest.mount, candidate);
        }
        const check = (file: string) => RunJsonTool(options.sceneTool!, ["check", "--json", file, manifest.mount, assetsRoot]);
        check(Path.join(candidate, "Scene.json"));
        if (options.authorScene) check(Path.resolve(options.authorScene));
        await writeFile(Path.join(candidate, ManifestSnapshot), `${JSON.stringify(manifest, null, 2)}\n`, { flag: "wx" });

        // If the publisher exits after replacement, the generation may already be live.
        publishing = true;
        RunJsonTool(options.assetImport, ["gltf", "publish", output, previous ?? "-", generation]);
        return { ...cooked, generation, contentRoot: candidate };
    } finally {
        if (!publishing) await rm(candidate, { recursive: true, force: true });
    }
}

function ParseArguments(args: string[]): { mode: "inspect" | "check" | "import"; options: ImportGltfOptions } {
    const [mode, ...rest] = args;
    if (mode !== "inspect" && mode !== "check" && mode !== "import") {
        throw new GltfImportError("invalid_arguments", "Usage: ImportGltf inspect|check|import --asset-import EXE --manifest FILE [--output DIR --scene-tool EXE --author-scene FILE] [--json]");
    }
    const flags = new Map<string, string>();
    for (let index = 0; index < rest.length; ++index) {
        const flag = rest[index]!;
        if (flag === "--json") continue;
        const argument = rest[++index];
        if (!argument || argument.startsWith("--") || flags.has(flag) ||
            !["--asset-import", "--manifest", "--output", "--scene-tool", "--author-scene"].includes(flag)) {
            throw new GltfImportError("invalid_arguments", `Invalid or duplicate option ${flag}.`);
        }
        flags.set(flag, argument);
    }
    const assetImport = flags.get("--asset-import");
    const manifest = flags.get("--manifest");
    if (!assetImport || !manifest) throw new GltfImportError("invalid_arguments", "Expected --asset-import and --manifest.");
    return { mode, options: { assetImport, manifest, output: flags.get("--output"), sceneTool: flags.get("--scene-tool"), authorScene: flags.get("--author-scene") } };
}

if (process.argv[1] && Path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    try {
        const { mode, options } = ParseArguments(process.argv.slice(2));
        console.log(JSON.stringify(await RunGltfImport(mode, options)));
    } catch (reason) {
        const report = reason instanceof GltfImportError ? reason.report :
            { ok: false, code: "import_error", message: String((reason as Error).message), file: (reason as NodeJS.ErrnoException).path };
        console.log(JSON.stringify(report));
        process.exitCode = 1;
    }
}
