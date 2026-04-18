using Sharpmake;
using System.IO;

namespace Limitless
{
    [Sharpmake.Generate]
    public class SpdlogProject : ThirdPartyModuleRule
    {
        public SpdlogProject()
        {
            Name = "Spdlog";
        }

        public override void ConfigureAll(Configuration conf, TargetRule target)
        {
            base.ConfigureAll(conf, target);

            conf.Output = Configuration.OutputType.Lib;

            conf.IncludePaths.Add(@"[project.SourceRootPath]\include");
            conf.Defines.Add("SPDLOG_COMPILED_LIB");
            conf.ExportDefines.Add("SPDLOG_COMPILED_LIB");

            if (target.Platform == Platform.mac)
            {
                conf.Defines.Add("SPDLOG_NO_EXCEPTIONS");
                conf.ExportDefines.Add("SPDLOG_NO_EXCEPTIONS");
            }
        }
    }
}
