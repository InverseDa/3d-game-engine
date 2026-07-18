import {
    ModuleBuild,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";

export default class RendererBuild extends ModuleBuild {
    public readonly Name = "Renderer";

    public Configure(_Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.PublicDependencies.push("Core", "RAL", "Vulkan", "RFG");
    }
}
