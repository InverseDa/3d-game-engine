import * as Fs from "node:fs";
import * as Path from "node:path";
import {
    ModuleBuild,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";

export default class RHIBuild extends ModuleBuild {
    public readonly Name = "RHI";

    public Configure(Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.PublicDependencies.push("Core", "Platform", "Spdlog");
        Configuration.PrivateDependencies.push("Vulkan");

        // The loader is a link input for RHI.dll or for the final consumer of static RHI.
        const Sdk = process.env.VULKAN_SDK;
        if (Target.Platform === "Win64" && Sdk) {
            const LibraryPath = Path.join(Sdk, "Lib");
            if (Fs.existsSync(LibraryPath)) {
                Configuration.LibraryPaths.push(LibraryPath);
                Configuration.LibraryFiles.push("vulkan-1.lib");
            }
        } else if (Target.Platform === "Mac") {
            const CandidateLibraryPaths = [
                Sdk ? Path.join(Sdk, "lib") : "",
                "/opt/homebrew/opt/vulkan-loader/lib",
                "/opt/homebrew/lib",
                "/usr/local/lib",
            ];
            for (const LibraryPath of CandidateLibraryPaths) {
                if (LibraryPath && Fs.existsSync(Path.join(LibraryPath, "libvulkan.dylib"))) {
                    Configuration.LibraryPaths.push(LibraryPath);
                    Configuration.LibraryFiles.push("vulkan");
                    break;
                }
            }
        }
    }
}
