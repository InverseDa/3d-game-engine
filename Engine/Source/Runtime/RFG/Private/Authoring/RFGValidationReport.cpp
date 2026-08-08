#include "Authoring/RFGValidationReport.h"

namespace LE
{

void FRFGValidationReport::AddIssue(const FRFGValidationIssue& Issue)
{
    Issues.PushBack(Issue);
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
    return Issues.IsEmpty();
}

const LE::Array<FRFGValidationIssue>& FRFGValidationReport::GetIssues() const
{
    return Issues;
}

LE::Array<FRFGValidationIssue>& FRFGValidationReport::GetMutableIssues()
{
    return Issues;
}

void FRFGValidationReport::Clear()
{
    Issues.Clear();
}

} // namespace LE
