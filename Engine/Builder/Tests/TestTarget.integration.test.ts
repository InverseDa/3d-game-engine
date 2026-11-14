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
    const GameArtifacts = [
        Path.join(GameIntermediate, "build.ninja"),
        Path.join(GameIntermediate, "compile_commands.json"),
        Path.join(GameRoot, "Core.lib"),
        Path.join(GameRoot, "LimitlessGame.exe"),
    ];
    const GameBeforeEditor = await HashFiles(GameArtifacts);
    const Demo = Path.join(GameRoot, "DemoContent");
    const SceneTool = Path.join(GameIntermediate,
        "Generated/DemoApplication/ContentTools/Binaries/Win64/Debug/SceneTool/Program/SceneTool.exe");
    const Scratch = await Fs.mkdtemp(Path.join(Os.tmpdir(), "limitless-textured-scene-"));
    try {
        await Fs.cp(Demo, Scratch, { recursive: true });
        const SceneFile = Path.join(Scratch, "Scene.json");
        const AssetRoot = Path.join(Scratch, "Assets");
        const Check = (File = SceneFile) => spawnSync(SceneTool, ["check", File, "Game", AssetRoot], {
            cwd: Scratch, encoding: "utf-8",
        });
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
        CameraEntity.camera.verticalFieldOfViewRadians = 0.9;
        CameraEntity.transform.rotation = [0, 0, 0, 0];
        await CheckCameraFixture("CameraInvalidRotation.json", "rotation");

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
    const EditorArtifacts = [
        Path.join(EditorIntermediate, "build.ninja"),
        Path.join(EditorIntermediate, "compile_commands.json"),
        Path.join(EditorRoot, "Core.dll"),
        Path.join(EditorRoot, "Core.lib"),
        Path.join(EditorRoot, "LimitlessEditor.exe"),
    ];
    const GameAndEditorArtifacts = [...GameArtifacts, ...EditorArtifacts];
    const BeforeTest = await HashFiles(GameAndEditorArtifacts);

    const TestBuild = RunBuild(Builder, Root, "LimitlessTests", "Test");
    Assert.equal(TestBuild.status, 0, `${TestBuild.stdout}\n${TestBuild.stderr}`);
    const AfterTest = await HashFiles(GameAndEditorArtifacts);
    Assert.deepEqual(AfterTest, BeforeTest, "Test must not modify Game or Editor graphs/artifacts");
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
