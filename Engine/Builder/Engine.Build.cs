using Sharpmake;

[module: Sharpmake.Include("Utils/DirectoryHelper.cs")]
[module: Sharpmake.Include(@"..\..\Engine\Source\ThirdParty\Spdlog\Spdlog.Build.cs")] 

[module: Sharpmake.DebugProjectName("Sharpmake.Engine")]

namespace Engine
{
    [Sharpmake.Generate]
    public class EngineProject : Project
    {
        public EngineProject()
        {
            Name = "Engine";
            AddTargets(new Target(
                Platform.win32 | Platform.win64,
                DevEnv.vs2022,
                Optimization.Debug | Optimization.Release
            ));
            
            SourceRootPath = DirectoryHelper.EngineDir;
        }

        [Configure]
        public void ConfigureAll(Configuration conf, Target target)
        {
            conf.ProjectFileName = "[project.Name]_[target.DevEnv]_[target.Platform]";
            conf.ProjectPath = DirectoryHelper.SolutionDir;

            conf.Defines.Add("_HAS_EXCEPTIONS=0");

            conf.CustomProperties.Add("CustomOptimizationProperty", $"Custom-{target.Optimization}");

            conf.AddPublicDependency<SpdlogProject>(target);
        }
    }

    [Sharpmake.Generate]
    public class EngineSolution : Sharpmake.Solution
    {
        public EngineSolution()
        {
            Name = "Engine";
            AddTargets(new Target(
                Platform.win32 | Platform.win64,
                DevEnv.vs2022,
                Optimization.Debug | Optimization.Release
            ));
        }

        [Configure()]
        public void ConfigureAll(Configuration conf, Target target)
        {
            conf.SolutionFileName = "[solution.Name]_[target.DevEnv]_[target.Platform]";
            conf.SolutionPath = DirectoryHelper.SolutionDir;

            conf.AddProject<EngineProject>(target);
        }
    }

    public static class Main
    {
        [Sharpmake.Main]
        public static void SharpmakeMain(Sharpmake.Arguments arguments)
        {
            KitsRootPaths.SetUseKitsRootForDevEnv(DevEnv.vs2022, KitsRootEnum.KitsRoot10, Options.Vc.General.WindowsTargetPlatformVersion.v10_0_22621_0);
            arguments.Generate<EngineSolution>();
        }
    }
}