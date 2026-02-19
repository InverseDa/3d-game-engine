#pragma once

#include "CoreMinimal.h"

class FRFGRecordedGraph;
class FRFGCompiledPlan;

class RFG_API FRFGDependencyAnalyzer
{
public:
    FRFGDependencyAnalyzer() = default;
    ~FRFGDependencyAnalyzer() = default;

public:
    void BuildDependencies(const FRFGRecordedGraph& RecordedGraph, FRFGCompiledPlan& OutPlan) const;
};
