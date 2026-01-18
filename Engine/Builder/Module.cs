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
            conf.ProjectFileName = "[project.Name]_[target.DevEnv]_[target.Platform]";
            conf.ProjectPath = DirectoryHelper.ProjectGenDir;

            conf.SolutionFolder = "Programs";

            conf.TargetPath = Path.Combine(DirectoryHelper.TmpDir, "Bin", "[project.Name]");
            conf.IntermediatePath = Path.Combine(DirectoryHelper.TmpDir, "Obj", "[project.Name]");

            conf.IncludePaths.Add(@"[project.SourceRootPath]\Public");
            conf.IncludePaths.Add(@"[project.SourceRootPath]\Private");

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
            else if (target.TargetType == TargetType.Editor)
            {
                conf.Output = Configuration.OutputType.Dll;
                conf.Defines.Add($"{apiMacro}=__declspec(dllexport)");
                conf.ExportDefines.Add($"{apiMacro}=__declspec(dllimport)");
            }
            else
            {
                conf.Output = Configuration.OutputType.Lib;
            };
        }
    }

    public abstract class ThirdPartyModuleRule : Sharpmake.Project
    {
        public ThirdPartyModuleRule() : base(typeof(TargetRule))
        {
            SourceRootPath = @"[project.SharpmakeCsPath]";
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
            conf.ProjectPath = DirectoryHelper.SolutionDir;
            conf.Output = Configuration.OutputType.None;

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
