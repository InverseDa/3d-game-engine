import * as Assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import * as Fs from "node:fs/promises";
import * as Os from "node:os";
import * as Path from "node:path";
import Test from "node:test";

const Root = Path.resolve(import.meta.dirname, "../../..");
const Examples = Path.join(Root, "Engine/Content/Examples/AssetImport");
const Builder = Path.join(Root, "Engine/Builder/Source/Cli/CommandLine.ts");
const Id = "62fdc991-27a8-4a81-a077-647b47fd1f7b";
const OtherId = "62fdc991-27a8-4a81-a077-647b47fd1f7c";
const Virtual = "Game/Meshes/Triangle";

function Payload(File: Buffer): Buffer {
    Assert.equal(File.toString("ascii", 0, 4), "LEAS");
    Assert.equal(File.readUInt32LE(4), 1);
    const Offset = 48 + File.readUInt32LE(40);
    Assert.equal(File.length, Offset + File.readUInt32LE(44));
    return File.subarray(Offset);
}

Test("AssetImport validates, converts and safely publishes editable sources", {
    skip: process.platform !== "win32",
}, async (Context) => {
    const Build = spawnSync(process.execPath, [
        "--no-warnings", "--experimental-strip-types", Builder,
        "build", "--target", "AssetImport", "--platform", "Win64", "--config", "Debug", "--type", "Program",
    ], { cwd: Root, encoding: "utf8", maxBuffer: 16 * 1024 * 1024 });
    Assert.equal(Build.status, 0, Build.error?.message ?? Build.stderr + Build.stdout);
    const Graph = await Fs.readFile(
        Path.join(Root, "Engine/Intermediate/Build/Win64/Debug/AssetImport/Program/build.ninja"), "utf8",
    );
    Assert.doesNotMatch(Graph, /Runtime[/\\](?:RHI|Renderer)[/\\]/);
    const Executable = Path.join(Root, "Engine/Binaries/Win64/Debug/AssetImport/Program/AssetImport.exe");

    const Temp = await Fs.mkdtemp(Path.join(Os.tmpdir(), "limitless-import-"));
    Context.after(async () => Fs.rm(Temp, { recursive: true, force: true }));
    const Output = Path.join(Temp, "资产 with spaces.leasset");
    const Source = Path.join(Temp, "可编辑 source.obj");
    const OriginalSource = await Fs.readFile(Path.join(Examples, "Triangle.obj"), "utf8");
    await Fs.writeFile(Source, OriginalSource);
    function Import(
        Kind: string, File: string, Destination = Output, AssetId = Id, AssetPath = Virtual,
    ): ReturnType<typeof spawnSync<string>> {
        return spawnSync(Executable, [
            Kind, "--source", File, "--id", AssetId, "--path", AssetPath,
            ...(Destination === "" ? ["--check"] : ["--output", Destination]),
        ], { cwd: Temp, encoding: "utf8" });
    }

    const Check = Import("mesh", Source, "");
    Assert.equal(Check.status, 0, Check.stderr);
    Assert.deepEqual(await Fs.readdir(Temp), [Path.basename(Source)]);
    const First = Import("mesh", Source);
    Assert.equal(First.status, 0, First.stderr);
    const Bytes = await Fs.readFile(Output);
    Assert.equal(Bytes.toString("ascii", 48, 48 + Buffer.byteLength(Virtual)), Virtual);
    const Mesh = Payload(Bytes);
    Assert.deepEqual([Mesh.readUInt32LE(0), Mesh.readUInt32LE(4)], [3, 3]);
    Assert.deepEqual(
        Array.from({ length: 9 }, (_, Index) => Mesh.readFloatLE(8 + 4 * Index)),
        [-0.75, -0.5, 0, 0.75, -0.5, 0, 0, 0.75, 0],
    );
    Assert.deepEqual(
        Array.from({ length: 3 }, (_, Index) => Mesh.readUInt32LE(44 + 4 * Index)), [0, 1, 2],
    );
    const Reimport = Import("mesh", Source);
    Assert.equal(Reimport.status, 0, Reimport.stderr);
    Assert.deepEqual(await Fs.readFile(Output), Bytes);

    const MaterialSource = Path.join(Examples, "WarmOrange.material");
    for (const Conflict of [
        Import("mesh", Source, Output, OtherId),
        Import("mesh", Source, Output, Id, "Game/Meshes/Other"),
        Import("material", MaterialSource),
    ]) {
        Assert.equal(Conflict.status, 1, Conflict.stderr);
        Assert.match(Conflict.stderr, /different ID, type or virtual path/);
        Assert.deepEqual(await Fs.readFile(Output), Bytes);
    }
    for (const [Record, Error] of [
        ["f 1 2 4\n", /face index exceeds vertex count/],
        ["f 1 2 3 1\n", /only triangular faces/],
        ["v nan 0 0\n", /finite decimal/],
        ["vt 0 0\n", /unsupported OBJ record/],
    ] as const) {
        await Fs.writeFile(Source, OriginalSource + Record);
        const Invalid = Import("mesh", Source);
        Assert.equal(Invalid.status, 1, Invalid.stderr);
        Assert.match(Invalid.stderr, Error);
        Assert.match(Invalid.stderr, /source\.obj:\d+/);
        Assert.deepEqual(await Fs.readFile(Output), Bytes);
    }

    await Fs.writeFile(Source, OriginalSource.replace("0.75 -0.5", "1.25 -0.5"));
    await Fs.chmod(Output, 0o444);
    try {
        const Locked = Import("mesh", Source);
        Assert.equal(Locked.status, 1, Locked.stderr);
        Assert.deepEqual(await Fs.readFile(Output), Bytes);
    } finally {
        await Fs.chmod(Output, 0o666);
    }
    const Edited = Import("mesh", Source);
    Assert.equal(Edited.status, 0, Edited.stderr);
    Assert.notDeepEqual(await Fs.readFile(Output), Bytes);

    const P3 = Path.join(Examples, "Palette.ppm");
    const P6 = Path.join(Temp, "binary.ppm");
    const Pixels = Buffer.from([32, 35, 10, 128, 64, 192]);
    const P6Header = Buffer.from("P6\n# preserve first whitespace pixel\n2 1\n255\n", "ascii");
    await Fs.writeFile(P6, Buffer.concat([P6Header, Pixels]));
    const TexturePath = Path.join(Temp, "Palette.leasset");
    const TextureVirtual = "Game/Textures/Palette";
    const P3Import = Import("texture", P3, TexturePath, OtherId, TextureVirtual);
    Assert.equal(P3Import.status, 0, P3Import.stderr);
    const TextureBytes = await Fs.readFile(TexturePath);
    const P6Import = Import("texture", P6, TexturePath, OtherId, TextureVirtual);
    Assert.equal(P6Import.status, 0, P6Import.stderr);
    Assert.deepEqual(await Fs.readFile(TexturePath), TextureBytes);
    const Texture = Payload(TextureBytes);
    Assert.deepEqual([Texture.readUInt32LE(0), Texture.readUInt32LE(4)], [2, 1]);
    Assert.deepEqual([...Texture.subarray(8)], [32, 35, 10, 255, 128, 64, 192, 255]);
    await Fs.writeFile(P6, Buffer.concat([P6Header, Pixels.subarray(1)]));
    const Truncated = Import("texture", P6, TexturePath, OtherId, TextureVirtual);
    Assert.equal(Truncated.status, 1, Truncated.stderr);
    Assert.deepEqual(await Fs.readFile(TexturePath), TextureBytes);

    const MaterialPath = Path.join(Temp, "WarmOrange.leasset");
    const MaterialVirtual = "Game/Materials/WarmOrange";
    const Material = Import("material", MaterialSource, MaterialPath, OtherId, MaterialVirtual);
    Assert.equal(Material.status, 0, Material.stderr);
    const Color = Payload(await Fs.readFile(MaterialPath));
    Assert.deepEqual(
        Array.from({ length: 3 }, (_, Index) => Color.readFloatLE(4 * Index)),
        [1, Math.fround(0.35), Math.fround(0.12)],
    );
    const InvalidMaterial = Path.join(Temp, "invalid.material");
    await Fs.writeFile(InvalidMaterial, "base-color 0 -1 0\n");
    const Invalid = Import("material", InvalidMaterial, "");
    Assert.equal(Invalid.status, 1, Invalid.stderr);
    Assert.match(Invalid.stderr, /finite non-negative decimal/);
});
