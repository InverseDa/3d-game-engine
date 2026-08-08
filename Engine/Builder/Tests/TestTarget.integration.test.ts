import * as Assert from "node:assert/strict";
import * as Path from "node:path";
import { spawnSync } from "node:child_process";
import Test from "node:test";

const IsWindows = process.platform === "win32";

Test("LimitlessTests builds and reports success/failure through process exit status", {
    skip: !IsWindows,
}, () => {
    const Root = Path.resolve(import.meta.dirname, "..", "..", "..");
    const Builder = Path.join(Root, "Engine", "Builder", "LimitlessBuilder.bat");
    const Build = spawnSync(Builder, [
        "build",
        "--target", "LimitlessTests",
        "--platform", "Win64",
        "--config", "Debug",
        "--type", "Test",
    ], {
        cwd: Root,
        encoding: "utf-8",
        shell: true,
    });
    Assert.equal(Build.status, 0, `${Build.stdout}\n${Build.stderr}`);

    const Executable = Path.join(Root, "Engine", "Binaries", "Win64", "LimitlessTestsDebug.exe");
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
