import {
    Optimization,
    Platform,
    TargetType,
    type ResolvedTarget,
    type Target,
    type TargetDescriptor,
} from "./Types.ts";

export function DefineTarget(Descriptor: TargetDescriptor): TargetDescriptor {
    return Descriptor;
}

export function IsTargetSupported(Descriptor: TargetDescriptor, Target: Target): boolean {
    return Array.isArray(Descriptor.Matrix) && Descriptor.Matrix.some((Entry) =>
        Entry.Platform === Target.Platform
        && Entry.Optimization === Target.Optimization
        && Entry.TargetType === Target.TargetType,
    );
}

export function ResolveTarget(Descriptor: TargetDescriptor, Target: Target): ResolvedTarget {
    if (!Descriptor || typeof Descriptor !== "object") {
        throw new Error("Target descriptor is missing.");
    }
    if (typeof Descriptor.Name !== "string" || Descriptor.Name.trim().length === 0) {
        throw new Error("Target descriptor Name must be a non-empty string.");
    }
    if (!/^[A-Za-z0-9][A-Za-z0-9._-]*$/.test(Descriptor.Name)) {
        throw new Error(
            `Target descriptor Name "${Descriptor.Name}" must be a safe identifier containing only letters, digits, '.', '_' or '-'.`,
        );
    }
    if (!Array.isArray(Descriptor.Modules) || Descriptor.Modules.length === 0) {
        throw new Error(`Target "${Descriptor.Name}" must declare at least one module.`);
    }
    if (Descriptor.Modules.some((ModuleName) =>
        typeof ModuleName !== "string" || ModuleName.trim().length === 0
    )) {
        throw new Error(`Target "${Descriptor.Name}" Modules must contain non-empty module names.`);
    }
    if (new Set(Descriptor.Modules).size !== Descriptor.Modules.length) {
        throw new Error(`Target "${Descriptor.Name}" must not list duplicate modules.`);
    }
    if (typeof Descriptor.EntryModule !== "string" || Descriptor.EntryModule.trim().length === 0) {
        throw new Error(`Target "${Descriptor.Name}" must declare a non-empty EntryModule.`);
    }
    if (!Descriptor.Modules.includes(Descriptor.EntryModule)) {
        throw new Error(
            `Target "${Descriptor.Name}" entry module "${Descriptor.EntryModule}" is not listed in Modules.`,
        );
    }
    if (!Array.isArray(Descriptor.Matrix) || !IsTargetSupported(Descriptor, Target)) {
        throw new Error(
            `Target "${Descriptor.Name}" does not support ${Target.Platform}/${Target.Optimization}/${Target.TargetType}.`,
        );
    }
    if (typeof Descriptor.OutputName !== "function") {
        throw new Error(`Target "${Descriptor.Name}" must provide an OutputName function.`);
    }

    let OutputName: unknown;
    try {
        OutputName = Descriptor.OutputName(Target);
    } catch (CaughtError) {
        const Reason = CaughtError instanceof Error ? CaughtError.message : String(CaughtError);
        throw new Error(`Target "${Descriptor.Name}" failed to resolve OutputName: ${Reason}`);
    }
    if (typeof OutputName !== "string" || OutputName.trim().length === 0) {
        throw new Error(`Target "${Descriptor.Name}" OutputName must resolve to a non-empty string.`);
    }
    if (OutputName !== OutputName.trim()
        || OutputName === "."
        || OutputName === ".."
        || /[<>:"/\\|?*\u0000-\u001f]/.test(OutputName)) {
        throw new Error(
            `Target "${Descriptor.Name}" OutputName "${OutputName}" must be a file name without a path.`,
        );
    }

    return { Descriptor, Target, OutputName };
}

export function SelectTargetDescriptor(
    Descriptors: TargetDescriptor[],
    Target: Target,
    RequestedName?: string,
    FallbackDescriptor?: TargetDescriptor,
): TargetDescriptor {
    // Keep the transitional Game/Editor fallback available while formal
    // Program/Test descriptors are introduced.
    const Candidates = [...Descriptors];
    if (Candidates.length === 0 && !FallbackDescriptor) {
        throw new Error("No target descriptors were found.");
    }

    const DuplicateNames = Candidates
        .map((Candidate) => Candidate.Name)
        .filter((Name, Index, Names) => Names.indexOf(Name) !== Index);
    if (DuplicateNames.length > 0) {
        throw new Error(`Duplicate target descriptor name: ${DuplicateNames[0]}`);
    }

    if (RequestedName) {
        const Requested = Candidates.find((Candidate) => Candidate.Name === RequestedName)
            ?? (FallbackDescriptor?.Name === RequestedName ? FallbackDescriptor : undefined);
        if (!Requested) {
            throw new Error(
                `Unknown target "${RequestedName}". Available targets: ${[
                    ...Candidates.map((Candidate) => Candidate.Name),
                    ...(FallbackDescriptor ? [FallbackDescriptor.Name] : []),
                ].join(", ")}.`,
            );
        }
        return Requested;
    }

    const Compatible = Candidates.filter((Candidate) => IsTargetSupported(Candidate, Target));
    if (Compatible.length === 1) {
        return Compatible[0];
    }
    if (Compatible.length === 0) {
        if (FallbackDescriptor && IsTargetSupported(FallbackDescriptor, Target)) {
            return FallbackDescriptor;
        }
        throw new Error(
            `No target supports ${Target.Platform}/${Target.Optimization}/${Target.TargetType}.`,
        );
    }
    throw new Error(
        `Multiple targets support ${Target.Platform}/${Target.Optimization}/${Target.TargetType}; pass --target <name>.`,
    );
}

/**
 * Compatibility only: used for Game/Editor until the project provides formal
 * descriptors for those products.
 */
export function CreateLegacyTargetDescriptor(ModuleNames: string[]): TargetDescriptor {
    const Name = "Limitless";
    return {
        Name,
        Modules: ModuleNames,
        EntryModule: "Launch",
        Matrix: Object.values(Platform).flatMap((ConcretePlatform) =>
            Object.values(Optimization).flatMap((ConcreteOptimization) =>
                [TargetType.Game, TargetType.Editor].map((ConcreteTargetType) => ({
                    Platform: ConcretePlatform,
                    Optimization: ConcreteOptimization,
                    TargetType: ConcreteTargetType,
                })),
            ),
        ),
        OutputName: (ConcreteTarget) => `${Name}${ConcreteTarget.TargetType}`,
    };
}
