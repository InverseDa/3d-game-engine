import {
    ModuleBuild,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";

export default class AssetBuild extends ModuleBuild {
    public readonly Name = "Asset";

    public Configure(_Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.PublicDependencies.push("Core");
    }
}
