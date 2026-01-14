using Sharpmake;

namespace Engine
{
    [Sharpmake.Generate]
    public class SpdlogProject : Project
    {
        public SpdlogProject()
        {
            Name = "Spdlog";

            AddTargets(new Target(
                Platform.win32 | Platform.win64,
                DevEnv.vs2022,
                Optimization.Debug | Optimization.Release
            ));

            SourceRootPath = @"[project.SharpmakeCsPath]";
        }

        [Configure]
        public void ConfigureAll(Configuration conf, Target target)
        {
            conf.ProjectFileName = "[project.Name]_[target.DevEnv]_[target.Platform]";
            conf.ProjectPath = @"[project.SharpmakeCsPath]\..\..\..\..\solution";

            conf.Output = Configuration.OutputType.Lib;

            conf.IncludePaths.Add(@"[project.SourceRootPath]\include");

            conf.Defines.Add("SPDLOG_COMPILED_LIB");
            conf.ExportDefines.Add("SPDLOG_COMPILED_LIB");
        }
    }
}