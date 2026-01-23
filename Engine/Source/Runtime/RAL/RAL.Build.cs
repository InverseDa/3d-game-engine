using Sharpmake;

namespace Limitless
{
    [Sharpmake.Generate]
    public class RALProject : ModuleRule
    {
        public RALProject()
        { 
            Name = "RAL"; // Render Abstraction Layer
        }

        public override void ConfigureAll(Configuration conf, TargetRule target)
        {
            base.ConfigureAll(conf, target);
            conf.AddPublicDependency<CoreProject>(target);
        }
    }
}