import {
    ModuleBuild,
    Platform,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";

export default class CoreBuild extends ModuleBuild {
    public readonly Name = "Core";

    public Configure(Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.PublicDependencies.push("Spdlog");
        if (Target.Platform === Platform.Win64) {
            Configuration.LibraryFiles.push("bcrypt.lib");
        } else if (Target.Platform === Platform.Mac) {
            Configuration.SystemFrameworks.push("Security");
        }
    }
}
