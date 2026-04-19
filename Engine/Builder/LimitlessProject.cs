using System;
using System.IO;
using Sharpmake;

namespace Limitless
{
    [Sharpmake.Generate]
    public class LimitlessProject : Sharpmake.Project
    {
        public LimitlessProject() : base(typeof(TargetRule))
        {
            Name = "LimitlessEngine";
            SourceRootPath = Path.Combine(DirectoryHelper.SourceDir, "Runtime", "Launch");
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
            conf.ProjectPath = Path.Combine(DirectoryHelper.TmpDir, "ProjectFiles");
            conf.ProjectFileName = "[project.Name]_[target.DevEnv]_[target.Platform]";

            conf.SolutionFolder = "Engine";

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

            conf.Output = Configuration.OutputType.Exe;
            if (target.Platform == Platform.win64)
            {
                conf.Options.Add(Options.Vc.Linker.SubSystem.Windows);
                conf.TargetPath = Path.Combine(DirectoryHelper.EngineDir, "Binaries", "Win64");
                conf.AdditionalCompilerOptions.Add("/utf-8");
                conf.Options.Add(Options.Vc.Compiler.CppLanguageStandard.CPP17);
            }
            else if (target.Platform == Platform.mac)
            {
                conf.TargetPath = Path.Combine(DirectoryHelper.EngineDir, "Binaries", "Mac");
                conf.Options.Add(Options.XCode.Compiler.CppLanguageStandard.CPP17);
                conf.Options.Add(new Options.XCode.Compiler.Archs("arm64"));
                conf.Options.Add(new Options.XCode.Compiler.ValidArchs("arm64"));
                LaunchPlatformRules.ApplyPlatformSourceExcludes(conf, target, @"[project.SourceRootPath]");
            }

            bool IsEditor = target.TargetType == TargetType.Editor;
            conf.TargetFileName = IsEditor ? "LimitlessEditor" : "LimitlessGame";
            conf.Defines.Add("WITH_EDITOR=" + (IsEditor ? "1" : "0"));

            // All Source Code Module must be added here
            conf.AddPublicDependency<CoreProject>(target);
            conf.AddPublicDependency<LaunchProject>(target);
            conf.AddPublicDependency<RALProject>(target);
            conf.AddPublicDependency<RFGProject>(target, DependencySetting.DefaultWithoutLinking);
            conf.AddPublicDependency<RendererProject>(target);
            conf.AddPublicDependency<WorldProject>(target);

        }
    }
}
