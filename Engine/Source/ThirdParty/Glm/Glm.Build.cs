using Sharpmake;
using System.IO;

namespace Limitless
{
    [Sharpmake.Generate]
    public class GlmProject : ThirdPartyModuleRule
    {
        public GlmProject()
        {
            Name = "Glm";
        }

        public override void ConfigureAll(Configuration conf, TargetRule target)
        {
            base.ConfigureAll(conf, target);

            conf.Output = Configuration.OutputType.Lib;

            conf.IncludePaths.Add(@"[project.SourceRootPath]\include");
            conf.Defines.Add("LE_USE_GLM");
            conf.ExportDefines.Add("LE_USE_GLM");
        }
    }
}