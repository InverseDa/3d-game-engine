import * as Assert from "node:assert/strict";
import { createHash } from "node:crypto";
import * as Path from "node:path";
import * as Fs from "node:fs/promises";
import * as Os from "node:os";
import { spawnSync } from "node:child_process";
import Test from "node:test";

const IsWindows = process.platform === "win32";

function RunBuild(
    Builder: string,
    Root: string,
    TargetName: string,
    TargetType: "Game" | "Editor" | "Test",
): ReturnType<typeof spawnSync> {
    return spawnSync(Builder, [
        "build",
        "--target", TargetName,
        "--platform", "Win64",
        "--config", "Debug",
        "--type", TargetType,
    ], {
        cwd: Root,
        encoding: "utf-8",
        maxBuffer: 32 * 1024 * 1024,
        shell: true,
        env: { ...process.env, LIMITLESS_BUILDER_NODE: process.execPath },
    });
}

async function HashFiles(FilePaths: string[]): Promise<Map<string, string>> {
    const Result = new Map<string, string>();
    for (const FilePath of FilePaths) {
        const Contents = await Fs.readFile(FilePath);
        Result.set(FilePath, createHash("sha256").update(Contents).digest("hex"));
    }
    return Result;
}

async function CheckStagedShaders(BinaryRoot: string): Promise<string[]> {
    const ShaderRoot = Path.join(BinaryRoot, "Shaders", "Vulkan");
    const ShaderFiles = [
        "BasePassShadowVS", "BasePassShadowPS", "CompositeVS", "CompositePS", "ShadowDepthVS",
    ].map((Name) => Path.join(ShaderRoot, `${Name}.spv`));
    for (const File of ShaderFiles) {
        const Bytes = await Fs.readFile(File);
        Assert.ok(Bytes.length > 20 && Bytes.length % 4 === 0, `Invalid SPIR-V size: ${File}`);
        Assert.equal(Bytes.readUInt32LE(0), 0x07230203, `Invalid SPIR-V magic: ${File}`);
    }
    return ShaderFiles;
}

async function ShaderWriteTimes(ShaderFiles: string[]): Promise<number[]> {
    return Promise.all(ShaderFiles.map(async (File) => (await Fs.stat(File)).mtimeMs));
}

async function CheckStagedLantern(BinaryRoot: string, SceneTool: string): Promise<string[]> {
    const root = Path.join(BinaryRoot, "LanternContent");
    const pointer = Path.join(root, "current.json");
    const { generation } = JSON.parse(await Fs.readFile(pointer, "utf8"));
    Assert.match(generation, /^[0-9a-f-]{36}$/);
    const content = Path.join(root, "generations", generation);
    const scene = Path.join(content, "Scene.json");
    const assets = Path.join(content, "Assets");
    const check = spawnSync(SceneTool, ["check", "--json", scene, "Game", assets], { encoding: "utf8" });
    Assert.equal(check.status, 0, check.error?.message ?? check.stdout + check.stderr);
    const manifest = Path.join(content, "Import.json");
    const { assets: bindings, nodes, draws } = JSON.parse(await Fs.readFile(manifest, "utf8"));
    Assert.equal(nodes.length, 4);
    Assert.equal(draws.length, 3);
    Assert.deepEqual(bindings.map((entry: { kind: string }) => entry.kind).sort(), ["material", "mesh", "mesh", "mesh", "texture"]);
    return [pointer, scene, manifest, ...bindings.map((entry: { path: string }) =>
        Path.join(assets, `${entry.path.slice("Game/".length)}.leasset`))];
}

Test("Game, Editor, and Test variants build incrementally without overwriting each other", {
    skip: !IsWindows,
}, async () => {
    const Root = Path.resolve(import.meta.dirname, "..", "..", "..");
    const Builder = Path.join(Root, "Engine", "Builder", "LimitlessBuilder.bat");
    const GameRoot = Path.join(Root, "Engine", "Binaries", "Win64", "Debug", "Limitless", "Game");
    const GameIntermediate = Path.join(
        Root, "Engine", "Intermediate", "Build", "Win64", "Debug", "Limitless", "Game",
    );
    const EditorRoot = Path.join(Root, "Engine", "Binaries", "Win64", "Debug", "Limitless", "Editor");
    const EditorIntermediate = Path.join(
        Root, "Engine", "Intermediate", "Build", "Win64", "Debug", "Limitless", "Editor",
    );
    const TestRoot = Path.join(Root, "Engine", "Binaries", "Win64", "Debug", "LimitlessTests", "Test");
    const TestIntermediate = Path.join(
        Root, "Engine", "Intermediate", "Build", "Win64", "Debug", "LimitlessTests", "Test",
    );

    const GameBuild = RunBuild(Builder, Root, "Limitless", "Game");
    Assert.equal(GameBuild.status, 0, `${GameBuild.stdout}\n${GameBuild.stderr}`);
    const GameShaders = await CheckStagedShaders(GameRoot);
    const SceneTool = Path.join(GameIntermediate,
        "Generated/DemoApplication/ContentTools/Binaries/Win64/Debug/SceneTool/Program/SceneTool.exe");
    const GameArtifacts = [
        Path.join(GameIntermediate, "build.ninja"),
        Path.join(GameIntermediate, "compile_commands.json"),
        Path.join(GameRoot, "Core.lib"),
        Path.join(GameRoot, "LimitlessGame.exe"),
        Path.join(GameRoot, "SolidGeometryContent/Scene.json"),
        Path.join(GameRoot, "SolidGeometryContent/Assets/Meshes/SolidBox.leasset"),
        ...await CheckStagedLantern(GameRoot, SceneTool),
        ...GameShaders,
    ];
    const GameBeforeEditor = await HashFiles(GameArtifacts);
    const GameShaderTimesBeforeEditor = await ShaderWriteTimes(GameShaders);
    const Demo = Path.join(GameRoot, "DemoContent");
    const AssetImport = Path.join(GameIntermediate,
        "Generated/DemoApplication/ContentTools/Binaries/Win64/Debug/AssetImport/Program/AssetImport.exe");
    const SolidGeometry = Path.join(GameRoot, "SolidGeometryContent");
    const SolidCheck = spawnSync(SceneTool, [
        "check", "--json", Path.join(SolidGeometry, "Scene.json"), "Game", Path.join(SolidGeometry, "Assets"),
    ], { cwd: Root, encoding: "utf-8" });
    Assert.equal(SolidCheck.status, 0, SolidCheck.error?.message ?? SolidCheck.stdout + SolidCheck.stderr);
    Assert.equal(JSON.parse(SolidCheck.stdout).entityCount, 5);
    const Scratch = await Fs.mkdtemp(Path.join(Os.tmpdir(), "limitless-textured-scene-"));
    try {
        await Fs.cp(Demo, Scratch, { recursive: true });
        const SceneFile = Path.join(Scratch, "Scene.json");
        const AssetRoot = Path.join(Scratch, "Assets");
        const Check = (File = SceneFile) => spawnSync(SceneTool, ["check", File, "Game", AssetRoot], {
            cwd: Scratch, encoding: "utf-8",
        });
        function CheckJson(File: string, ExpectedExit: number) {
            const Run = spawnSync(SceneTool, ["check", "--json", File, "Game", AssetRoot], {
                cwd: Scratch, encoding: "utf-8",
            });
            Assert.equal(Run.status, ExpectedExit, Run.error?.message ?? Run.stdout + Run.stderr);
            Assert.equal(Run.stderr, "");
            const Report = JSON.parse(Run.stdout);
            Assert.equal(Report.ok, ExpectedExit === 0);
            Assert.equal(Report.path, File);
            Assert.equal(typeof Report.code, "string");
            Assert.equal(typeof Report.message, "string");
            return Report;
        }
        const Valid = Check();
        Assert.equal(Valid.status, 0, Valid.stderr);
        const SceneText = await Fs.readFile(SceneFile, "utf-8");
        const Scene = JSON.parse(SceneText);
        const CameraScene = JSON.parse(SceneText);
        const CameraId = "8a0a5d74-17ef-48e0-a46b-f7105518e704";
        const CameraEntity = CameraScene.entities.find((Entity: { id: string }) => Entity.id === CameraId);
        Assert.ok(CameraEntity?.transform && CameraEntity?.camera);
        CameraEntity.transform.translation = [0, 0, 4];
        CameraEntity.transform.rotation = [0, 0, 0.13052619, 0.99144486];
        CameraEntity.camera.verticalFieldOfViewRadians = 0.9;
        const UnicodeCameraFile = Path.join(Scratch, "场景 相机.json");
        const EditedCameraBytes = Buffer.from(JSON.stringify(CameraScene, null, 2) + "\n");
        await Fs.writeFile(UnicodeCameraFile, EditedCameraBytes);
        const ValidJson = CheckJson(UnicodeCameraFile, 0);
        Assert.equal(ValidJson.code, "ok");
        Assert.equal(ValidJson.entityCount, CameraScene.entities.length);
        Assert.deepEqual(await Fs.readFile(UnicodeCameraFile), EditedCameraBytes);

        async function CheckCameraFixture(FileName: string, InvalidField?: string): Promise<void> {
            const File = Path.join(Scratch, FileName);
            const Before = Buffer.from(`${JSON.stringify(CameraScene, null, 2)}\n`);
            await Fs.writeFile(File, Before);
            const Result = Check(File);
            Assert.equal(Result.status, InvalidField === undefined ? 0 : 1, Result.stderr);
            if (InvalidField !== undefined) {
                Assert.ok(Result.stderr.startsWith(`${File}:`), Result.stderr);
                Assert.ok(Result.stderr.includes(`entity ${CameraId}, field ${InvalidField})`), Result.stderr);
            }
            Assert.deepEqual(await Fs.readFile(File), Before);
        }

        await CheckCameraFixture("CameraEdited.json");
        CameraEntity.camera.verticalFieldOfViewRadians = 0;
        await CheckCameraFixture("CameraInvalidFov.json", "verticalFieldOfViewRadians");
        const InvalidCameraFile = Path.join(Scratch, "CameraInvalidFov.json");
        const InvalidCameraBytes = await Fs.readFile(InvalidCameraFile);
        const InvalidJson = CheckJson(InvalidCameraFile, 1);
        Assert.equal(InvalidJson.code, "scene_document_error");
        Assert.equal(InvalidJson.entity, CameraId);
        Assert.equal(InvalidJson.field, "verticalFieldOfViewRadians");
        Assert.ok(InvalidJson.line > 0 && InvalidJson.column > 0);
        Assert.deepEqual(await Fs.readFile(InvalidCameraFile), InvalidCameraBytes);
        CameraEntity.camera.verticalFieldOfViewRadians = 0.9;
        CameraEntity.transform.rotation = [0, 0, 0, 0];
        await CheckCameraFixture("CameraInvalidRotation.json", "rotation");

        const LightId = "8a0a5d74-17ef-48e0-a46b-f7105518e707";
        const SecondLightId = "8a0a5d74-17ef-48e0-a46b-f7105518e708";
        type Light = { direction: number[]; color: number[]; intensity?: number; active: boolean };
        type LightEntity = { id: string; directionalLight: Light };
        const LightScene = JSON.parse(SceneText);
        LightScene.version = 2;
        LightScene.entities.push({
            id: LightId,
            directionalLight: { direction: [0, 0, -1], color: [1, 0.5, 0.25], intensity: 0.5, active: true },
        });
        const LightEntity: LightEntity = LightScene.entities.find((Entity: LightEntity) => Entity.id === LightId);
        const EditedLight = { direction: [0, 0, -4], color: [0.25, 0.5, 1], intensity: 2, active: true };
        Object.assign(LightEntity.directionalLight, EditedLight);

        async function CheckLightFixture(FileName: string, InvalidField?: string, EntityId = LightId): Promise<void> {
            const File = Path.join(Scratch, FileName);
            await Fs.writeFile(File, JSON.stringify(LightScene, null, 2) + "\n");
            const Before = await HashFiles([File]);
            const Result = Check(File);
            Assert.equal(Result.status, InvalidField === undefined ? 0 : 1, Result.stderr);
            if (InvalidField !== undefined) {
                Assert.ok(Result.stderr.startsWith(`${File}:`), Result.stderr);
                Assert.ok(Result.stderr.includes(`entity ${EntityId}, field ${InvalidField})`), Result.stderr);
            }
            Assert.deepEqual(await HashFiles([File]), Before);
        }

        await CheckLightFixture("LightEdited.json");
        const WrongTypeLight = { ...EditedLight, intensity: "0.5" };
        for (const [Name, Entity] of [
            ["id-first", { id: LightId, directionalLight: WrongTypeLight }],
            ["id-last", { directionalLight: WrongTypeLight, id: LightId }],
        ] as const) {
            const File = Path.join(Scratch, `LightTypedField-${Name}.json`);
            const Bytes = Buffer.from(JSON.stringify({
                ...LightScene, entities: LightScene.entities.map((Item: LightEntity) =>
                    Item.id === LightId ? Entity : Item),
            }, null, 2) + "\n");
            await Fs.writeFile(File, Bytes);
            const InvalidLight = CheckJson(File, 1);
            Assert.equal(InvalidLight.code, "scene_document_error");
            Assert.equal(InvalidLight.entity, LightId);
            Assert.equal(InvalidLight.field, "intensity");
            Assert.ok(InvalidLight.line > 0 && InvalidLight.column > 0);
            Assert.deepEqual(await Fs.readFile(File), Bytes);
        }
        for (const [Field, Invalid] of [
            ["direction", [0, 0, 0]], ["color", [1, 1.25, 0]], ["intensity", 17],
        ] as const) {
            Object.assign(LightEntity.directionalLight, { [Field]: Invalid });
            await CheckLightFixture(`LightInvalid-${Field}.json`, Field);
            Object.assign(LightEntity.directionalLight, EditedLight);
        }
        delete LightEntity.directionalLight.intensity;
        await CheckLightFixture("LightMissingIntensity.json", "intensity");
        Object.assign(LightEntity.directionalLight, EditedLight);
        LightScene.entities.push({ id: SecondLightId, directionalLight: { ...EditedLight } });
        await CheckLightFixture("LightDuplicateActive.json", "active", SecondLightId);
        LightScene.entities.pop();

        const NormalSource = Path.join(Scratch, "WithNormals.obj");
        const NormalAsset = Path.join(AssetRoot, "Meshes/TextureQuad.leasset");
        const NormalAssetId = "62fdc991-27a8-4a81-a077-647b47fd1f7e";
        const OriginalMeshBytes = await Fs.readFile(NormalAsset);
        await Fs.writeFile(NormalSource, "v -1 -1 0\nv 1 -1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0.5 1\n"
            + "vn 0 0 1\nf 1/1/1 2/2/1 3/3/1\n");
        const ImportNormalMesh = spawnSync(AssetImport, [
            "mesh", "--source", NormalSource, "--id", NormalAssetId, "--path", "Game/Meshes/TextureQuad",
            "--output", NormalAsset,
        ], { cwd: Scratch, encoding: "utf-8" });
        Assert.equal(ImportNormalMesh.status, 0, ImportNormalMesh.error?.message ?? ImportNormalMesh.stderr);
        const NormalMeshOk = Check();
        Assert.equal(NormalMeshOk.status, 0, NormalMeshOk.stderr);

        const LitMeshId = "8a0a5d74-17ef-48e0-a46b-f7105518e702";
        const LitParentId = "8a0a5d74-17ef-48e0-a46b-f7105518e709";
        LightScene.entities.find((Entity: { id: string }) => Entity.id === LitMeshId).parent = LitParentId;
        const LitParent = {
            id: LitParentId,
            transform: { translation: [0, 0, 0], rotation: [0, 0, 0, 1], scale: [0, 1, 1] },
        };
        LightScene.entities.push(LitParent);
        const LitSceneFile = Path.join(Scratch, "LitMeshTransform.json");
        const LitSceneBytes = Buffer.from(JSON.stringify(LightScene, null, 2) + "\n");
        await Fs.writeFile(LitSceneFile, LitSceneBytes);
        const InvalidLitMesh = CheckJson(LitSceneFile, 1);
        Assert.equal(InvalidLitMesh.code, "invalid_normal_transform");
        Assert.equal(InvalidLitMesh.entity, LitMeshId);
        Assert.equal(InvalidLitMesh.asset, NormalAssetId);
        Assert.equal(InvalidLitMesh.field, "transform");
        Assert.deepEqual(await Fs.readFile(LitSceneFile), LitSceneBytes);
        LightEntity.directionalLight.active = false;
        await Fs.writeFile(LitSceneFile, JSON.stringify(LightScene, null, 2) + "\n");
        Assert.equal(CheckJson(LitSceneFile, 0).code, "ok");
        LightEntity.directionalLight.active = true;
        LitParent.transform.scale[0] = 1;
        await Fs.writeFile(LitSceneFile, JSON.stringify(LightScene, null, 2) + "\n");
        Assert.equal(CheckJson(LitSceneFile, 0).code, "ok");

        const BadNormalMesh = await Fs.readFile(NormalAsset);
        Assert.equal(BadNormalMesh.readUInt32LE(4), 3);
        const MeshPayload = 48 + BadNormalMesh.readUInt32LE(40);
        const VertexCount = BadNormalMesh.readUInt32LE(MeshPayload);
        const FirstNormal = MeshPayload + 12 + VertexCount * (3 + 2) * 4;
        BadNormalMesh.fill(0, FirstNormal, FirstNormal + 3 * 4);
        await Fs.writeFile(NormalAsset, BadNormalMesh);
        const BadNormal = Check();
        Assert.equal(BadNormal.status, 1, BadNormal.stderr);
        Assert.ok(BadNormal.stderr.includes(
            `entity 8a0a5d74-17ef-48e0-a46b-f7105518e702 field mesh.mesh: asset ${NormalAssetId}`,
        ), BadNormal.stderr);
        const BadMeshJson = CheckJson(SceneFile, 1);
        Assert.equal(BadMeshJson.code, "asset_reference_error");
        Assert.equal(BadMeshJson.entity, LitMeshId);
        Assert.equal(BadMeshJson.asset, NormalAssetId);
        Assert.equal(BadMeshJson.field, "mesh.mesh");
        await Fs.writeFile(NormalAsset, OriginalMeshBytes);

        const TexturedEntity = Scene.entities.find((Entity: { mesh?: { sortKey: string } }) =>
            Entity.mesh?.sortKey === "0");
        TexturedEntity.mesh.mesh = "62fdc991-27a8-4a81-a077-647b47fd1f7b";
        await Fs.writeFile(SceneFile, JSON.stringify(Scene));
        const NoUV = Check();
        Assert.equal(NoUV.status, 1, NoUV.stderr);
        Assert.match(NoUV.stderr, /17ef-48e0-a46b-f7105518e702.*has no UVs/);
        await Fs.writeFile(SceneFile, SceneText);

        const Texture = Path.join(AssetRoot, "Textures/TextureCorners.leasset");
        const Bytes = await Fs.readFile(Texture);
        const BadBytes = Buffer.from(Bytes);
        BadBytes.writeUInt32LE(0, 48 + BadBytes.readUInt32LE(40));
        await Fs.writeFile(Texture, BadBytes);
        const Corrupt = Check();
        Assert.equal(Corrupt.status, 1, Corrupt.stderr);
        Assert.match(Corrupt.stderr, /mesh\.material\.baseColorTexture.*47fd1f7f/);
        await Fs.unlink(Texture);
        const Missing = Check();
        Assert.equal(Missing.status, 1, Missing.stderr);
        Assert.match(Missing.stderr, /mesh\.material\.baseColorTexture.*47fd1f7f/);

        const CookedMaterial = Path.join(AssetRoot, "Materials/TextureCorners.leasset");
        const MaterialBytes = await Fs.readFile(CookedMaterial);
        const OtherMaterialId = Buffer.from("62fdc99127a84a81a077647b47fd1f7c", "hex");
        OtherMaterialId.copy(MaterialBytes, 48 + MaterialBytes.readUInt32LE(40) + 12);
        await Fs.writeFile(CookedMaterial, MaterialBytes);
        const WrongType = Check();
        Assert.equal(WrongType.status, 1, WrongType.stderr);
        Assert.match(WrongType.stderr, /mesh\.material\.baseColorTexture.*47fd1f7c/);
    } finally {
        if (Path.dirname(Path.resolve(Scratch)) === Path.resolve(Os.tmpdir())) {
            await Fs.rm(Scratch, { recursive: true, force: true });
        }
    }

    const EditorBuild = RunBuild(Builder, Root, "Limitless", "Editor");
    Assert.equal(EditorBuild.status, 0, `${EditorBuild.stdout}\n${EditorBuild.stderr}`);
    Assert.deepEqual(await HashFiles(GameArtifacts), GameBeforeEditor, "Editor must not modify Game");
    Assert.deepEqual(await ShaderWriteTimes(GameShaders), GameShaderTimesBeforeEditor,
        "Editor must not rewrite Game shaders");
    const EditorShaders = await CheckStagedShaders(EditorRoot);
    const EditorArtifacts = [
        Path.join(EditorIntermediate, "build.ninja"),
        Path.join(EditorIntermediate, "compile_commands.json"),
        Path.join(EditorRoot, "Core.dll"),
        Path.join(EditorRoot, "Core.lib"),
        Path.join(EditorRoot, "LimitlessEditor.exe"),
        Path.join(EditorRoot, "SolidGeometryContent/Scene.json"),
        Path.join(EditorRoot, "SolidGeometryContent/Assets/Meshes/SolidBox.leasset"),
        ...await CheckStagedLantern(EditorRoot, SceneTool),
        ...EditorShaders,
    ];
    const GameAndEditorArtifacts = [...GameArtifacts, ...EditorArtifacts];
    const StagedShaders = [...GameShaders, ...EditorShaders];
    const BeforeTest = await HashFiles(GameAndEditorArtifacts);
    const ShaderTimesBeforeTest = await ShaderWriteTimes(StagedShaders);

    const TestBuild = RunBuild(Builder, Root, "LimitlessTests", "Test");
    Assert.equal(TestBuild.status, 0, `${TestBuild.stdout}\n${TestBuild.stderr}`);
    const AfterTest = await HashFiles(GameAndEditorArtifacts);
    Assert.deepEqual(AfterTest, BeforeTest, "Test must not modify Game or Editor graphs/artifacts");
    Assert.deepEqual(await ShaderWriteTimes(StagedShaders), ShaderTimesBeforeTest,
        "Test must not rewrite Game or Editor shaders");
    const TestArtifacts = [
        Path.join(TestIntermediate, "build.ninja"),
        Path.join(TestIntermediate, "compile_commands.json"),
        Path.join(TestRoot, "Core.lib"),
        Path.join(TestRoot, "LimitlessTestsDebug.exe"),
    ];
    const AllArtifacts = [...GameAndEditorArtifacts, ...TestArtifacts];
    const BeforeIncrementalBuilds = await HashFiles(AllArtifacts);

    for (const [TargetName, TargetType] of [
        ["Limitless", "Game"],
        ["Limitless", "Editor"],
        ["LimitlessTests", "Test"],
    ] as const) {
        const Incremental = RunBuild(Builder, Root, TargetName, TargetType);
        Assert.equal(Incremental.status, 0, `${Incremental.stdout}\n${Incremental.stderr}`);
        Assert.match(Incremental.stdout, /ninja: no work to do\./i, `${TargetType} must be incremental`);
    }
    Assert.deepEqual(
        await HashFiles(AllArtifacts),
        BeforeIncrementalBuilds,
        "incremental generation/builds must preserve every variant's graphs and artifacts",
    );
    Assert.deepEqual(await ShaderWriteTimes(StagedShaders), ShaderTimesBeforeTest,
        "incremental builds must preserve staged shader modification times");

    const Executable = Path.join(TestRoot, "LimitlessTestsDebug.exe");
    const PassingRun = spawnSync(Executable, [], { cwd: Root, encoding: "utf-8" });
    Assert.equal(PassingRun.status, 0, `${PassingRun.stdout}\n${PassingRun.stderr}`);
    Assert.match(PassingRun.stdout, /All Limitless C\+\+ tests passed/);

    const FailingRun = spawnSync(Executable, ["--force-failure"], {
        cwd: Root,
        encoding: "utf-8",
    });
    Assert.notEqual(FailingRun.status, 0, "an explicit assertion failure must produce nonzero status");
    Assert.match(FailingRun.stderr, /FAILED: explicit failure path/);

    for (const [Probe, ExpectedStatus] of [
        ["--oom-probe", 91],
        ["--bounds-probe", 92],
        ["--iterator-probe", 92],
        ["--iterator-end-probe", 92],
        ["--alignment-probe", 92],
        ["--span-bounds-probe", 92],
        ["--hash-oom-probe", 91],
        ["--hash-iterator-probe", 92],
        ["--string-oom-probe", 91],
        ["--string-view-probe", 92],
        ["--function-probe", 92],
        ["--ownership-oom-probe", 91],
        ["--function-oom-probe", 91],
        ["--math-bounds-probe", 92],
    ] as const) {
        const ProbeRun = spawnSync(Executable, [Probe], {
            cwd: Root,
            encoding: "utf-8",
        });
        Assert.equal(
            ProbeRun.status,
            ExpectedStatus,
            `${Probe} must terminate through the injected non-returning handler\n${ProbeRun.stdout}\n${ProbeRun.stderr}`,
        );
    }
});
