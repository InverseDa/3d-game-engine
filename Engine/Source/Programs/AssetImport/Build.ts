import {
    ModuleBuild,
    OutputType,
    TargetType,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";

export default class AssetImportBuild extends ModuleBuild {
    public readonly Name = "AssetImport";

    public Configure(Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.Output = Target.TargetType === TargetType.Program
            ? OutputType.Exe
            : OutputType.None;
        Configuration.PrivateDependencies.push("Core", "Asset", "AssetFormat", "FileSystem", "Scene", "GltfSourceCodecs");
    }
}
