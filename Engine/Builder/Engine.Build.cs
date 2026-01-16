using Sharpmake;
using System;
using System.Linq;
using System.Reflection; // 必须引用反射库

[module: Sharpmake.Include("GeneratedIncludeFiles.cs")]
[module: Sharpmake.Include("Utils/DirectoryHelper.cs")]
[module: Sharpmake.Include("Modules/LimitlessModule.cs")]
[module: Sharpmake.DebugProjectName("Limitless.Engine")]

namespace Limitless
{
    // 代表引擎的核心项目 (或者叫 Limitless.Launch)
    [Sharpmake.Generate]
    public class EngineProject : Project
    {
        public EngineProject()
        {
            Name = "LimitlessEngine"; // VS 里显示的项目名

            // 使用 Globals 全局路径
            SourceRootPath = DirectoryHelper.EngineDir;
            SourceFilesExtensions.Add(".cs");

            AddTargets(new Target(
                Platform.win64,
                DevEnv.vs2022,
                Optimization.Debug | Optimization.Release
            ));
        }

        [Configure]
        public void ConfigureAll(Configuration conf, Target target)
        {
            conf.ProjectFileName = "[project.Name]_[target.DevEnv]_[target.Platform]";
            conf.ProjectPath = DirectoryHelper.SolutionDir;

            // ---------------------------------------------------------
            // 1. 输出设置
            // ---------------------------------------------------------
            // 引擎本身通常编译为 Static Lib (供游戏链接) 
            // 或者 Exe (如果包含 Main 入口)
            // 这里我们假设它是聚合库，或者包含了 Main 的启动器
            // conf.Output = Configuration.OutputType.Lib;
            conf.Output = Configuration.OutputType.Exe; // kodak: 目前开发期，为了快速验证，先编译为 Exe
            conf.SolutionFolder = "Engine";

            // 中间目录整洁化
            conf.IntermediatePath = System.IO.Path.Combine(DirectoryHelper.TmpDir, "Obj", "[project.Name]");
            conf.TargetPath = System.IO.Path.Combine(DirectoryHelper.TmpDir, "Bin", "[project.Name]");

            // ---------------------------------------------------------
            // 2. 全局宏定义
            // ---------------------------------------------------------
            conf.Defines.Add("_HAS_EXCEPTIONS=0");
            conf.Defines.Add("LE_ENGINE"); // 标识这是引擎编译环境

            // ---------------------------------------------------------
            // 3. PCH (预编译头)
            // ---------------------------------------------------------
            // 如果 Engine 目录下有直接的源码（比如 Launch 文件夹），需要 PCH
            // 如果 Engine 只是个空壳用来聚合模块，这几行可以注释掉
            // conf.PrecompHeader = "EnginePCH.h";
            // conf.PrecompSource = "EnginePCH.cpp";

            // ---------------------------------------------------------
            // 4. [魔法] 自动挂载所有 Runtime 模块
            // ---------------------------------------------------------
            MountRuntimeModules(conf, target);

            // ---------------------------------------------------------
            // 5. [手动] 挂载第三方库 (按需启用)
            // ---------------------------------------------------------
            // 只有这里需要显式写出依赖。
            // 注意：通常 Core 模块已经依赖了 Spdlog，如果 Engine 依赖了 Core，
            // 这一行其实可以省掉（依赖传递）。但显式写出来也无妨。
            // conf.AddPublicDependency<SpdlogProject>(target);
        }

        // 核心：自动扫描并添加依赖
        private void MountRuntimeModules(Configuration conf, Target target)
        {
            // 获取当前程序集（包含所有被扫描到的 .sharpmake.cs 类）
            var assembly = Assembly.GetExecutingAssembly();
            var allTypes = assembly.GetTypes();

            // 筛选条件：
            // 1. 继承自 LimitlessModule (意味着它是 Runtime 模块)
            // 2. 不是抽象类
            // 3. 不是 EngineProject 自己 (防止循环依赖)
            var runtimeModules = allTypes.Where(t =>
                t.IsSubclassOf(typeof(LimitlessModule)) &&
                !t.IsAbstract &&
                t != typeof(EngineProject)
            );

            foreach (var moduleType in runtimeModules)
            {
                // 动态添加依赖
                conf.AddPublicDependency(target, moduleType);
                
                // (可选) 打印日志方便调试
                Console.WriteLine($"[Limitless] Auto-mounted Module: {moduleType.Name}");
            }
        }
    }

    // 解决方案定义
    [Sharpmake.Generate]
    public class EngineSolution : Sharpmake.Solution
    {
        public EngineSolution()
        {
            Name = "Limitless";

            AddTargets(new Target(
                Platform.win64,
                DevEnv.vs2022,
                Optimization.Debug | Optimization.Release
            ));
        }

        [Configure]
        public void ConfigureAll(Configuration conf, Target target)
        {
            conf.SolutionFileName = "[solution.Name]_[target.DevEnv]_[target.Platform]";
            conf.SolutionPath = DirectoryHelper.SolutionDir;

            // 只要添加了 EngineProject，Sharpmake 会自动把
            // Engine 依赖的所有 Modules (Core, Platform...) 也加进解决方案
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