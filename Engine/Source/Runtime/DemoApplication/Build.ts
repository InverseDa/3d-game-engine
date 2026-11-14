import {
    ModuleBuild,
    Platform,
    TargetType,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";
import { readFileSync, readdirSync } from "node:fs";
import * as Path from "node:path";

export default class DemoApplicationBuild extends ModuleBuild {
    public readonly Name = "DemoApplication";

    public Configure(Target: Target, Configuration: ModuleConfiguration): void {
        // These implementation dependencies are public for static-link reachability
        // in Game builds. DemoApplication's public C++ surface only includes the
        // backend-neutral Application contract.
        Configuration.PublicDependencies.push(
            "Application",
            "Asset",
            "AssetLoading",
            "Core",
            "Platform",
            "Spdlog",
            "RHI",
            "RFG",
            "Reflection",
            "Renderer",
            "Scene",
            "World",
            "WorldRenderer",
        );
        if (Target.Platform === Platform.Win64 &&
            (Target.TargetType === TargetType.Game || Target.TargetType === TargetType.Editor)) {
            const Samples = [
                { Name: "AssetImport", Destination: "[engine.Binaries]/DemoContent" },
                { Name: "DirectionalLight", Destination: "[engine.Binaries]/DirectionalLightContent" },
            ].map(({ Name, Destination }) => {
                const Source = `[engine.Root]/Engine/Content/Examples/${Name}`;
                const Manifest = JSON.parse(readFileSync(new URL(
                    `../../../Content/Examples/${Name}/Imports.json`, import.meta.url), "utf-8"));
                const Assets = Manifest.assets as { source: string; path: string }[];
                const Cooked = Assets.map((Asset) => {
                    const Prefix = `${Manifest.mount}/`;
                    if (!Asset.path.startsWith(Prefix) || Path.basename(Asset.source) !== Asset.source) {
                        throw new Error("Invalid Demo import manifest entry");
                    }
                    const Relative = Asset.path.slice(Prefix.length);
                    if (!Relative.split("/").every((Segment) => /^[A-Za-z0-9_-]+$/.test(Segment))) {
                        throw new Error(`Invalid Demo asset path ${Asset.path}`);
                    }
                    return `${Destination}/Assets/${Relative}.leasset`;
                });
                return {
                    Source, Destination,
                    Inputs: [`${Source}/Imports.json`, `${Source}/Scene.json`, ...Assets.map((Asset) => `${Source}/${Asset.source}`)],
                    Outputs: [`${Destination}/Scene.json`, ...Cooked],
                };
            });
            const ToolSources = [
                "Runtime/Core", "Runtime/Asset", "Runtime/AssetFormat", "Runtime/AssetLoading",
                "Runtime/ContentIO", "Runtime/Reflection", "Runtime/World", "Runtime/Scene",
                "Programs/AssetImport", "Programs/SceneTool",
            ].flatMap((Directory) => {
                const Root = new URL(`../../${Directory}/`, import.meta.url);
                return readdirSync(Root, { recursive: true, withFileTypes: true })
                    .filter((Entry) => Entry.isFile())
                    .map((Entry) => Path.join(Entry.parentPath, Entry.name));
            }).sort();
            Configuration.CustomActions.push({
                Id: "StageDemoContent",
                Inputs: Samples.flatMap((Sample) => Sample.Inputs),
                ImplicitInputs: [
                    "[engine.Root]/Engine/Builder/Source/Content/StageDemoContent.ts",
                    "[engine.Root]/Engine/Builder/Targets/AssetImport.target.ts",
                    "[engine.Root]/Engine/Builder/Targets/SceneTool.target.ts",
                    "[engine.Root]/Engine/Builder/Source/Cli/CommandLine.ts",
                    "[engine.Root]/Engine/Builder/Source/Project/EnginePaths.ts",
                    ...ToolSources,
                ],
                Outputs: Samples.flatMap((Sample) => Sample.Outputs),
                Command: [
                    process.execPath, "--no-warnings", "--experimental-strip-types",
                    "[engine.Root]/Engine/Builder/Source/Content/StageDemoContent.ts",
                    "[engine.Root]", Target.Optimization, "[module.Generated]/ContentTools",
                    ...Samples.flatMap(({ Source, Destination }) => [Source, Destination]),
                ],
                Description: "CONTENT DemoApplication",
                RunBeforeCompile: true,
            });
        }
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
