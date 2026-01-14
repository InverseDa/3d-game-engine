using Sharpmake;

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
            // ... (Targets 保持不变) ...
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

            conf.CustomProperties.Add("CustomOptimizationProperty", $"Custom-{target.Optimization}");

            // 2. [新增] 告诉 Engine 我要用 Spdlog <--- 关键修复
            // 这会自动把 Spdlog 的 IncludePaths 加进来，也会自动链接 Spdlog.lib
            conf.AddPublicDependency<SpdlogProject>(target);
        }
    }

    [Sharpmake.Generate]
    public class EngineSolution : Sharpmake.Solution
    {
        public EngineSolution()
        {
            Name = "Engine";
            // ... (Targets 保持不变) ...
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

            // 只要加了 EngineProject，因为 Engine 依赖 Spdlog，
            // Sharpmake 会自动把 Spdlog 项目也加到 .sln 里。
            conf.AddProject<EngineProject>(target);
        }
    }

    public static class Main
    {
        [Sharpmake.Main]
        public static void SharpmakeMain(Sharpmake.Arguments arguments)
        {
            // SDK 版本如果没装，建议注释掉让它自动检测
            KitsRootPaths.SetUseKitsRootForDevEnv(DevEnv.vs2022, KitsRootEnum.KitsRoot10, Options.Vc.General.WindowsTargetPlatformVersion.v10_0_22621_0);
            
            arguments.Generate<EngineSolution>();
        }
    }
}