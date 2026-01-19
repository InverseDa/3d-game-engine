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
            SourceRootPath = DirectoryHelper.EngineDir;
            SourceFilesExtensions.Add(".cs");
            SourceFilesExtensions.Add(".cpp");
            SourceFilesExtensions.Add(".h");
            SourceFilesExtensions.Add(".c");
            SourceFilesExtensions.Add(".inl");
            AddTargets(new TargetRule(
                Platform.win64,
                DevEnv.vs2022,
                Optimization.Debug | Optimization.Release,
                TargetType.Game | TargetType.Editor
            ));
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

            // Exclude ALL files from build in this project. 
            // The code is compiled by the respective Module projects (Launch, Core, etc.) and linked via dependencies.
            // This allows the project to serve as a complete source browser without double-compilation.
            conf.SourceFilesBuildExcludeRegex.Add(@".*");

            conf.TargetPath = Path.Combine(DirectoryHelper.EngineDir, "Binaries", "Win64");

            bool IsEditor = target.TargetType == TargetType.Editor;
            conf.TargetFileName = IsEditor ? "LimitlessEditor" : "LimitlessGame";
            conf.Defines.Add("WITH_EDITOR=" + (IsEditor ? "1" : "0"));

            conf.AddPublicDependency<CoreProject>(target);
            conf.AddPublicDependency<LaunchProject>(target);
        }
    }
}