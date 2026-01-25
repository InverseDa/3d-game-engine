using Sharpmake;
using System.IO;

namespace Limitless
{
    [Sharpmake.Generate]
    public class VulkanProject : ThirdPartyModuleRule
    {
        public VulkanProject()
        {
            Name = "Vulkan";
        }

        public override void ConfigureAll(Configuration conf, TargetRule target)
        {
            base.ConfigureAll(conf, target);

            conf.Output = Configuration.OutputType.Lib;

            conf.IncludePaths.Add(@"[project.SourceRootPath]\Include");
        }
    }
}