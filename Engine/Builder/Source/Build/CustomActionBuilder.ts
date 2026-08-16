import type { BuildAction, ResolvedTarget } from "../Configuration/Types.ts";
import type { ResolvedModule } from "../Graph/DependencyGraph.ts";
import type { EnginePaths } from "../Project/EnginePaths.ts";
import { ValidateAndSortActions } from "./ActionGraph.ts";
import { ResolveCustomActions } from "./CustomActionResolver.ts";

export function BuildCustomActionGraph(
    Paths: EnginePaths,
    BuildTarget: ResolvedTarget,
    Modules: ResolvedModule[],
): BuildAction[] {
    return ValidateAndSortActions(Modules.flatMap((Module) =>
        ResolveCustomActions(Paths, BuildTarget, Module)));
}
