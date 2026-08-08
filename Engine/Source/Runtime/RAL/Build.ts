import {
    ModuleBuild,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";

export default class RALBuild extends ModuleBuild {
    public readonly Name = "RAL";

    public Configure(_Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.PublicDependencies.push("Core", "Platform", "Spdlog", "Vulkan");
    }
}
