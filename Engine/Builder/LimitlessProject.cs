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
            SourceFilesExtensions.Add(".cs");
            AddTargets(new TargetRule(
                Platform.win64,
                DevEnv.vs2022,
                Optimization.Debug | Optimization.Release,
                TargetType.Game | TargetType.Editor
            ));

            // Solution Engine does not contain the whole project, so we need to add the README.md file manually.
            // This can force the solution to contain the whole project.
            string ReadmePath = Path.Combine(DirectoryHelper.EngineDir, "README.md");
            if (File.Exists(ReadmePath))
            {
                SourceFiles.Add(ReadmePath); 
            }
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
            conf.Name = configName;

            conf.Output = Configuration.OutputType.Exe;
            conf.Options.Add(Options.Vc.Linker.SubSystem.Windows);

            conf.TargetPath = Path.Combine(DirectoryHelper.EngineDir, "Binaries", "Win64");

            bool IsEditor = target.TargetType == TargetType.Editor;
            conf.TargetFileName = IsEditor ? "LimitlessEditor" : "LimitlessGame";
            conf.Defines.Add("WITH_EDITOR=" + (IsEditor ? "1" : "0"));

            conf.AddPublicDependency<CoreProject>(target);
            conf.AddPublicDependency<LaunchProject>(target);
        }
    }
}