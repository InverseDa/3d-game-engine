import {
    DefineTarget,
    Optimization,
    Platform,
    TargetType,
} from "../Runtime/Api.ts";

export default DefineTarget({
    Name: "AssetImport",
    Modules: ["Spdlog", "Core", "Asset", "AssetFormat", "FileSystem", "AssetImport"],
    EntryModule: "AssetImport",
    Matrix: Object.values(Platform).flatMap((ConcretePlatform) =>
        Object.values(Optimization).map((ConcreteOptimization) => ({
            Platform: ConcretePlatform,
            Optimization: ConcreteOptimization,
            TargetType: TargetType.Program,
        })),
    ),
    OutputName: () => "AssetImport",
});
