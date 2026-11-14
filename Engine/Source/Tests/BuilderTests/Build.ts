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
        Configuration.PublicDependencies.push(
            "Application",
            "Asset",
            "AssetFormat",
            "AssetLoading",
            "ContentIO",
            "Core",
            "Platform",
            "RHI",
            "Reflection",
            "Renderer",
            "Scene",
            "World",
            "WorldRenderer",
        );
        if (Target.TargetType === TargetType.Test) {
            Configuration.IncludePaths.push("[engine.Root]/Engine/Source/Runtime/DemoApplication/Private/Camera");
            Configuration.CustomActions.push({
                Id: "GenerateReflection",
                Inputs: ["[module.SourceRoot]/Private/ReflectionGeneratedFixture.h"],
                ImplicitInputs: [
                    "[engine.Root]/Engine/Builder/Source/ReflectionGenerator/CommandLine.ts",
                    "[engine.Root]/Engine/Builder/Source/ReflectionGenerator/ReflectionGenerator.ts",
                    "[engine.Root]/Engine/Source/Runtime/Reflection/Public/Reflection/ReflectionMacros.h",
                ],
                Outputs: ["[module.Generated]/Reflection/BuilderTests.reflection.generated.cpp"],
                Command: [
                    process.execPath,
                    "--no-warnings",
                    "--experimental-strip-types",
                    "[engine.Root]/Engine/Builder/Source/ReflectionGenerator/CommandLine.ts",
                    "--module", "BuilderTests",
                    "--module-root", "[module.SourceRoot]",
                    "--api-macro", "BUILDERTESTS_API",
                    "--entry-header", "ReflectionGeneratedFixture.h",
                    "--output", "[module.Generated]/Reflection/BuilderTests.reflection.generated.cpp",
                    "--input", "[module.SourceRoot]/Private/ReflectionGeneratedFixture.h",
                ],
                Description: "REFLECT BuilderTests",
                RunBeforeCompile: true,
            });
        }
    }
}
