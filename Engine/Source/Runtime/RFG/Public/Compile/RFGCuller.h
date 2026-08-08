#pragma once

#include "CoreMinimal.h"

namespace LE
{

class FRFGRecordedGraph;
class FRFGCompiledPlan;

class RFG_API FRFGCuller
{
public:
    FRFGCuller() = default;
    ~FRFGCuller() = default;

public:
    void CullPasses(const FRFGRecordedGraph& RecordedGraph, FRFGCompiledPlan& InOutPlan) const;
};

} // namespace LE
