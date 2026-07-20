import * as Path from "node:path";
import * as Fs from "node:fs/promises";
import type { ModuleConfiguration } from "../Configuration/Types.ts";

const SourceExtensions = [".cpp", ".cc", ".c", ".mm", ".m"];

export class SourceScanner {
    async Scan(SourceRoot: string, Conf: ModuleConfiguration): Promise<string[]> {
        const Files = await this.Walk(SourceRoot);
        return Files.filter((F) => {
            const Ext = Path.extname(F).toLowerCase();
            if (!SourceExtensions.includes(Ext)) return false;
            const Rel = Path.relative(SourceRoot, F).replace(/\\/g, "/");
            for (const Exclude of Conf.SourceFilesExclude) {
                if (Rel === Exclude.replace(/\\/g, "/")) return false;
            }
            for (const Pattern of Conf.SourceFilesExcludeRegex) {
                if (new RegExp(Pattern).test(Rel)) return false;
            }
            return true;
        });
    }

    private async Walk(Dir: string): Promise<string[]> {
        const Results: string[] = [];
        let Entries: import("node:fs").Dirent[];
        try {
            Entries = await Fs.readdir(Dir, { withFileTypes: true });
        } catch {
            return Results;
        }
        for (const Entry of Entries) {
            const Full = Path.join(Dir, Entry.name);
            if (Entry.isDirectory()) {
                const Sub = await this.Walk(Full);
                Results.push(...Sub);
            } else if (Entry.isFile()) {
                Results.push(Full);
            }
        }
        return Results;
    }
}
