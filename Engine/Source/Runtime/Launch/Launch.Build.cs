using Sharpmake;

namespace Limitless
{
    [Sharpmake.Generate]
    public class LaunchProject : ModuleRule
    {
        public LaunchProject() 
        { 
            Name = "Launch"; 
        }

        public override void ConfigureAll(Configuration conf, TargetRule target)
        {
            base.ConfigureAll(conf, target);
            conf.AddPublicDependency<CoreProject>(target);
            conf.AddPublicDependency<SpdlogProject>(target);
            conf.AddPublicDependency<RALProject>(target);
        }
    }
}