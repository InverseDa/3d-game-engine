import * as Assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { createHash, randomUUID } from "node:crypto";
import * as Fs from "node:fs/promises";
import * as Path from "node:path";
import { deflateSync } from "node:zlib";
import Test from "node:test";
import { GltfImportError, type ImportManifest } from "../Source/Content/GltfImportManifest.ts";
import { RunGltfImport } from "../Source/Content/ImportGltf.ts";

const Root = Path.resolve(import.meta.dirname, "../../..");
const Builder = Path.join(Root, "Engine/Builder/Source/Cli/CommandLine.ts");
const TemporaryRoot = Path.join(Root, "Engine/Intermediate");
const Id = (n: number) => `eb15d26f-1a4c-43a2-b5eb-${String(n).padStart(12, "0")}`;
const Sha = (bytes: string | Buffer) => createHash("sha256").update(bytes).digest("hex");

function PngChunk(kind: string, bytes: Buffer): Buffer {
    const header = Buffer.alloc(8);
    header.writeUInt32BE(bytes.length);
    header.write(kind, 4);
    let crc = 0xffff_ffff;
    for (const byte of Buffer.concat([header.subarray(4), bytes])) {
        crc ^= byte;
        for (let bit = 0; bit < 8; ++bit) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb8_8320 : 0);
    }
    const tail = Buffer.alloc(4);
    tail.writeUInt32BE((crc ^ 0xffff_ffff) >>> 0);
    return Buffer.concat([header, bytes, tail]);
}

function TestPng(pixels: number[]): Buffer {
    const format = Buffer.alloc(13);
    format.writeUInt32BE(2);
    format.writeUInt32BE(2, 4);
    format[8] = 8;
    format[9] = 6;
    const rows = deflateSync(Buffer.from([0, ...pixels.slice(0, 8), 0, ...pixels.slice(8)]));
    return Buffer.concat([Buffer.from("89504e470d0a1a0a", "hex"), PngChunk("IHDR", format), PngChunk("IDAT", rows), PngChunk("IEND", Buffer.alloc(0))]);
}

async function PutFixture(directory: string) {
    const vertices = [
        [1, 0, -2, 0, 0, 1, 0, 0], [3, 0, 1, 0, 0, 1, 1, 0], [1, 2, -2, 0, 0, 1, 0, 1],
    ];
    const bin = Buffer.alloc(102);
    vertices.flat().forEach((value, index) => bin.writeFloatLE(value, index * 4));
    [0, 1, 2].forEach((value, index) => bin.writeUInt16LE(value, 96 + index * 2));
    const pixels = [235, 17, 6, 255, 16, 235, 6, 255, 6, 16, 235, 255, 235, 235, 6, 255];
    const source = {
        asset: { version: "2.0" },
        scene: 0,
        scenes: [{ nodes: [0] }],
        nodes: [
            { name: "assembly", rotation: [0, 0, Math.SQRT1_2, Math.SQRT1_2], children: [1, 2] },
            { name: "instance", translation: [1, 2, 3], mesh: 0 },
            { name: "instance", translation: [-1, 1, 0], mesh: 0 },
        ],
        meshes: [{ primitives: [
            { attributes: { POSITION: 0, NORMAL: 1, TEXCOORD_0: 2 }, indices: 3, material: 0 },
            { attributes: { POSITION: 0, NORMAL: 1, TEXCOORD_0: 2 }, indices: 3, material: 1 },
        ] }],
        materials: [{ pbrMetallicRoughness: { baseColorFactor: [0.75, 0.5, 0.25, 1], baseColorTexture: { index: 0 } }, normalTexture: { index: 0 } }, {}],
        textures: [{ source: 0 }], images: [{ uri: "FourCorners.png" }],
        buffers: [{ uri: "Model.bin", byteLength: bin.length }],
        bufferViews: [{ buffer: 0, byteLength: 96, byteStride: 32 }, { buffer: 0, byteOffset: 96, byteLength: 6 }],
        accessors: [
            { bufferView: 0, componentType: 5126, count: 3, type: "VEC3", min: [1, 0, -2], max: [3, 2, 1] },
            { bufferView: 0, byteOffset: 12, componentType: 5126, count: 3, type: "VEC3" },
            { bufferView: 0, byteOffset: 24, componentType: 5126, count: 3, type: "VEC2" },
            { bufferView: 1, componentType: 5123, count: 3, type: "SCALAR" },
        ],
    };
    const manifest: ImportManifest = {
        version: 1, id: Id(1), mount: "Game", source: "Model.gltf", scene: 0, sourceDocumentSha256: "0".repeat(64),
        assets: [
            { key: "mesh-tint", kind: "mesh", id: Id(10), path: "Game/Meshes/Tint", selector: { mesh: 0, primitive: 0 } },
            { key: "mesh-solid", kind: "mesh", id: Id(11), path: "Game/Meshes/Solid", selector: { mesh: 0, primitive: 1 } },
            { key: "mat-tint", kind: "material", id: Id(12), path: "Game/Materials/Tint", selector: { material: 0 } },
            { key: "mat-solid", kind: "material", id: Id(13), path: "Game/Materials/Solid", selector: { material: 1 } },
            { key: "tex-corners", kind: "texture", id: Id(14), path: "Game/Textures/Corners", selector: { image: 0 } },
        ],
        nodes: [
            { key: "node-root", id: Id(20), selector: { node: 0 } },
            { key: "node-left", id: Id(21), selector: { node: 1 } },
            { key: "node-right", id: Id(22), selector: { node: 2 } },
        ],
        draws: [
            { key: "draw-left-tint", id: Id(30), selector: { node: "node-left", mesh: "mesh-tint" } },
            { key: "draw-left-solid", id: Id(31), selector: { node: "node-left", mesh: "mesh-solid" } },
            { key: "draw-right-tint", id: Id(32), selector: { node: "node-right", mesh: "mesh-tint" } },
            { key: "draw-right-solid", id: Id(33), selector: { node: "node-right", mesh: "mesh-solid" } },
        ],
    };
    const sourceFile = Path.join(directory, "Model.gltf");
    const manifestFile = Path.join(directory, "Import.json");
    const pngFile = Path.join(directory, "FourCorners.png");
    const binFile = Path.join(directory, "Model.bin");
    const WriteSource = async (modified: object, accept = true) => {
        const text = JSON.stringify(modified);
        await Fs.writeFile(sourceFile, text);
        if (accept) manifest.sourceDocumentSha256 = Sha(text);
        await Fs.writeFile(manifestFile, JSON.stringify(manifest));
    };
    await Promise.all([WriteSource(source), Fs.writeFile(binFile, bin), Fs.writeFile(pngFile, TestPng(pixels))]);
    return { bin, binFile, source, sourceFile, manifest, manifestFile, pngFile, pixels, WriteSource };
}

function BuildTool(name: string, output: string) {
    const result = spawnSync(process.execPath, ["--no-warnings", "--experimental-strip-types", Builder,
        "build", "--target", name, "--platform", "Win64", "--config", "Debug", "--type", "Program",
        "--program-output-root", output], { cwd: Root, encoding: "utf8", maxBuffer: 32 * 1024 * 1024 });
    Assert.equal(result.status, 0, result.error?.message ?? result.stderr + result.stdout);
    return Path.join(output, "Binaries/Win64/Debug", name, "Program", `${name}.exe`);
}

async function TreeHashes(root: string) {
    const files = (await Fs.readdir(root, { withFileTypes: true, recursive: true })).filter(entry => entry.isFile());
    return Object.fromEntries(await Promise.all(files.map(async entry => {
        const absolute = Path.join(entry.parentPath, entry.name);
        return [Path.relative(root, absolute), Sha(await Fs.readFile(absolute))];
    })));
}

function Payload(file: Buffer, version: number) {
    Assert.equal(file.toString("ascii", 0, 4), "LEAS");
    Assert.equal(file.readUInt32LE(4), version);
    return file.subarray(48 + file.readUInt32LE(40));
}

Test("glTF converts static geometry, preserves explicit IDs and only publishes complete generations", { skip: process.platform !== "win32" }, async context => {
    await Fs.mkdir(TemporaryRoot, { recursive: true });
    const temp = await Fs.mkdtemp(Path.join(TemporaryRoot, "GltfTest-"));
    context.after(() => Fs.rm(temp, { recursive: true, force: true }));
    const assetImport = process.env.LIMITLESS_GLTF_TEST_ASSET_IMPORT ?? BuildTool("AssetImport", temp);
    const sceneTool = process.env.LIMITLESS_GLTF_TEST_SCENE_TOOL ?? BuildTool("SceneTool", temp);
    const sourceRoot = Path.join(temp, "src");
    const output = Path.join(temp, "out");
    await Fs.mkdir(sourceRoot);
    const fixture = await PutFixture(sourceRoot);
    const opts = { assetImport, sceneTool, manifest: fixture.manifestFile, output };
    const beforeCheck = await TreeHashes(sourceRoot);
    const checked = await RunGltfImport("check", opts);
    Assert.equal(checked.ok, true);
    Assert.ok(checked.warnings?.length);
    Assert.deepEqual(await TreeHashes(sourceRoot), beforeCheck);
    await Assert.rejects(Fs.stat(output), { code: "ENOENT" });

    const initial = await RunGltfImport("import", opts);
    const firstGeneration = initial.contentRoot!;
    const firstBytes = await TreeHashes(firstGeneration);
    const readAsset = (name: string) => Fs.readFile(Path.join(firstGeneration, "Assets", name));
    const mesh = Payload(await readAsset("Meshes/Tint.leasset"), 3);
    Assert.deepEqual([mesh.readUInt32LE(0), mesh.readUInt32LE(4), mesh.readUInt32LE(8)], [3, 3, 1]);
    Assert.deepEqual(Array.from({ length: 9 }, (_, i) => mesh.readFloatLE(12 + i * 4) || 0), [-1, 0, 2, -3, 0, -1, -1, 2, 2]);
    Assert.deepEqual(Array.from({ length: 6 }, (_, i) => mesh.readFloatLE(48 + i * 4)), [0, 0, 1, 0, 0, 1]);
    Assert.deepEqual(Array.from({ length: 9 }, (_, i) => mesh.readFloatLE(72 + i * 4) || 0), [0, 0, -1, 0, 0, -1, 0, 0, -1]);
    Assert.deepEqual(Array.from({ length: 3 }, (_, i) => mesh.readUInt32LE(108 + i * 4)), [0, 1, 2]);
    const material = Payload(await readAsset("Materials/Tint.leasset"), 2);
    Assert.deepEqual([0, 4, 8].map(offset => material.readFloatLE(offset)), [0.75, 0.5, 0.25]);
    Assert.deepEqual([...Payload(await readAsset("Textures/Corners.leasset"), 1).subarray(8)], fixture.pixels);
    Payload(await readAsset("Materials/Solid.leasset"), 1);
    const savedScene = JSON.parse(await Fs.readFile(Path.join(firstGeneration, "Scene.json"), "utf8"));
    type Entity = { id: string; parent?: string; transform: { translation: number[]; rotation: number[] }; mesh: { mesh: string; material: string } };
    const entities = new Map<string, Entity>(savedScene.entities.map((entity: Entity) => [entity.id, entity]));
    Assert.equal(entities.size, 7);
    Assert.deepEqual(entities.get(Id(21))!.transform.translation, [-1, 2, -3]);
    Assert.equal(entities.get(Id(21))!.parent, Id(20));
    Assert.ok(Math.abs(entities.get(Id(20))!.transform.rotation[2] + Math.SQRT1_2) < 1e-6);
    Assert.deepEqual([30, 31, 32, 33].map(id => entities.get(Id(id))!.mesh.material), [Id(12), Id(13), Id(12), Id(13)]);
    Assert.deepEqual([30, 31, 32, 33].map(id => entities.get(Id(id))!.mesh.mesh), [Id(10), Id(11), Id(10), Id(11)]);
    Assert.deepEqual([30, 31, 32, 33].map(id => entities.get(Id(id))!.parent), [Id(21), Id(21), Id(22), Id(22)]);

    const authorScene = Path.join(temp, "Author.json");
    entities.get(Id(21))!.transform.translation[0] = 4;
    const authorBytes = JSON.stringify(savedScene);
    await Fs.writeFile(authorScene, authorBytes);
    const again = await RunGltfImport("import", { ...opts, authorScene });
    Assert.deepEqual(await TreeHashes(again.contentRoot!), firstBytes);
    Assert.equal(await Fs.readFile(authorScene, "utf8"), authorBytes);
    const pointer = Path.join(output, "current.json");
    const priorPointer = await Fs.readFile(pointer, "utf8");

    const reexport = structuredClone(fixture.source);
    [reexport.nodes[1], reexport.nodes[2]] = [reexport.nodes[2], reexport.nodes[1]];
    await fixture.WriteSource(reexport, false);
    await Assert.rejects(RunGltfImport("check", opts), reason => reason instanceof GltfImportError && reason.report.code === "source_document_changed");
    fixture.manifest.nodes[1].selector.node = 2;
    fixture.manifest.nodes[2].selector.node = 1;
    await fixture.WriteSource(reexport);
    Assert.equal((await RunGltfImport("check", opts)).ok, true);

    const Unchanged = async () => {
        Assert.equal(await Fs.readFile(pointer, "utf8"), priorPointer);
        Assert.deepEqual(await TreeHashes(firstGeneration), firstBytes);
        Assert.equal(await Fs.readFile(authorScene, "utf8"), authorBytes);
    };
    const Fail = async (operation: Promise<unknown>, code?: string) => {
        await Assert.rejects(operation, reason => reason instanceof GltfImportError && (!code || reason.report.code === code));
        await Unchanged();
    };
    await Fs.writeFile(fixture.binFile, fixture.bin.subarray(0, 99));
    await Fail(RunGltfImport("import", opts));
    await Fs.writeFile(fixture.binFile, fixture.bin);
    for (const mutator of [
        (input: typeof reexport) => { input.bufferViews[0].byteStride = 8; },
        (input: typeof reexport) => { input.accessors[3].byteOffset = 999; },
        (input: typeof reexport) => { input.buffers[0].uri = "../Model.bin"; },
        (input: typeof reexport) => { Object.assign(input, { extensionsRequired: ["KHR_draco_mesh_compression"] }); },
        (input: typeof reexport) => { Object.assign(input.meshes[0].primitives[0], { mode: 1 }); },
    ]) {
        const malformed = structuredClone(reexport);
        mutator(malformed);
        await fixture.WriteSource(malformed);
        await Fail(RunGltfImport("check", opts));
    }
    await fixture.WriteSource(reexport);
    await Fs.writeFile(fixture.pngFile, Buffer.from("invalid PNG"));
    await Fail(RunGltfImport("import", opts));
    await Fs.writeFile(fixture.pngFile, TestPng(fixture.pixels.map((v, i) => i % 4 === 3 ? v : 255 - v)));
    const beforeIds = fixture.manifest.assets.map(x => x.id);
    fixture.manifest.assets[1].id = Id(99);
    await Fs.writeFile(fixture.manifestFile, JSON.stringify(fixture.manifest));
    await Fail(RunGltfImport("check", opts), "identity_changed");
    fixture.manifest.assets[1].id = beforeIds[1];
    await Fs.writeFile(fixture.manifestFile, JSON.stringify(fixture.manifest));

    const missingDraw = fixture.manifest.draws.pop()!;
    await Fs.writeFile(fixture.manifestFile, JSON.stringify(fixture.manifest));
    await Fail(RunGltfImport("check", opts), "identity_changed");
    fixture.manifest.draws.push(missingDraw);
    await Fs.writeFile(Path.join(sourceRoot, "Rig.json"), '{"version":1,"entities":[]}');
    await Fs.writeFile(Path.join(sourceRoot, "Invalid.material"), "not-material\n");
    fixture.manifest.preview = { scene: "Rig.json", assets: [{ id: Id(55), kind: "material", path: "Game/Materials/Broken", source: "Invalid.material" }] };
    await Fs.writeFile(fixture.manifestFile, JSON.stringify(fixture.manifest));
    await Fail(RunGltfImport("import", opts), "preview_asset_error");
    delete fixture.manifest.preview;
    await Fs.writeFile(fixture.manifestFile, JSON.stringify(fixture.manifest));

    const invalidScene = Path.join(temp, "Invalid.json");
    await Fs.writeFile(invalidScene, JSON.stringify({ version: 1, entities: [{ id: Id(60), parent: Id(61) }] }));
    await Fail(RunGltfImport("import", { ...opts, authorScene: invalidScene }));
    await Fs.chmod(pointer, 0o444);
    try {
        await Fail(RunGltfImport("import", { ...opts, authorScene }));
    } finally {
        await Fs.chmod(pointer, 0o666);
    }
    const last = await RunGltfImport("import", { ...opts, authorScene });
    const acceptedPointer = await Fs.readFile(pointer, "utf8");
    Assert.notEqual(acceptedPointer, priorPointer);
    Assert.deepEqual(await TreeHashes(firstGeneration), firstBytes);
    Assert.notDeepEqual(await Fs.readFile(Path.join(last.contentRoot!, "Assets/Textures/Corners.leasset")), await readAsset("Textures/Corners.leasset"));
    const next = randomUUID();
    await Fs.cp(last.contentRoot!, Path.join(output, "generations", next), { recursive: true });
    const stale = spawnSync(assetImport, ["gltf", "publish", output, again.generation!, next], { encoding: "utf8" });
    Assert.equal(stale.status, 1, stale.stdout + stale.stderr);
    Assert.equal(await Fs.readFile(pointer, "utf8"), acceptedPointer);
    Assert.equal(await Fs.readFile(authorScene, "utf8"), authorBytes);
});
