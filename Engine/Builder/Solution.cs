using System.IO;
using Sharpmake;

namespace Limitless
{
    [Sharpmake.Generate]
    public class SolutionRule : Sharpmake.Solution
    {
        public SolutionRule(): base(typeof(TargetRule))
        {
            Name = "Limitless";
            AddTargets(new TargetRule(
                Platform.win64,
                DevEnv.vs2022,
                Optimization.Debug | Optimization.Release,
                TargetType.Editor | TargetType.Game
            ));
        }

        [Configure]
        public void ConfigureAll(Configuration conf, TargetRule target)
        {
            conf.SolutionFileName = "[solution.Name]";
            conf.SolutionPath = DirectoryHelper.SolutionDir;

            string configName = target.Optimization.ToString();
            if (target.TargetType == TargetType.Editor)
            {
                configName += " Editor";
            }
            else if (target.TargetType == TargetType.Game)
            {
                configName += " Game";
            }
            conf.Name = configName;

            conf.AddProject<LimitlessProject>(target);
        }
    }
}