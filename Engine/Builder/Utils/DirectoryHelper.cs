using System.IO;
using Sharpmake;
using System.Runtime.CompilerServices;

public static class DirectoryHelper
{
    public static string RootDir { get; private set; }
    public static string EngineDir { get; private set; }
    public static string SourceDir { get; private set; }
    public static string ThirdPartyDir { get; private set; }
    public static string SolutionDir { get; private set; }
    public static string TmpDir { get; private set; }
    public static string ProjectGenDir { get; private set; }

    static DirectoryHelper()
    {
        Setup();
    }

    private static void Setup([CallerFilePath] string sourceFilePath = "")
    {
        string currentScriptDir = Path.GetDirectoryName(sourceFilePath);

        RootDir = Util.PathMakeStandard(Path.GetFullPath(Path.Combine(currentScriptDir, @"..\..\..")));
        EngineDir = Path.Combine(RootDir, "Engine");
        SourceDir = Path.Combine(EngineDir, "Source");
        ThirdPartyDir = Path.Combine(SourceDir, "ThirdParty");
        SolutionDir = Path.Combine(RootDir, "Solution");
        TmpDir = Path.Combine(RootDir, "Temp");
        ProjectGenDir = Path.Combine(TmpDir, "ProjectFiles");
    }
}