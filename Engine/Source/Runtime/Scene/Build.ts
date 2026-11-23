import {
    ModuleBuild,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";

export default class SceneBuild extends ModuleBuild {
    public readonly Name = "Scene";

    public Configure(_Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.PublicDependencies.push("Core", "World", "Asset", "FileSystem");
    }
}
