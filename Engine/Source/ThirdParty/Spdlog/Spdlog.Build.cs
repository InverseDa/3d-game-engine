using Sharpmake;

[module: Sharpmake.DebugProjectName("Sharpmake.Spdlog")]

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

            // Configure as static library
            conf.Output = Configuration.OutputType.Lib;

            // Add include path
            conf.IncludePaths.Add(@"[project.SourceRootPath]\include");

            // Define SPDLOG_COMPILED_LIB to compile the library
            conf.Defines.Add("SPDLOG_COMPILED_LIB");

            // Add source files
            conf.SourceFiles.Add(@"[project.SourceRootPath]\src\async.cpp");
            conf.SourceFiles.Add(@"[project.SourceRootPath]\src\cfg.cpp");
            conf.SourceFiles.Add(@"[project.SourceRootPath]\src\color_sinks.cpp");
            conf.SourceFiles.Add(@"[project.SourceRootPath]\src\file_sinks.cpp");
            conf.SourceFiles.Add(@"[project.SourceRootPath]\src\fmt.cpp");
            conf.SourceFiles.Add(@"[project.SourceRootPath]\src\spdlog.cpp");
            conf.SourceFiles.Add(@"[project.SourceRootPath]\src\stdout_sinks.cpp");
        }
    }
}
