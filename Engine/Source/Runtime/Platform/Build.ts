import {
    ModuleBuild,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";

export default class PlatformBuild extends ModuleBuild {
    public readonly Name = "Platform";

    public Configure(Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.PublicDependencies.push("Core");

        if (Target.Platform === "Mac") {
            Configuration.SystemFrameworks.push("AppKit", "QuartzCore");
            Configuration.SourceFilesExclude.push("Private/Windows/WindowsPlatformWindow.h");
            Configuration.SourceFilesExclude.push("Private/Windows/WindowsPlatformWindow.cpp");
        } else {
            Configuration.LibraryFiles.push("user32.lib");
            Configuration.SourceFilesExclude.push("Private/Mac/MacPlatformWindow.h");
            Configuration.SourceFilesExclude.push("Private/Mac/MacPlatformWindow.mm");
        }
    }
}
