#pragma once

#include "CoreMinimal.h"

#include <vector>

enum class ERFGValidationSeverity : uint8
{
    Info,
    Warning,
    Error,
};

struct FRFGValidationIssue
{
    ERFGValidationSeverity Severity = ERFGValidationSeverity::Info;
    FString Message;
    FString Context;
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
    const std::vector<FRFGValidationIssue>& GetIssues() const;
    std::vector<FRFGValidationIssue>& GetMutableIssues();

public:
    void Clear();

private:
    std::vector<FRFGValidationIssue> Issues;
};
