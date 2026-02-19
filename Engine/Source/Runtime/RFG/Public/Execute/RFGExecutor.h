#pragma once

#include "CoreMinimal.h"

class FRFGRecordedGraph;
class FRFGCompiledPlan;
struct FRFGExecutionContext;

struct FRFGExecuteOptions
{
    bool bSubmitImmediately = true;
    bool bWaitForCompletion = false;
};

class RFG_API FRFGExecutor
{
public:
    FRFGExecutor() = default;
    ~FRFGExecutor() = default;

public:
    void Execute(
        const FRFGCompiledPlan& CompiledPlan,
        const FRFGRecordedGraph& RecordedGraph,
        FRFGExecutionContext& ExecutionContext,
        const FRFGExecuteOptions& ExecuteOptions = {}) const;
};
