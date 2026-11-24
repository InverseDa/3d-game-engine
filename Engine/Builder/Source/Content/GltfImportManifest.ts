import { open } from "node:fs/promises";

export type AssetKind = "mesh" | "material" | "texture";

type Identity = { key: string; id: string };
export type AssetBinding = Identity & { path: string } & (
    | { kind: "mesh"; selector: { mesh: number; primitive: number } }
    | { kind: "material"; selector: { material: number | null } }
    | { kind: "texture"; selector: { image: number } }
);
type NodeBinding = Identity & { selector: { node: number } };
type DrawBinding = Identity & { selector: { node: string; mesh: string } };
export type PreviewAsset = { kind: AssetKind; id: string; path: string; source: string };

export type ImportManifest = {
    version: 1;
    id: string;
    mount: string;
    source: string;
    scene: number;
    sourceDocumentSha256: string;
    assets: AssetBinding[];
    nodes: NodeBinding[];
    draws: DrawBinding[];
    preview?: { scene: string; assets: PreviewAsset[]; parent?: string };
};

export type ImportReport = {
    ok: boolean;
    code: string;
    message?: string;
    file?: string;
    key?: string;
    selector?: string;
    warnings?: unknown[];
    generation?: string;
    contentRoot?: string;
};

export class GltfImportError extends Error {
    readonly report: ImportReport;

    constructor(code: string, message: string, context: Partial<ImportReport> = {}) {
        super(message);
        this.report = { ...context, ok: false, code, message };
    }
}

export const Uuid = /^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/;
const NilUuid = "00000000-0000-0000-0000-000000000000";
const ReservedStem = /^(?:con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i;
const Key = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;
const MaxManifestBytes = 16 * 1024 * 1024;
const MaxAssets = 16_384;
const MaxEntities = 65_536;

export async function ReadGltfJson(file: string, limit: number): Promise<unknown> {
    const handle = await open(file, "r");
    try {
        const stat = await handle.stat();
        if (!stat.isFile() || stat.size > limit) {
            throw new GltfImportError("file_limit", `Expected a regular file of at most ${limit} bytes.`, { file });
        }
        const bytes = Buffer.alloc(stat.size + 1);
        let count = 0;
        while (count < bytes.length) {
            const part = await handle.read(bytes, count, bytes.length - count);
            if (part.bytesRead === 0) break;
            count += part.bytesRead;
        }
        if (count !== stat.size) {
            throw new GltfImportError("file_changed", "File size changed while reading.", { file });
        }
        try {
            return JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes.subarray(0, count)));
        } catch (reason) {
            throw new GltfImportError("invalid_json", (reason as Error).message, { file });
        }
    } finally {
        await handle.close();
    }
}

function Require(condition: unknown, message: string, key: string): asserts condition {
    if (!condition) throw new GltfImportError("invalid_manifest", message, { key });
}

function RecordAt(value: unknown, key: string, fields: string[]): Record<string, unknown> {
    Require(value !== null && typeof value === "object" && !Array.isArray(value), "Expected an object.", key);
    const record = value as Record<string, unknown>;
    Require(Object.keys(record).every(field => fields.includes(field)), "Unknown field in import manifest.", key);
    return record;
}

function ArrayAt(value: unknown, key: string, max: number): unknown[] {
    Require(Array.isArray(value) && value.length <= max, `Expected an array with at most ${max} entries.`, key);
    return value;
}

function TextAt(value: unknown, key: string, rule: RegExp): string {
    Require(typeof value === "string" && value.length <= 1024 && rule.test(value), "Invalid string value.", key);
    return value;
}

function IdAt(value: unknown, key: string): string {
    const id = TextAt(value, key, Uuid);
    Require(id !== NilUuid, "A persistent UUID must be non-nil.", key);
    return id;
}

function IndexAt(value: unknown, key: string): number {
    Require(typeof value === "number" && Number.isSafeInteger(value) && value >= 0 && value <= 0xffff_ffff,
        "Expected a non-negative source index.", key);
    return value;
}

export function CookedRelativePath(path: string, mount: string): string {
    const segments = path.split("/");
    Require(segments.length > 1 && segments[0] === mount && segments.every(segment =>
        /^[A-Za-z0-9_-][A-Za-z0-9_.-]*$/.test(segment) && !segment.endsWith(".") && !ReservedStem.test(segment)),
    "Expected a canonical portable path under the selected mount.", path);
    return `${segments.slice(1).join("/")}.leasset`;
}

function KindAt(value: unknown, key: string): AssetKind {
    Require(value === "mesh" || value === "material" || value === "texture", "Unknown asset type.", key);
    return value;
}

function IdentityAt(record: Record<string, unknown>): Identity {
    const key = TextAt(record.key, "key", Key);
    return { key, id: IdAt(record.id, key) };
}

function ByKey<T extends { key: string }>(left: T, right: T): number {
    return left.key < right.key ? -1 : Number(left.key !== right.key);
}

function Claim<T>(set: Set<T>, value: T, key: string, description: string): void {
    Require(!set.has(value), `Duplicate ${description}: ${String(value)}.`, key);
    set.add(value);
}

export function AssetSelector(binding: AssetBinding): string {
    switch (binding.kind) {
        case "mesh": return `mesh/${binding.selector.mesh}/${binding.selector.primitive}`;
        case "material": return `material/${binding.selector.material ?? "default"}`;
        case "texture": return `image/${binding.selector.image}`;
    }
}

export function ParseGltfManifest(value: unknown): ImportManifest {
    const top = RecordAt(value, "manifest", ["version", "id", "mount", "source", "scene", "sourceDocumentSha256", "assets", "nodes", "draws", "preview"]);
    Require(top.version === 1, "Expected manifest version 1.", "version");
    const mount = TextAt(top.mount, "mount", /^[A-Za-z0-9_-]+$/);
    const gltfSource = TextAt(top.source, "source", /^(?:[A-Za-z0-9_-]+\/)*[A-Za-z0-9_.-]+\.gltf$/);
    const keys = new Set<string>();
    const assetIds = new Set<string>();
    const assetPaths = new Set<string>();
    const assetSelectors = new Set<string>();
    const entityIds = new Set<string>();
    const nodeSelectors = new Set<number>();
    const drawSelectors = new Set<string>();
    const assets = ArrayAt(top.assets, "assets", MaxAssets).map((raw): AssetBinding => {
        const entry = RecordAt(raw, "assets", ["key", "id", "kind", "path", "selector"]);
        const identity = IdentityAt(entry);
        const { key, id } = identity;
        const kind = KindAt(entry.kind, key);
        const path = TextAt(entry.path, key, /^\S+$/);
        CookedRelativePath(path, mount);
        Claim(keys, key, key, "logical key");
        Claim(assetIds, id, key, "asset ID");
        Claim(assetPaths, path.toLowerCase(), key, "asset path");
        let binding: AssetBinding;
        if (kind === "mesh") {
            const selector = RecordAt(entry.selector, key, ["mesh", "primitive"]);
            binding = { ...identity, kind, path, selector: { mesh: IndexAt(selector.mesh, key), primitive: IndexAt(selector.primitive, key) } };
        } else if (kind === "material") {
            const selector = RecordAt(entry.selector, key, ["material"]);
            binding = { ...identity, kind, path, selector: { material: selector.material === null ? null : IndexAt(selector.material, key) } };
        } else {
            const selector = RecordAt(entry.selector, key, ["image"]);
            binding = { ...identity, kind, path, selector: { image: IndexAt(selector.image, key) } };
        }
        Claim(assetSelectors, AssetSelector(binding), key, "asset selector");
        return binding;
    }).sort(ByKey);
    const nodes = ArrayAt(top.nodes, "nodes", MaxEntities).map((raw): NodeBinding => {
        const entry = RecordAt(raw, "nodes", ["key", "id", "selector"]);
        const identity = IdentityAt(entry);
        const { key, id } = identity;
        const selector = RecordAt(entry.selector, key, ["node"]);
        const node = IndexAt(selector.node, key);
        Claim(keys, key, key, "logical key");
        Claim(entityIds, id, key, "entity ID");
        Claim(nodeSelectors, node, key, "node selector");
        return { ...identity, selector: { node } };
    }).sort(ByKey);
    const draws = ArrayAt(top.draws, "draws", MaxEntities - nodes.length).map((raw): DrawBinding => {
        const entry = RecordAt(raw, "draws", ["key", "id", "selector"]);
        const identity = IdentityAt(entry);
        const { key, id } = identity;
        const selector = RecordAt(entry.selector, key, ["node", "mesh"]);
        const node = TextAt(selector.node, key, Key);
        const mesh = TextAt(selector.mesh, key, Key);
        Claim(keys, key, key, "logical key");
        Claim(entityIds, id, key, "entity ID");
        Claim(drawSelectors, `${node}/${mesh}`, key, "draw selector");
        return { ...identity, selector: { node, mesh } };
    }).sort(ByKey);

    let preview: ImportManifest["preview"];
    if (top.preview !== undefined) {
        const entry = RecordAt(top.preview, "preview", ["scene", "assets", "parent"]);
        const previewAssets = ArrayAt(entry.assets, "preview.assets", MaxAssets - assets.length).map((raw): PreviewAsset => {
            const asset = RecordAt(raw, "preview.assets", ["kind", "id", "path", "source"]);
            const path = TextAt(asset.path, "preview.assets", /^\S+$/);
            CookedRelativePath(path, mount);
            const id = IdAt(asset.id, path);
            Claim(assetIds, id, path, "asset ID");
            Claim(assetPaths, path.toLowerCase(), path, "asset path");
            return { kind: KindAt(asset.kind, path), id, path, source: TextAt(asset.source, path, /^[A-Za-z0-9_-][A-Za-z0-9_.-]*$/) };
        });
        preview = { scene: TextAt(entry.scene, "preview.scene", /^[A-Za-z0-9_-][A-Za-z0-9_.-]*\.json$/), assets: previewAssets };
        if (entry.parent !== undefined) preview.parent = IdAt(entry.parent, "preview.parent");
    }

    return {
        version: 1,
        id: IdAt(top.id, "id"), mount, source: gltfSource, scene: IndexAt(top.scene, "scene"),
        sourceDocumentSha256: TextAt(top.sourceDocumentSha256, "sourceDocumentSha256", /^[0-9a-f]{64}$/),
        assets, nodes, draws, ...(preview && { preview }),
    };
}

export async function ReadGltfManifest(file: string): Promise<ImportManifest> {
    try {
        return ParseGltfManifest(await ReadGltfJson(file, MaxManifestBytes));
    } catch (reason) {
        if (reason instanceof GltfImportError) reason.report.file ??= file;
        throw reason;
    }
}

export function VerifyReimportIdentity(current: ImportManifest, previous: ImportManifest): void {
    if (previous.id !== current.id || previous.mount !== current.mount) {
        throw new GltfImportError("identity_changed", "The output root belongs to another import set or mount.");
    }
    const groups = [
        [previous.assets, current.assets], [previous.nodes, current.nodes], [previous.draws, current.draws],
    ] as const;
    for (const [before, after] of groups) {
        const bindings = new Map<string, Identity | AssetBinding>(after.map(entry => [entry.key, entry]));
        for (const old of before) {
            const next = bindings.get(old.key);
            if (!next || old.id !== next.id || ("kind" in old &&
                (!("kind" in next) || old.kind !== next.kind || old.path !== next.path))) {
                throw new GltfImportError("identity_changed", "An existing key must keep its UUID, kind and virtual path.", { key: old.key });
            }
        }
    }
    const afterPreview = new Map(current.preview?.assets.map(asset => [asset.path, asset]));
    for (const old of previous.preview?.assets ?? []) {
        const next = afterPreview.get(old.path);
        if (!next || next.kind !== old.kind || next.id !== old.id) {
            throw new GltfImportError("identity_changed", "An existing preview asset must keep its UUID, kind and virtual path.", { key: old.path });
        }
    }
}
