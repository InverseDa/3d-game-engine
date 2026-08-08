#!/usr/bin/env node
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { formatDiagnostic, scanRepository } from "./CppGovernance.ts";

const builderDirectory = resolve(fileURLToPath(new URL("../..", import.meta.url)));
const repositoryRoot = resolve(builderDirectory, "../..");
const diagnostics = await scanRepository(repositoryRoot);
for (const value of diagnostics) { console.error(formatDiagnostic(value)); }
if (diagnostics.length > 0) {
    console.error(`C++ governance failed with ${diagnostics.length} violation(s).`);
    process.exitCode = 1;
} else {
    console.log("C++ governance passed (0 violations).");
}
