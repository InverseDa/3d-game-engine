import { OutputType, Platform, TargetType } from "../Configuration/Types.ts";
import type { ResolvedTarget } from "../Configuration/Types.ts";

export interface APIMacroResult {
    SelfDefine: string;
    ExportDefine: string;
    Output: OutputType;
}

export function DeriveAPIMacro(ModuleName: string, BuildTarget: ResolvedTarget): APIMacroResult {
    const Macro = `${ModuleName.toUpperCase()}_API`;

    if (ModuleName === BuildTarget.Descriptor.EntryModule) {
        return { SelfDefine: `${Macro}=`, ExportDefine: `${Macro}=`, Output: OutputType.Lib };
    }

    if (BuildTarget.Target.TargetType === TargetType.Editor
        && BuildTarget.Target.Platform === Platform.Win64) {
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
