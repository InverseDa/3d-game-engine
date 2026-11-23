import {
    ModuleBuild,
    OutputType,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";

export default class VulkanBuild extends ModuleBuild {
    public readonly Name = "Vulkan";
    public override readonly ThirdParty = true;

    public Configure(_Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.Output = OutputType.None;
        Configuration.IncludePaths.push("[module.SourceRoot]/Include");
    }
}
