import {
    ModuleBuild,
    OutputType,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";

export default class GlmBuild extends ModuleBuild {
    public readonly Name = "Glm";
    public override readonly ThirdParty = true;

    public Configure(_Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.Output = OutputType.None;
        Configuration.SourceFilesExcludeRegex.push(".*\\.cpp$");
        Configuration.IncludePaths.push("[module.SourceRoot]/include");
        Configuration.Defines.LE_USE_GLM = "1";
        Configuration.ExportDefines.LE_USE_GLM = "1";
    }
}
