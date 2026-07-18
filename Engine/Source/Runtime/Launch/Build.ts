import {
    ModuleBuild,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";

export default class LaunchBuild extends ModuleBuild {
    public readonly Name = "Launch";

    public Configure(Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.PublicDependencies.push("Core", "Spdlog", "RAL", "RFG", "Renderer", "World");

        if (Target.Platform === "Mac") {
            Configuration.SystemFrameworks.push("AppKit", "QuartzCore");
            Configuration.SourceFilesExclude.push("Public/Windows/WindowsWindow.h");
            Configuration.SourceFilesExclude.push("Private/Windows/LaunchWindows.cpp");
            Configuration.SourceFilesExclude.push("Private/Windows/WindowsWindow.cpp");
        } else {
            Configuration.SourceFilesExclude.push("Private/Mac/MacWindow.mm");
        }
    }
}
