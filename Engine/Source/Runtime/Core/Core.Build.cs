using Sharpmake;

namespace Limitless
{
    [Sharpmake.Generate]
    public class CoreProject : LimitlessModule
    {
        public CoreProject() 
        { 
            Name = "Core"; 
        }

        public override void ConfigureAll(Configuration conf, Target target)
        {
            base.ConfigureAll(conf, target);
            conf.AddPublicDependency<SpdlogProject>(target);
        }
    }
}