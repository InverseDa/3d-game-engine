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
            "Reflection",
            "Renderer",
            "World",
        );
        Configuration.CustomActions.push({
            Id: "GenerateReflection",
            Inputs: ["[module.SourceRoot]/Private/DemoSettings.h"],
            ImplicitInputs: [
                "[engine.Root]/Engine/Builder/Source/ReflectionGenerator/CommandLine.ts",
                "[engine.Root]/Engine/Builder/Source/ReflectionGenerator/ReflectionGenerator.ts",
                "[engine.Root]/Engine/Source/Runtime/Reflection/Public/Reflection/ReflectionMacros.h",
            ],
            Outputs: ["[module.Generated]/Reflection/DemoApplication.reflection.generated.cpp"],
            Command: [
                process.execPath,
                "--no-warnings",
                "--experimental-strip-types",
                "[engine.Root]/Engine/Builder/Source/ReflectionGenerator/CommandLine.ts",
                "--module", "DemoApplication",
                "--module-root", "[module.SourceRoot]",
                "--entry-header", "DemoSettings.h",
                "--output", "[module.Generated]/Reflection/DemoApplication.reflection.generated.cpp",
                "--input", "[module.SourceRoot]/Private/DemoSettings.h",
            ],
            Description: "REFLECT DemoApplication",
            RunBeforeCompile: true,
        });
    }
}
