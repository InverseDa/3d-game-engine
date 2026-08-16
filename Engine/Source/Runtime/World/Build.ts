import {
    ModuleBuild,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";

export default class WorldBuild extends ModuleBuild {
    public readonly Name = "World";

    public Configure(_Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.PublicDependencies.push("Core", "RAL", "Reflection", "Renderer");
        Configuration.CustomActions.push({
            Id: "GenerateReflection",
            Inputs: ["[module.SourceRoot]/Public/World/World.h"],
            ImplicitInputs: [
                "[engine.Root]/Engine/Builder/Source/ReflectionGenerator/CommandLine.ts",
                "[engine.Root]/Engine/Builder/Source/ReflectionGenerator/ReflectionGenerator.ts",
                "[engine.Root]/Engine/Source/Runtime/Reflection/Public/Reflection/ReflectionMacros.h",
            ],
            Outputs: ["[module.Generated]/Reflection/World.reflection.generated.cpp"],
            Command: [
                process.execPath,
                "--no-warnings",
                "--experimental-strip-types",
                "[engine.Root]/Engine/Builder/Source/ReflectionGenerator/CommandLine.ts",
                "--module", "World",
                "--module-root", "[module.SourceRoot]",
                "--api-macro", "WORLD_API",
                "--entry-header", "World/WorldReflection.h",
                "--output", "[module.Generated]/Reflection/World.reflection.generated.cpp",
                "--input", "[module.SourceRoot]/Public/World/World.h",
            ],
            Description: "REFLECT World",
            RunBeforeCompile: true,
        });
    }
}
