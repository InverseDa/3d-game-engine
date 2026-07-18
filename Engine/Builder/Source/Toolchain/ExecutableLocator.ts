import * as ChildProcess from "node:child_process";
import * as Fs from "node:fs";
import * as Path from "node:path";

export function FindExecutable(Name: string, OverridePath?: string): string | null {
    if (OverridePath) {
        const ResolvedOverride = Path.resolve(OverridePath);
        if (!Fs.existsSync(ResolvedOverride)) {
            throw new Error(`Configured ${Name} path does not exist: ${ResolvedOverride}`);
        }
        return ResolvedOverride;
    }

    const Locator = process.platform === "win32" ? "where.exe" : "which";
    const Result = ChildProcess.spawnSync(Locator, [Name], { encoding: "utf-8" });
    if (Result.status === 0 && Result.stdout) {
        const Candidate = Result.stdout.split(/\r?\n/).find(Boolean)?.trim();
        if (Candidate && Fs.existsSync(Candidate)) {
            return Candidate;
        }
    }
    return FindExecutableOnPersistedWindowsPath(Name);
}

export function FindNinjaExecutable(): string | null {
    return FindExecutable("ninja", process.env.LIMITLESS_BUILDER_NINJA);
}

function FindExecutableOnPersistedWindowsPath(Name: string): string | null {
    if (process.platform !== "win32") {
        return null;
    }

    const RegistryKeys = [
        "HKCU\\Environment",
        "HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
    ];
    for (const RegistryKey of RegistryKeys) {
        const Result = ChildProcess.spawnSync("reg.exe", ["query", RegistryKey, "/v", "Path"], {
            encoding: "utf-8",
        });
        const PathValue = Result.stdout?.split(/\r?\n/)
            .find((Line) => /\sPath\s+REG_.*SZ\s+/.test(Line))
            ?.replace(/^.*\sREG_.*SZ\s+/, "")
            .trim();
        if (!PathValue) {
            continue;
        }

        for (const Directory of ExpandEnvironmentVariables(PathValue).split(";")) {
            const Candidate = Path.join(Directory, Name);
            if (Fs.existsSync(Candidate)) {
                return Candidate;
            }
        }
    }
    return null;
}

function ExpandEnvironmentVariables(Value: string): string {
    return Value.replace(/%([^%]+)%/g, (_Match, VariableName: string) =>
        process.env[VariableName] ?? `%${VariableName}%`,
    );
}
