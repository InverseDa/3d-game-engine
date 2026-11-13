import {
    ModuleBuild,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";

export default class RHIBuild extends ModuleBuild {
    public readonly Name = "RHI";

    public Configure(_Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.PublicDependencies.push("Core", "Platform", "Spdlog", "Vulkan");
    }
}
