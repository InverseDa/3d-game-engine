import { DeriveAPIMacro, type APIMacroResult } from "../Build/APIMacro.ts";
import { CreateModuleConfiguration } from "../Configuration/ModuleBuild.ts";
import type {
    DependencyRef,
    ModuleConfiguration,
    ModuleInstance,
    OutputType,
    ResolvedTarget,
} from "../Configuration/Types.ts";

export interface ResolvedModule {
    Instance: ModuleInstance;
    Configuration: ModuleConfiguration;
    APIMacro: APIMacroResult;
}

function GetDependencyName(Dependency: DependencyRef): string {
    return typeof Dependency === "string" ? Dependency : Dependency.Name;
}

function IsWithoutLinking(Dependency: DependencyRef): boolean {
    return typeof Dependency === "object" && Dependency.WithoutLinking === true;
}

export class DependencyGraph {
    private readonly ResolvedModules = new Map<string, ResolvedModule>();
    private readonly AllModules: ModuleInstance[];
    private readonly BuildTarget: ResolvedTarget;

    public constructor(
        AllModules: ModuleInstance[],
        BuildTarget: ResolvedTarget,
    ) {
        this.AllModules = AllModules;
        this.BuildTarget = BuildTarget;
    }

    public Build(): ResolvedModule[] {
        this.ResolveAll();
        this.Validate();
        this.ValidateEntryModule();
        return this.TopologicalSort();
    }

    public Get(Name: string): ResolvedModule | undefined {
        return this.ResolvedModules.get(Name);
    }

    public GetLinkDependencies(Module: ResolvedModule): string[] {
        const Libraries: string[] = [];
        const Visited = new Set<string>();
        const Visit = (CurrentModule: ResolvedModule): void => {
            for (const Dependency of CurrentModule.Configuration.PublicDependencies) {
                if (IsWithoutLinking(Dependency)) {
                    continue;
                }

                const Name = GetDependencyName(Dependency);
                if (Visited.has(Name)) {
                    continue;
                }
                Visited.add(Name);

                const Resolved = this.ResolvedModules.get(Name);
                if (!Resolved) {
                    continue;
                }
                if (Resolved.Configuration.Output === ("Lib" as OutputType)) {
                    Libraries.push(Name);
                }
                Visit(Resolved);
            }
        };

        Visit(Module);
        return Libraries;
    }

    public GetTransitiveExportDefines(Module: ResolvedModule): Record<string, string> {
        const Defines: Record<string, string> = {};
        const Visited = new Set<string>();
        const Visit = (CurrentModule: ResolvedModule): void => {
            for (const Dependency of CurrentModule.Configuration.PublicDependencies) {
                const Name = GetDependencyName(Dependency);
                if (Visited.has(Name)) {
                    continue;
                }
                Visited.add(Name);

                const Resolved = this.ResolvedModules.get(Name);
                if (!Resolved) {
                    continue;
                }
                Object.assign(Defines, Resolved.Configuration.ExportDefines, Resolved.Configuration.Defines);
                Visit(Resolved);
            }
        };

        Visit(Module);
        return Defines;
    }

    private ResolveAll(): void {
        const DeclaredModules = new Set(this.BuildTarget.Descriptor.Modules);
        const DiscoveredNames = new Set(this.AllModules.map((Module) => Module.Descriptor.Name));
        for (const ModuleName of DeclaredModules) {
            if (!DiscoveredNames.has(ModuleName)) {
                if (ModuleName === this.BuildTarget.Descriptor.EntryModule) {
                    throw new Error(
                        `Target "${this.BuildTarget.Descriptor.Name}" entry module "${ModuleName}" was not discovered.`,
                    );
                }
                throw new Error(
                    `Target "${this.BuildTarget.Descriptor.Name}" declares unknown module "${ModuleName}".`,
                );
            }
        }

        for (const Module of this.AllModules) {
            if (!DeclaredModules.has(Module.Descriptor.Name)) {
                continue;
            }
            const Configuration = CreateModuleConfiguration();
            Module.Descriptor.Configure(this.BuildTarget.Target, Configuration);

            if (Module.Descriptor.ThirdParty) {
                this.ResolvedModules.set(Module.Descriptor.Name, {
                    Instance: Module,
                    Configuration,
                    APIMacro: { SelfDefine: "", ExportDefine: "", Output: Configuration.Output },
                });
                continue;
            }

            const Macro = DeriveAPIMacro(Module.Descriptor.Name, this.BuildTarget);
            if (Configuration.Output === ("Lib" as OutputType)) {
                Configuration.Output = Macro.Output;
            }
            this.ApplyDefine(Configuration.Defines, Macro.SelfDefine);
            this.ApplyDefine(Configuration.ExportDefines, Macro.ExportDefine);
            this.ResolvedModules.set(Module.Descriptor.Name, {
                Instance: Module,
                Configuration,
                APIMacro: Macro,
            });
        }
    }

    private ApplyDefine(Defines: Record<string, string>, Definition: string): void {
        if (!Definition) {
            return;
        }

        const SeparatorIndex = Definition.indexOf("=");
        Defines[Definition.slice(0, SeparatorIndex)] = Definition.slice(SeparatorIndex + 1);
    }

    private Validate(): void {
        for (const [Name, Resolved] of this.ResolvedModules) {
            const Dependencies = [
                ...Resolved.Configuration.PublicDependencies,
                ...Resolved.Configuration.PrivateDependencies,
            ];
            for (const Dependency of Dependencies) {
                const DependencyName = GetDependencyName(Dependency);
                if (!this.ResolvedModules.has(DependencyName)) {
                    throw new Error(`Module "${Name}" depends on unknown module "${DependencyName}"`);
                }
            }
        }
    }

    private ValidateEntryModule(): void {
        const EntryModule = this.BuildTarget.Descriptor.EntryModule;
        if (!this.ResolvedModules.has(EntryModule)) {
            throw new Error(
                `Target "${this.BuildTarget.Descriptor.Name}" entry module "${EntryModule}" was not discovered.`,
            );
        }
    }

    private TopologicalSort(): ResolvedModule[] {
        const Visited = new Set<string>();
        const Visiting = new Set<string>();
        const Results: ResolvedModule[] = [];
        const Visit = (Name: string): void => {
            if (Visited.has(Name)) {
                return;
            }
            if (Visiting.has(Name)) {
                throw new Error(`Circular dependency detected involving module "${Name}"`);
            }

            Visiting.add(Name);
            const Resolved = this.ResolvedModules.get(Name);
            if (!Resolved) {
                return;
            }
            for (const Dependency of [
                ...Resolved.Configuration.PublicDependencies,
                ...Resolved.Configuration.PrivateDependencies,
            ]) {
                Visit(GetDependencyName(Dependency));
            }

            Visiting.delete(Name);
            Visited.add(Name);
            Results.push(Resolved);
        };

        for (const Name of this.ResolvedModules.keys()) {
            Visit(Name);
        }
        return Results;
    }
}
