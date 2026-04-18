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

            // Vulkan in this repo is headers + SDK import library; this module does not build a local .lib.
            conf.Output = Configuration.OutputType.None;

            conf.IncludePaths.Add("[project.SourceRootPath]/Include");

            // Link Vulkan SDK library
            if (target.Platform == Platform.win64)
            {
                string vulkanSdkPath = System.Environment.GetEnvironmentVariable("VULKAN_SDK");
                if (string.IsNullOrEmpty(vulkanSdkPath))
                {
                    // Fallback to default path if environment variable is not set
                    vulkanSdkPath = @"C:\VulkanSDK\1.3.275.0";
                }

                string libPath = Path.Combine(vulkanSdkPath, "Lib");
                if (System.IO.Directory.Exists(libPath))
                {
                    conf.LibraryPaths.Add(libPath);
                    conf.LibraryFiles.Add("vulkan-1.lib");
                }
            }
            else if (target.Platform == Platform.mac)
            {
                string vulkanSdkPath = System.Environment.GetEnvironmentVariable("VULKAN_SDK");
                string libPath = string.Empty;
                if (!string.IsNullOrEmpty(vulkanSdkPath))
                {
                    libPath = Path.Combine(vulkanSdkPath, "lib");
                }
                else if (Directory.Exists("/opt/homebrew/lib"))
                {
                    libPath = "/opt/homebrew/lib";
                }
                else if (Directory.Exists("/usr/local/lib"))
                {
                    libPath = "/usr/local/lib";
                }

                if (!string.IsNullOrEmpty(libPath) && Directory.Exists(libPath))
                {
                    conf.LibraryPaths.Add(libPath);
                    conf.LibraryFiles.Add("vulkan");
                }
            }
        }
    }
}
