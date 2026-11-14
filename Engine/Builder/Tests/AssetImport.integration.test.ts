import * as Assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
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

function Payload(File: Buffer, Version = 1): Buffer {
    Assert.equal(File.toString("ascii", 0, 4), "LEAS");
    Assert.equal(File.readUInt32LE(4), Version);
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
    const OriginalSource = "v -0.75 -0.5 0\nv 0.75 -0.5 0\nv 0 0.75 0\nf 1 2 3\n";
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
    Assert.equal(createHash("sha256").update(Bytes).digest("hex"),
        "4538257c001b2216989143bf760b4cf8d329e77adf42e3aef344d923de1fde8b");
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
        ["s 1\n", /unsupported OBJ record/],
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

    const TexturedMesh = [
        "v 0 0 0", "v 1 0 0", "v 1 1 0", "v 0 1 0",
        "vt 0 0", "vt 1 0", "vt 1 1", "vt 0 1", "vt 0.25 0.75",
        "f 1/1 2/2 3/3", "f 1/5 3/3 4/4", "",
    ].join("\n");
    await Fs.writeFile(Source, TexturedMesh);
    const Textured = Import("mesh", Source);
    Assert.equal(Textured.status, 0, Textured.stderr);
    const MeshV2Bytes = await Fs.readFile(Output);
    Assert.equal(createHash("sha256").update(MeshV2Bytes).digest("hex"),
        "bf295c16fda94d91d8f22ad5a6a0e92fb675365fc33680570244ee57ea01ef39");
    const MeshV2 = Payload(MeshV2Bytes, 2);
    Assert.deepEqual([MeshV2.readUInt32LE(0), MeshV2.readUInt32LE(4)], [5, 6]);
    Assert.deepEqual(
        Array.from({ length: 15 }, (_, Index) => MeshV2.readFloatLE(8 + 4 * Index)),
        [0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 0],
    );
    Assert.deepEqual(
        Array.from({ length: 10 }, (_, Index) => MeshV2.readFloatLE(68 + 4 * Index)),
        [0, 1, 1, 1, 1, 0, 0.25, 0.25, 0, 0],
    );
    Assert.deepEqual(
        Array.from({ length: 6 }, (_, Index) => MeshV2.readUInt32LE(108 + 4 * Index)),
        [0, 1, 2, 3, 2, 4],
    );
    Assert.equal(Import("mesh", Source).status, 0);
    Assert.deepEqual(await Fs.readFile(Output), MeshV2Bytes);
    for (const [Face, Error] of [
        ["f 1/6 2/2 3/3", /UV index exceeds UV coordinate count/],
        ["f 1///1 2/2 3/3", /positive OBJ corners/],
        ["f 1/1 2 3/3", /cannot mix/],
        ["f 1 2 3", /cannot mix/],
    ] as const) {
        await Fs.writeFile(Source, `${TexturedMesh}${Face}\n`);
        const Rejected = Import("mesh", Source);
        Assert.equal(Rejected.status, 1, Rejected.stderr);
        Assert.match(Rejected.stderr, Error);
        Assert.match(Rejected.stderr, /source\.obj:12:/);
        Assert.deepEqual(await Fs.readFile(Output), MeshV2Bytes);
    }

    const PositionLines = ["v 0 0 0", "v 1 0 0", "v 1 1 0", "v 0 1 0"];
    const NormalLines = ["vn 0 0 25", "vn 1e30 0 0", "vn 0 0 -1e-30"];
    const UntexturedNormals = [
        ...PositionLines, ...NormalLines, "f 3//3 2//3 1//3", "",
    ].join("\n");
    await Fs.writeFile(Source, UntexturedNormals);
    const NormalOnlyCheck = Import("mesh", Source, "");
    Assert.equal(NormalOnlyCheck.status, 0, NormalOnlyCheck.stderr);
    Assert.deepEqual(await Fs.readFile(Output), MeshV2Bytes);
    const NormalOnlyImport = Import("mesh", Source);
    Assert.equal(NormalOnlyImport.status, 0, NormalOnlyImport.stderr);
    const NormalOnly = Payload(await Fs.readFile(Output), 3);
    Assert.deepEqual([NormalOnly.readUInt32LE(0), NormalOnly.readUInt32LE(4), NormalOnly.readUInt32LE(8)],
        [3, 3, 0]);
    Assert.deepEqual(Array.from({ length: 18 }, (_, Index) => NormalOnly.readFloatLE(12 + 4 * Index)),
        [1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, -1, 0, 0, -1]);
    Assert.deepEqual(Array.from({ length: 3 }, (_, Index) => NormalOnly.readUInt32LE(84 + 4 * Index)), [0, 1, 2]);

    const NormalSeams = [
        ...PositionLines, "vt 0 0", "vt 1 0", "vt 1 1", "vt 0 1", "vt 0.25 0.75",
        ...NormalLines, "f 1/1/1 2/2/1 3/3/1", "f 1/1/2 3/5/1 4/4/1", "",
    ].join("\n");
    await Fs.writeFile(Source, NormalSeams);
    const NormalSeamImport = Import("mesh", Source);
    Assert.equal(NormalSeamImport.status, 0, NormalSeamImport.stderr);
    const MeshV3Bytes = await Fs.readFile(Output);
    const MeshV3 = Payload(MeshV3Bytes, 3);
    Assert.deepEqual([MeshV3.readUInt32LE(0), MeshV3.readUInt32LE(4), MeshV3.readUInt32LE(8)], [6, 6, 1]);
    Assert.deepEqual(Array.from({ length: 18 }, (_, Index) => MeshV3.readFloatLE(12 + 4 * Index)),
        [0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0]);
    Assert.deepEqual(Array.from({ length: 12 }, (_, Index) => MeshV3.readFloatLE(84 + 4 * Index)),
        [0, 1, 1, 1, 1, 0, 0, 1, 0.25, 0.25, 0, 0]);
    Assert.deepEqual(Array.from({ length: 18 }, (_, Index) => MeshV3.readFloatLE(132 + 4 * Index)),
        [0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 1]);
    Assert.deepEqual(Array.from({ length: 6 }, (_, Index) => MeshV3.readUInt32LE(204 + 4 * Index)),
        [0, 1, 2, 3, 4, 5]);
    const RepeatNormals = Import("mesh", Source);
    Assert.equal(RepeatNormals.status, 0, RepeatNormals.stderr);
    Assert.deepEqual(await Fs.readFile(Output), MeshV3Bytes);

    for (const [InvalidSource, Line, Error] of [
        [NormalSeams.replace("vn 0 0 25", "vn 0 0 0"), 10, /normal coordinates with nonzero length/],
        [NormalSeams.replace("vn 0 0 25", "vn nan 0 0"), 10, /finite decimal normal/],
        [NormalSeams.replace("f 1/1/1", "f 1/1/"), 13, /positive OBJ corners/],
        [NormalSeams.replace("f 1/1/1", "f 1/1/4"), 13, /normal index exceeds normal coordinate count/],
        [NormalSeams.replace("f 1/1/1", "f 1/1"), 13, /cannot mix OBJ face formats/],
        [OriginalSource + "vn 0 0 1\n", 4, /every face corner must reference a normal/],
    ] as const) {
        await Fs.writeFile(Source, InvalidSource);
        for (const Destination of ["", Output]) {
            const Rejected = Import("mesh", Source, Destination);
            Assert.equal(Rejected.status, 1, Rejected.stderr);
            Assert.match(Rejected.stderr, Error);
            Assert.ok(Rejected.stderr.includes(`source.obj:${Line}:`), Rejected.stderr);
            Assert.deepEqual(await Fs.readFile(Output), MeshV3Bytes);
        }
    }

    const P3 = Path.join(Temp, "ascii.ppm");
    await Fs.writeFile(P3, "P3\n2 1\n255\n32 35 10 128 64 192\n");
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
    await Fs.writeFile(InvalidMaterial, "base-color # v1 allows whitespace between components\n1 0.35 \n0.12\n");
    const Continued = Import("material", InvalidMaterial, MaterialPath, OtherId, MaterialVirtual);
    Assert.equal(Continued.status, 0, Continued.stderr);
    Assert.deepEqual(Payload(await Fs.readFile(MaterialPath)), Color);
    await Fs.writeFile(InvalidMaterial, "base-color 0 -1 0\n");
    const Invalid = Import("material", InvalidMaterial, "");
    Assert.equal(Invalid.status, 1, Invalid.stderr);
    Assert.match(Invalid.stderr, /finite non-negative decimal/);

    const TexturedMaterial = Path.join(Temp, "textured.material");
    const WithTexture = `base-color 0.5 1 0.5\nbase-color-texture ${Id}\n`;
    await Fs.writeFile(TexturedMaterial, WithTexture);
    const CheckedMaterial = Import("material", TexturedMaterial, "", OtherId, MaterialVirtual);
    Assert.equal(CheckedMaterial.status, 0, CheckedMaterial.stderr);
    Assert.deepEqual(Payload(await Fs.readFile(MaterialPath)), Color);
    const Upgraded = Import("material", TexturedMaterial, MaterialPath, OtherId, MaterialVirtual);
    Assert.equal(Upgraded.status, 0, Upgraded.stderr);
    const TexturedBytes = await Fs.readFile(MaterialPath);
    const TexturedColor = Payload(TexturedBytes, 2);
    Assert.deepEqual(
        Array.from({ length: 3 }, (_, Index) => TexturedColor.readFloatLE(4 * Index)), [0.5, 1, 0.5],
    );
    Assert.equal(TexturedColor.subarray(12).toString("hex"), Id.replaceAll("-", ""));
    Assert.equal(Import("material", TexturedMaterial, MaterialPath, OtherId, MaterialVirtual).status, 0);
    Assert.deepEqual(await Fs.readFile(MaterialPath), TexturedBytes);
    for (const [Contents, Error] of [
        [`base-color 1 1 1\nbase-color-texture BAD\n`, /:2: expected base-color-texture/],
        [`${WithTexture}base-color-texture ${Id}\n`, /:3: duplicate base-color-texture/],
        [`${WithTexture}base-color 1 1 1\n`, /:3: duplicate base-color directive/],
    ] as const) {
        await Fs.writeFile(TexturedMaterial, Contents);
        const Rejected = Import("material", TexturedMaterial, MaterialPath, OtherId, MaterialVirtual);
        Assert.equal(Rejected.status, 1, Rejected.stderr);
        Assert.match(Rejected.stderr, Error);
        Assert.deepEqual(await Fs.readFile(MaterialPath), TexturedBytes);
    }
    const Downgraded = Import("material", MaterialSource, MaterialPath, OtherId, MaterialVirtual);
    Assert.equal(Downgraded.status, 0, Downgraded.stderr);
    Assert.deepEqual(Payload(await Fs.readFile(MaterialPath)), Color);
});
