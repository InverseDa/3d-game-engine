#include "Authoring/RFGValidationReport.h"

void FRFGValidationReport::AddIssue(const FRFGValidationIssue& Issue)
{
    Issues.push_back(Issue);
}

bool FRFGValidationReport::HasErrors() const
{
    for (const FRFGValidationIssue& Issue : Issues)
    {
        if (Issue.Severity == ERFGValidationSeverity::Error)
        {
            return true;
        }
    }

    return false;
}

bool FRFGValidationReport::IsEmpty() const
{
    return Issues.empty();
}

const std::vector<FRFGValidationIssue>& FRFGValidationReport::GetIssues() const
{
    return Issues;
}

std::vector<FRFGValidationIssue>& FRFGValidationReport::GetMutableIssues()
{
    return Issues;
}

void FRFGValidationReport::Clear()
{
    Issues.clear();
}
