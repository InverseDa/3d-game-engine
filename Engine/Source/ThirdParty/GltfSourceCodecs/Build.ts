import { ModuleBuild, OutputType, TargetType, type ModuleConfiguration, type Target } from "../../../Builder/Runtime/Api.ts";

export default class GltfSourceCodecsBuild extends ModuleBuild {
    public readonly Name = "GltfSourceCodecs";
    public override readonly ThirdParty = true;

    public Configure(Target: Target, Configuration: ModuleConfiguration): void {
        if (Target.TargetType !== TargetType.Program) {
            Configuration.Output = OutputType.None;
            return;
        }
        Configuration.Output = OutputType.Lib;
        Configuration.IncludePaths.push("[module.SourceRoot]/include");

        const DecoderOnly = {
            LODEPNG_NO_COMPILE_CPP: "1",
            LODEPNG_NO_COMPILE_DISK: "1",
            LODEPNG_NO_COMPILE_ENCODER: "1",
            LODEPNG_NO_COMPILE_ANCILLARY_CHUNKS: "1",
        };
        Object.assign(Configuration.Defines, DecoderOnly);
        Object.assign(Configuration.ExportDefines, DecoderOnly);
    }
}
