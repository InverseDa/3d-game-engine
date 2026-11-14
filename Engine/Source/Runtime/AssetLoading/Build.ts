import {
    ModuleBuild,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";

export default class AssetLoadingBuild extends ModuleBuild {
    public readonly Name = "AssetLoading";

    public Configure(_Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.PublicDependencies.push("Core", "Asset", "AssetFormat", "ContentIO");
    }
}
