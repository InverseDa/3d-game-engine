import { OutputType, Platform, TargetType } from "../Configuration/Types.ts";
import type { Target } from "../Configuration/Types.ts";

export interface APIMacroResult {
    SelfDefine: string;
    ExportDefine: string;
    Output: OutputType;
}

export function DeriveAPIMacro(ModuleName: string, Target: Target): APIMacroResult {
    const Macro = `${ModuleName.toUpperCase()}_API`;

    if (ModuleName === "Launch") {
        return { SelfDefine: `${Macro}=`, ExportDefine: `${Macro}=`, Output: OutputType.Lib };
    }

    if (Target.TargetType === TargetType.Editor && Target.Platform === Platform.Win64) {
        return {
            SelfDefine: `${Macro}=__declspec(dllexport)`,
            ExportDefine: `${Macro}=__declspec(dllimport)`,
            Output: OutputType.Dll,
        };
    }

    return { SelfDefine: `${Macro}=`, ExportDefine: `${Macro}=`, Output: OutputType.Lib };
}

export function APIMacroName(ModuleName: string): string {
    return `${ModuleName.toUpperCase()}_API`;
}
