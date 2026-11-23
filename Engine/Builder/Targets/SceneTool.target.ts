import { DefineTarget, Optimization, Platform, TargetType } from "../Runtime/Api.ts";

export default DefineTarget({
    Name: "SceneTool",
    Modules: [
        "Spdlog", "Core", "Asset", "Reflection", "World", "FileSystem",
        "AssetFormat", "AssetLoading", "Scene", "SceneTool",
    ],
    EntryModule: "SceneTool",
    Matrix: Object.values(Platform).flatMap((ConcretePlatform) =>
        Object.values(Optimization).map((ConcreteOptimization) => ({
            Platform: ConcretePlatform,
            Optimization: ConcreteOptimization,
            TargetType: TargetType.Program,
        })),
    ),
    OutputName: () => "SceneTool",
});
