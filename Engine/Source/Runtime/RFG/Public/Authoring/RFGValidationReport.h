#pragma once

#include "CoreMinimal.h"


namespace LE
{

enum class ERFGValidationSeverity : uint8
{
    Info,
    Warning,
    Error,
};

struct FRFGValidationIssue
{
    ERFGValidationSeverity Severity = ERFGValidationSeverity::Info;
    LE::String Message;
    LE::String Context;
};

class RFG_API FRFGValidationReport
{
public:
    FRFGValidationReport() = default;
    ~FRFGValidationReport() = default;

public:
    void AddIssue(const FRFGValidationIssue& Issue);
    bool HasErrors() const;
    bool IsEmpty() const;

public:
    const LE::Array<FRFGValidationIssue>& GetIssues() const;
    LE::Array<FRFGValidationIssue>& GetMutableIssues();

public:
    void Clear();

private:
    LE::Array<FRFGValidationIssue> Issues;
};

} // namespace LE
