using Sharpmake;

namespace Limitless
{
    [Sharpmake.Generate]
    public class RFGProject : ModuleRule
    {
        public RFGProject()
        {
            Name = "RFG"; // Render Frame Graph
        }

        public override void ConfigureAll(Configuration conf, TargetRule target)
        {
            base.ConfigureAll(conf, target);
            conf.AddPublicDependency<CoreProject>(target);
            conf.AddPublicDependency<RALProject>(target);
        }
    }
}
