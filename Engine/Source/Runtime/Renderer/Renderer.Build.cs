using Sharpmake;

namespace Limitless
{
    [Sharpmake.Generate]
    public class RendererProject : ModuleRule
    {
        public RendererProject()
        {
            Name = "Renderer";
        }

        public override void ConfigureAll(Configuration conf, TargetRule target)
        {
            base.ConfigureAll(conf, target);
            conf.AddPublicDependency<CoreProject>(target);
            conf.AddPublicDependency<RALProject>(target);
            conf.AddPublicDependency<RFGProject>(target);
        }
    }
}
