import {
    DefineTarget,
    Optimization,
    Platform,
    TargetType,
} from "../Runtime/Api.ts";

export default DefineTarget({
    Name: "LimitlessTests",
    Modules: ["Spdlog", "Core", "Platform", "Vulkan", "RAL", "RFG", "Renderer", "Application", "Reflection", "World", "BuilderTests"],
    EntryModule: "BuilderTests",
    Matrix: Object.values(Platform).flatMap((ConcretePlatform) =>
        Object.values(Optimization).map((ConcreteOptimization) => ({
            Platform: ConcretePlatform,
            Optimization: ConcreteOptimization,
            TargetType: TargetType.Test,
        })),
    ),
    OutputName: (Target) => `LimitlessTests${Target.Optimization}`,
});
