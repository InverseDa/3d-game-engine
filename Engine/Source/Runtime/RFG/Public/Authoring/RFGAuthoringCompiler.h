#pragma once

#include "CoreMinimal.h"
#include "Authoring/RFGTemplate.h"
#include "Authoring/RFGValidationReport.h"

namespace LE
{

struct FRFGAuthoringCompileRequest
{
    LE::String AssetPath;
    uint32 AssetVersion = 0;
    bool bAllowExperimentalNodes = false;
};

struct FRFGAuthoringCompileResult
{
    FRFGTemplate Template;
    FRFGValidationReport Validation;
    bool bSucceeded = false;
};

class RFG_API IRFGAuthoringCompiler
{
public:
    virtual ~IRFGAuthoringCompiler() = default;

public:
    virtual FRFGAuthoringCompileResult Compile(const FRFGAuthoringCompileRequest& Request) = 0;
};

} // namespace LE
