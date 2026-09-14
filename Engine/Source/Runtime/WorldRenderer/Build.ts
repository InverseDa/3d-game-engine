import {
    ModuleBuild,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";

export default class WorldRendererBuild extends ModuleBuild {
    public readonly Name = "WorldRenderer";

    public Configure(_Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.PublicDependencies.push("Core", "Asset", "Renderer", "World");
    }
}
