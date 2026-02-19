#pragma once

#include "CoreMinimal.h"
#include "Compile/RFGCompiler.h"
#include "Core/RFGTypes.h"
#include "Execute/RFGExecutor.h"
#include "Record/RFGBuilder.h"

#include <memory>

class IRFGPassRegistry;
class FRFGRecordedGraph;
class FRFGPlanCache;

class RFG_API FRFGInstance : public FNonCopyable
{
public:
    FRFGInstance() override;
    ~FRFGInstance() override;

public:
    void Initialize(IRFGPassRegistry* InPassRegistry);
    void Shutdown();

public:
    FRFGBuilder CreateBuilder() const;

public:
    FRFGCompileResult Compile(
        const FRFGRecordedGraph& RecordedGraph,
        const FRFGGraphSignature& Signature = {});

    void Execute(
        const FRFGCompileResult& CompileResult,
        const FRFGRecordedGraph& RecordedGraph,
        FRFGExecutionContext& ExecutionContext,
        const FRFGExecuteOptions& ExecuteOptions = {});

public:
    void SetCompileOptions(const FRFGCompileOptions& InCompileOptions);
    const FRFGCompileOptions& GetCompileOptions() const;

private:
    IRFGPassRegistry* PassRegistry = nullptr;
    FRFGCompileOptions CompileOptions;

private:
    FRFGCompiler* Compiler;
    FRFGExecutor* Executor;
    FRFGPlanCache* PlanCache;
};
