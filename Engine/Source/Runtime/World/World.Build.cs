using Sharpmake;

namespace Limitless
{
    [Sharpmake.Generate]
    public class WorldProject : ModuleRule
    {
        public WorldProject()
        {
            Name = "World";
        }

        public override void ConfigureAll(Configuration conf, TargetRule target)
        {
            base.ConfigureAll(conf, target);
            conf.AddPublicDependency<CoreProject>(target);
            conf.AddPublicDependency<RALProject>(target);
            conf.AddPublicDependency<RendererProject>(target);
        }
    }
}
