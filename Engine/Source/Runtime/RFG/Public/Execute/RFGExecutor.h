#pragma once

#include "CoreMinimal.h"
#include "Execute/RFGDeferredReleaseSink.h"

namespace LE
{

class FRFGRecordedGraph;
class FRFGCompiledPlan;
struct FRFGExecutionContext;
struct FRALSubmitInfo;
enum class ERALQueueSubmitResult : uint8;

struct FRFGExecuteOptions
{
    bool bSubmitImmediately = true;
    bool bWaitForCompletion = false;
    const FRALSubmitInfo* SubmitInfo = nullptr;
    /** Borrowed only for the duration of Execute; RFG never retains or deletes it. */
    IRFGDeferredReleaseSink* DeferredReleaseSink = nullptr;
};

class RFG_API FRFGExecutor
{
public:
    FRFGExecutor() = default;
    ~FRFGExecutor() = default;

public:
    ERALQueueSubmitResult Execute(
        const FRFGCompiledPlan& CompiledPlan,
        const FRFGRecordedGraph& RecordedGraph,
        FRFGExecutionContext& ExecutionContext,
        const FRFGExecuteOptions& ExecuteOptions = {}) const;
};

} // namespace LE
