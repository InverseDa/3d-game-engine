import * as Fs from "node:fs";
import * as Path from "node:path";
import {
    ModuleBuild,
    OutputType,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";

export default class VulkanBuild extends ModuleBuild {
    public readonly Name = "Vulkan";
    public override readonly ThirdParty = true;

    public Configure(Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.Output = OutputType.None;
        Configuration.IncludePaths.push("[module.SourceRoot]/Include");

        if (Target.Platform === "Win64") {
            const Sdk = process.env.VULKAN_SDK;
            if (!Sdk) {
                return;
            }

            const LibraryPath = Path.join(Sdk, "Lib");
            if (Fs.existsSync(LibraryPath)) {
                Configuration.LibraryPaths.push(LibraryPath);
                Configuration.LibraryFiles.push("vulkan-1.lib");
            }
            return;
        }

        if (Target.Platform === "Mac") {
            const Sdk = process.env.VULKAN_SDK;
            const LibraryPath = Sdk ? Path.join(Sdk, "lib") : "";
            if (LibraryPath && Fs.existsSync(LibraryPath)) {
                Configuration.LibraryPaths.push(LibraryPath);
                Configuration.LibraryFiles.push("vulkan");
            }
        }
    }
}
