import * as Assert from "node:assert/strict";
import { createHash } from "node:crypto";
import * as Path from "node:path";
import * as Fs from "node:fs/promises";
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
