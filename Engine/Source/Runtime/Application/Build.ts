import {
    ModuleBuild,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";

export default class ApplicationBuild extends ModuleBuild {
    public readonly Name = "Application";

    public Configure(_Target: Target, Configuration: ModuleConfiguration): void {
        // Platform is link-visible so static Game builds carry the private clock
        // implementation through the final executable link.
        Configuration.PublicDependencies.push("Core", "Platform");
    }
}
