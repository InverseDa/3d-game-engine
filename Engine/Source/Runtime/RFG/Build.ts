import {
    ModuleBuild,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";

export default class RFGBuild extends ModuleBuild {
    public readonly Name = "RFG";

    public Configure(_Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.PublicDependencies.push("Core", "Spdlog", "RAL");
    }
}
