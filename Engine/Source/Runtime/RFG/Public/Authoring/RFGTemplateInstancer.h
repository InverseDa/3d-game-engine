#pragma once

#include "CoreMinimal.h"
#include "Authoring/RFGParameterStore.h"
#include "Authoring/RFGTemplate.h"
#include "Authoring/RFGValidationReport.h"
#include "Core/RFGTypes.h"

class FRFGBuilder;

struct FRFGTemplateInstantiateOptions
{
    bool bAllowPassCulling = true;
    bool bDeterministicOrder = true;
};

struct FRFGTemplateInstantiateResult
{
    FRFGValidationReport Validation;
    FRFGGraphSignature Signature;
    bool bSucceeded = false;
};

class RFG_API FRFGTemplateInstancer
{
public:
    FRFGTemplateInstancer() = default;
    ~FRFGTemplateInstancer() = default;

public:
    FRFGTemplateInstantiateResult Instantiate(
        const FRFGTemplate& Template,
        const FRFGParameterStore& ParameterStore,
        FRFGBuilder& Builder,
        const FRFGTemplateInstantiateOptions& Options = {}) const;
};
