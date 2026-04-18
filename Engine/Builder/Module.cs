using System.IO;
using System;
using Sharpmake;

namespace Limitless
{
    public abstract class ModuleRule : Sharpmake.Project
    {
        public ModuleRule() : base(typeof(TargetRule))
        {
            SourceRootPath = this.SharpmakeCsPath;
            SourceFilesExtensions.Add(".cs");
            AddTargets(
                new TargetRule(
                    Platform.win64,
                    DevEnv.vs2022,
                    Optimization.Debug | Optimization.Release,
                    TargetType.Game | TargetType.Editor
                ),
                new TargetRule(
                    Platform.mac,
                    DevEnv.xcode,
                    Optimization.Debug | Optimization.Release,
                    TargetType.Game | TargetType.Editor
                )
            );
        }

        [Configure]
        public virtual void ConfigureAll(Configuration conf, TargetRule target)
        {
            conf.ProjectFileName = "[project.Name]_[target.DevEnv]_[target.Platform]";
            conf.ProjectPath = DirectoryHelper.ProjectGenDir;

            conf.SolutionFolder = "Programs";

            // Keep source/execution charset consistent across modules.
            if (target.Platform == Platform.win64)
            {
                conf.AdditionalCompilerOptions.Add("/utf-8");
                conf.Options.Add(Options.Vc.Compiler.CppLanguageStandard.CPP17);
            }
            else if (target.Platform == Platform.mac)
            {
                conf.Options.Add(Options.XCode.Compiler.CppExceptions.Enable);
                conf.Options.Add(Options.XCode.Compiler.RTTI.Enable);
                conf.Options.Add(Options.XCode.Compiler.CppLanguageStandard.CPP17);
                conf.Options.Add(new Options.XCode.Compiler.Archs("arm64"));
                conf.Options.Add(new Options.XCode.Compiler.ValidArchs("arm64"));
            }

            conf.TargetPath = Path.Combine(DirectoryHelper.TmpDir, "Bin", "[project.Name]");
            conf.IntermediatePath = Path.Combine(DirectoryHelper.TmpDir, "Obj", "[project.Name]");

            conf.IncludePaths.Add("[project.SourceRootPath]/Public");
            conf.IncludePaths.Add("[project.SourceRootPath]/Private");

            // Platform defines
            if (target.Platform == Platform.win64)
            {
                conf.Defines.Add("PLATFORM_WINDOWS=1");
                conf.ExportDefines.Add("PLATFORM_WINDOWS=1");
            }
            else if (target.Platform == Platform.mac)
            {
                conf.Defines.Add("PLATFORM_MAC=1");
                conf.ExportDefines.Add("PLATFORM_MAC=1");
            }

            string configName = target.Optimization.ToString();
            if (target.TargetType == TargetType.Editor)
            {
                configName += " Editor";
            }
            else if (target.TargetType == TargetType.Game)
            {
                configName += " Game";
            }
            conf.Name = configName; 

            string apiMacro = this.Name.ToUpper() + "_API";
            if (this.Name == "Launch")
            {
                conf.Output = Configuration.OutputType.Lib;
            }
            else if (target.TargetType == TargetType.Editor && target.Platform == Platform.win64)
            {
                conf.Output = Configuration.OutputType.Dll;
                conf.Defines.Add($"{apiMacro}=__declspec(dllexport)");
                conf.ExportDefines.Add($"{apiMacro}=__declspec(dllimport)");
            }
            else
            {
                conf.Output = Configuration.OutputType.Lib;
                conf.Defines.Add($"{apiMacro}=");
                conf.ExportDefines.Add($"{apiMacro}=");
            };
        }
    }

    public abstract class ThirdPartyModuleRule : Sharpmake.Project
    {
        public ThirdPartyModuleRule() : base(typeof(TargetRule))
        {
            SourceRootPath = @"[project.SharpmakeCsPath]";
            SourceFilesExtensions.Add(".cs");
            AddTargets(
                new TargetRule(
                    Platform.win64,
                    DevEnv.vs2022,
                    Optimization.Debug | Optimization.Release,
                    TargetType.Game | TargetType.Editor
                ),
                new TargetRule(
                    Platform.mac,
                    DevEnv.xcode,
                    Optimization.Debug | Optimization.Release,
                    TargetType.Game | TargetType.Editor
                )
            );
        }

        [Configure]
        public virtual void ConfigureAll(Configuration conf, TargetRule target)
        {
            conf.ProjectFileName = "[project.Name]_[target.DevEnv]_[target.Platform]";
            conf.ProjectPath = DirectoryHelper.SolutionDir;
            conf.Output = Configuration.OutputType.None;

            // Keep source/execution charset consistent across third-party modules.
            if (target.Platform == Platform.win64)
            {
                conf.AdditionalCompilerOptions.Add("/utf-8");
                conf.Options.Add(Options.Vc.Compiler.CppLanguageStandard.CPP17);
            }
            else if (target.Platform == Platform.mac)
            {
                conf.Options.Add(Options.XCode.Compiler.CppExceptions.Enable);
                conf.Options.Add(Options.XCode.Compiler.RTTI.Enable);
                conf.Options.Add(Options.XCode.Compiler.CppLanguageStandard.CPP17);
                conf.Options.Add(new Options.XCode.Compiler.Archs("arm64"));
                conf.Options.Add(new Options.XCode.Compiler.ValidArchs("arm64"));
            }

            string configName = target.Optimization.ToString();
            if (target.TargetType == TargetType.Editor)
            {
                configName += " Editor";
            }
            else if (target.TargetType == TargetType.Game)
            {
                configName += " Game";
            }
            conf.Name = configName; 

            conf.SolutionFolder = "Programs";
        }
    }
}
