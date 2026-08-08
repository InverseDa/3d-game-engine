#pragma once

#include "CoreMinimal.h"
#include "Core/RFGTypes.h"
#include "Execute/RFGExecutor.h"
#include "Record/RFGBuilder.h"
#include "Compile/RFGCompiler.h"

namespace LE
{

class FRFGRuntime;
class IRFGPassRegistry;
class FRFGRecordedGraph;

class RFG_API FRFGInstance : public FNonCopyable
{
public:
    FRFGInstance();
    ~FRFGInstance();

public:
    void Initialize(FRFGRuntime* InRuntime);
    void Shutdown();
    bool IsInitialized() const;

public:
    FRFGRuntime* GetRuntime();
    const FRFGRuntime* GetRuntime() const;

public:
    IRFGPassRegistry* GetPassRegistry() const;

public:
    FRFGBuilder CreateBuilder() const;

public:
    FRFGCompileResult Compile(
        const FRFGRecordedGraph& RecordedGraph,
        const FRFGGraphSignature& Signature = {});

    ERALQueueSubmitResult Execute(
        const FRFGCompileResult& CompileResult,
        const FRFGRecordedGraph& RecordedGraph,
        FRFGExecutionContext& ExecutionContext,
        const FRFGExecuteOptions& ExecuteOptions = {});

public:
    void SetCompileOptions(const FRFGCompileOptions& InCompileOptions);
    const FRFGCompileOptions& GetCompileOptions() const;

private:
    FRFGRuntime* Runtime = nullptr;
    FRFGExecutor* Executor = nullptr;
};

} // namespace LE
