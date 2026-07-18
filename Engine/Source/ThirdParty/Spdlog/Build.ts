import {
    ModuleBuild,
    OutputType,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";

export default class SpdlogBuild extends ModuleBuild {
    public readonly Name = "Spdlog";
    public override readonly ThirdParty = true;

    public Configure(Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.Output = OutputType.Lib;
        Configuration.IncludePaths.push("[module.SourceRoot]/include");
        Configuration.Defines.SPDLOG_COMPILED_LIB = "1";
        Configuration.ExportDefines.SPDLOG_COMPILED_LIB = "1";

        if (Target.Platform === "Mac") {
            Configuration.Defines.SPDLOG_NO_EXCEPTIONS = "1";
            Configuration.ExportDefines.SPDLOG_NO_EXCEPTIONS = "1";
        }
    }
}
