#pragma once

#include "FileSystem/FileSystem.h"
#include "Types/Uuid.h"

namespace LE
{

struct FDemoContentGeneration
{
    FUuid Id;
    String AssetRoot;
    String ScenePath;
};

struct FDemoContentGenerationDiagnostic
{
    const char* Code = "ok";
    const char* Message = "";
    EFileSystemResult FileResult = EFileSystemResult::Success;
    FUuid GenerationId;
};

// Resolve a read-only snapshot of the published generation. The caller validates
// the Assets directory when it mounts the generation's candidate catalog.
// A failure preserves Out, and a parsed candidate ID is available in Diagnostic.
bool ResolveDemoContentGeneration(StringView PublicationRoot, FDemoContentGeneration& Out, FDemoContentGenerationDiagnostic& Diagnostic) noexcept;

} // namespace LE
