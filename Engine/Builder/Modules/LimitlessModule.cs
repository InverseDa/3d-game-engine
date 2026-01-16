using Sharpmake;
using System.IO;
using System;

public abstract class LimitlessModule : Project
{
    public LimitlessModule()
    {
        SourceRootPath = @"[project.SharpmakeCsPath]";
        AddTargets(new Target(
            Platform.win32 | Platform.win64,
            DevEnv.vs2022,
            Optimization.Debug | Optimization.Release
        ));
    }

    [Configure]
    public virtual void ConfigureAll(Configuration conf, Target target)
    {
        conf.ProjectFileName = "[project.Name]_[target.DevEnv]_[target.Platform]";
        conf.ProjectPath = DirectoryHelper.SolutionDir;

        conf.Output = Configuration.OutputType.Lib;
        conf.SolutionFolder = "Programs";

        conf.IncludePaths.Add(@"[project.SourceRootPath]\Public");
        conf.IncludePaths.Add(@"[project.SourceRootPath]\Private");
    }
}

public abstract class LimitlessThirdPartyModule : Project
{
    public LimitlessThirdPartyModule()
    {
        SourceRootPath = @"[project.SharpmakeCsPath]";
        AddTargets(new Target(
            Platform.win32 | Platform.win64,
            DevEnv.vs2022,
            Optimization.Debug | Optimization.Release
        ));
    }

    [Configure]
    public virtual void ConfigureAll(Configuration conf, Target target)
    {
        conf.ProjectPath = DirectoryHelper.SolutionDir;
        conf.Output = Configuration.OutputType.None;

        conf.SolutionFolder = "Programs";
    }
}
