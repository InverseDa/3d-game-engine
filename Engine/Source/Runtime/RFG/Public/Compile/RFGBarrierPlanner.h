#pragma once

#include "CoreMinimal.h"

class FRFGRecordedGraph;
class FRFGCompiledPlan;

class RFG_API FRFGBarrierPlanner
{
public:
    FRFGBarrierPlanner() = default;
    ~FRFGBarrierPlanner() = default;

public:
    void BuildResourceLifetimes(const FRFGRecordedGraph& RecordedGraph, FRFGCompiledPlan& InOutPlan) const;
    void BuildBarriers(const FRFGRecordedGraph& RecordedGraph, FRFGCompiledPlan& InOutPlan) const;
};
