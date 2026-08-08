import {
    ModuleBuild,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";

export default class DemoApplicationBuild extends ModuleBuild {
    public readonly Name = "DemoApplication";

    public Configure(_Target: Target, Configuration: ModuleConfiguration): void {
        // These implementation dependencies are public for static-link reachability
        // in Game builds. DemoApplication's public C++ surface only includes the
        // backend-neutral Application contract.
        Configuration.PublicDependencies.push(
            "Application",
            "Core",
            "Platform",
            "Spdlog",
            "RAL",
            "RFG",
            "Renderer",
            "World",
        );
    }
}
