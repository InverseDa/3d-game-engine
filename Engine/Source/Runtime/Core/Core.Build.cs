using Sharpmake;

namespace Limitless
{
    [Sharpmake.Generate]
    public class CoreProject : ModuleRule
    {
        public CoreProject() 
        { 
            Name = "Core"; 
        }

        public override void ConfigureAll(Configuration conf, TargetRule target)
        {
            base.ConfigureAll(conf, target);
            conf.AddPublicDependency<GlmProject>(target);
            conf.AddPublicDependency<SpdlogProject>(target);
        }
    }
}