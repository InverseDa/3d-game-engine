using Sharpmake;

[module: Sharpmake.Include("Module.cs")]
[module: Sharpmake.Include("Target.cs")]
[module: Sharpmake.Include("Solution.cs")]
[module: Sharpmake.Include("LimitlessProject.cs")]
[module: Sharpmake.Include("GeneratedIncludeFiles.cs")]
[module: Sharpmake.Include("Utils/DirectoryHelper.cs")]
[module: Sharpmake.Include("../Source/Runtime/Core/Core.Build.cs")]

namespace Limitless
{
    public static class Main
    {
        [Sharpmake.Main]
        public static void SharpmakeMain(Sharpmake.Arguments arguments)
        {
            KitsRootPaths.SetUseKitsRootForDevEnv(
                DevEnv.vs2022,
                KitsRootEnum.KitsRoot10,
                Options.Vc.General.WindowsTargetPlatformVersion.v10_0_22621_0);
            arguments.Generate<SolutionRule>();
        }
    }
}
