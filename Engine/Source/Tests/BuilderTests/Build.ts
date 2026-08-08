import {
    ModuleBuild,
    OutputType,
    type ModuleConfiguration,
    type Target,
    TargetType,
} from "../../../Builder/Runtime/Api.ts";

export default class BuilderTestsBuild extends ModuleBuild {
    public readonly Name = "BuilderTests";

    public Configure(Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.Output = Target.TargetType === TargetType.Test
            ? OutputType.Exe
            : OutputType.None;
        Configuration.PublicDependencies.push("Application", "Core", "Platform", "RAL");
    }
}
