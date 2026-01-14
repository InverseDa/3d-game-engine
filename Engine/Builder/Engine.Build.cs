
using Sharpmake;

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

            SourceRootPath = @"[project.SharpmakeCsPath]\..\..\Engine";
        }

        [Configure]
        public void ConfigureAll(Configuration conf, Target target)
        {
            conf.ProjectFileName = "[project.Name]_[target.DevEnv]_[target.Platform]";
            conf.ProjectPath = @"[project.SharpmakeCsPath]\..\..\Solution";

            conf.Defines.Add("_HAS_EXCEPTIONS=0");

            // if not set, no precompile option will be used.
            // conf.PrecompHeader = "stdafx.h";
            // conf.PrecompSource = "stdafx.cpp";

            conf.CustomProperties.Add("CustomOptimizationProperty", $"Custom-{target.Optimization}");
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
            conf.SolutionPath = @"[solution.SharpmakeCsPath]\..\..\Solution";
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
