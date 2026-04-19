using Sharpmake;

namespace Limitless
{
    public static class LaunchPlatformRules
    {
        public static void ApplyPlatformSourceExcludes(Project.Configuration conf, TargetRule target, string sourceRootPathToken)
        {
            if (target.Platform != Platform.mac)
            {
                conf.SourceFilesBuildExclude.Add($@"{sourceRootPathToken}/Private/Mac/MacWindow.mm");
                return;
            }

            conf.SourceFilesBuildExclude.Add($@"{sourceRootPathToken}/Public/Windows/WindowsWindow.h");
            conf.SourceFilesBuildExclude.Add($@"{sourceRootPathToken}/Private/Windows/LaunchWindows.cpp");
            conf.SourceFilesBuildExclude.Add($@"{sourceRootPathToken}/Private/Windows/WindowsWindow.cpp");
        }
    }

    [Sharpmake.Generate]
    public class LaunchProject : ModuleRule
    {
        public LaunchProject() 
        {
            Name = "Launch"; 
            SourceFilesExtensions.Add(".m", ".mm");
            SourceFilesCompileExtensions.Add(".m", ".mm");
        }

        public override void ConfigureAll(Configuration conf, TargetRule target)
        {
            base.ConfigureAll(conf, target);
            conf.AddPublicDependency<CoreProject>(target);
            conf.AddPublicDependency<SpdlogProject>(target);
            conf.AddPublicDependency<RALProject>(target);
            conf.AddPublicDependency<RFGProject>(target);
            if (target.Platform == Platform.mac)
            {
                conf.XcodeSystemFrameworks.Add("AppKit", "QuartzCore");
            }
            LaunchPlatformRules.ApplyPlatformSourceExcludes(conf, target, @"[project.SourceRootPath]");
        }
    }
}
