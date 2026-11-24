#include "DemoContentGeneration.h"

#if defined(WITH_EDITOR) && WITH_EDITOR
namespace LE
{
namespace
{

constexpr char PointerPrefixText[] = "{\"generation\":\"";
constexpr char PointerSuffixText[] = "\"}\n";
constexpr StringView PointerPrefix{PointerPrefixText, sizeof(PointerPrefixText) - 1};
constexpr StringView PointerSuffix{PointerSuffixText, sizeof(PointerSuffixText) - 1};
constexpr std::size_t PointerSize = PointerPrefix.Size() + FUuid::CanonicalStringLength + PointerSuffix.Size();
constexpr char SceneFileName[] = "Scene.json";

bool ReportFileFailure(FDemoContentGenerationDiagnostic& Diagnostic, const char* Code, EFileSystemResult Result) noexcept
{
    Diagnostic.Code = Code;
    Diagnostic.Message = DescribeFileSystemResult(Result);
    Diagnostic.FileResult = Result;
    return false;
}

} // namespace

bool ResolveDemoContentGeneration(const StringView PublicationRoot, FDemoContentGeneration& Out, FDemoContentGenerationDiagnostic& Diagnostic) noexcept
{
    Diagnostic = {};
    String PointerPath;
    if (const auto Result = ResolveContentFile(PublicationRoot, "current.json", PointerPath); Result != EFileSystemResult::Success)
    {
        return ReportFileFailure(Diagnostic, "generation_pointer_unreadable", Result);
    }

    Array<uint8> PointerBytes;
    if (const auto Result = ReadContentFile(PointerPath.View(), PointerSize, PointerBytes); Result != EFileSystemResult::Success)
    {
        const char* const Code = Result == EFileSystemResult::LimitExceeded ? "generation_pointer_invalid" : "generation_pointer_unreadable";
        return ReportFileFailure(Diagnostic, Code, Result);
    }

    const StringView Pointer(reinterpret_cast<const char*>(PointerBytes.Data()), PointerBytes.Size());
    if (Pointer.Size() != PointerSize || Pointer.Substr(0, PointerPrefix.Size()) != PointerPrefix ||
        Pointer.Substr(PointerPrefix.Size() + FUuid::CanonicalStringLength, PointerSuffix.Size()) != PointerSuffix)
    {
        Diagnostic.Code = "generation_pointer_invalid";
        Diagnostic.Message = "current.json is not in the published generation format";
        return false;
    }

    const StringView PublishedId = Pointer.Substr(PointerPrefix.Size(), FUuid::CanonicalStringLength);
    FUuid Id;
    if (!FUuid::TryParse(PublishedId, Id) || !Id.IsValid())
    {
        Diagnostic.Code = "generation_pointer_invalid";
        Diagnostic.Message = "current.json does not contain a canonical non-nil generation UUID";
        return false;
    }
    Diagnostic.GenerationId = Id;

    // A contained marker verifies the generations directory and generation
    // directory before its sibling Assets directory is passed to the catalog.
    String RelativeScenePath;
    if (!RelativeScenePath.TryAssign("generations/") || !RelativeScenePath.TryAppend(PublishedId) ||
        !RelativeScenePath.TryAppend("/") || !RelativeScenePath.TryAppend(SceneFileName))
    {
        return ReportFileFailure(Diagnostic, "generation_out_of_memory", EFileSystemResult::OutOfMemory);
    }
    String ScenePath(Out.ScenePath.GetAllocator());
    if (const auto Result = ResolveContentFile(PublicationRoot, RelativeScenePath.View(), ScenePath); Result != EFileSystemResult::Success)
    {
        return ReportFileFailure(Diagnostic, "generation_layout_invalid", Result);
    }

    String AssetRoot(Out.AssetRoot.GetAllocator());
    const auto DirectorySize = ScenePath.Size() - (sizeof(SceneFileName) - 1);
    if (!AssetRoot.TryAssign(ScenePath.View().Substr(0, DirectorySize)) || !AssetRoot.TryAppend("Assets"))
    {
        return ReportFileFailure(Diagnostic, "generation_out_of_memory", EFileSystemResult::OutOfMemory);
    }
    Out.Id = Id;
    Out.ScenePath.Swap(ScenePath);
    Out.AssetRoot.Swap(AssetRoot);
    return true;
}

} // namespace LE
#endif
