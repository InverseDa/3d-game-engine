#include "Core/RFGInstance.h"

#include "Core/RFGRuntime.h"

namespace LE
{

FRFGInstance::FRFGInstance()
    : Executor(new FRFGExecutor())
{
}

FRFGInstance::~FRFGInstance()
{
    Shutdown();

    delete Executor;
    Executor = nullptr;
}

void FRFGInstance::Initialize(FRFGRuntime* InRuntime)
{
    Runtime = InRuntime;
}

void FRFGInstance::Shutdown()
{
    Runtime = nullptr;
}

bool FRFGInstance::IsInitialized() const
{
    return Runtime != nullptr;
}

FRFGRuntime* FRFGInstance::GetRuntime()
{
    return Runtime;
}

const FRFGRuntime* FRFGInstance::GetRuntime() const
{
    return Runtime;
}

IRFGPassRegistry* FRFGInstance::GetPassRegistry() const
{
    return Runtime != nullptr ? Runtime->GetPassRegistry() : nullptr;
}

FRFGBuilder FRFGInstance::CreateBuilder() const
{
    FRFGBuilderCreateInfo CreateInfo;
    CreateInfo.PassRegistry = GetPassRegistry();
    return FRFGBuilder(CreateInfo);
}

FRFGCompileResult FRFGInstance::Compile(const FRFGRecordedGraph& RecordedGraph, const FRFGGraphSignature& Signature)
{
    return Runtime != nullptr ? Runtime->Compile(RecordedGraph, Signature) : FRFGCompileResult{};
}

void FRFGInstance::Execute(
    const FRFGCompileResult& CompileResult,
    const FRFGRecordedGraph& RecordedGraph,
    FRFGExecutionContext& ExecutionContext,
    const FRFGExecuteOptions& ExecuteOptions)
{
    if (Executor == nullptr || !CompileResult.Plan)
    {
        return;
    }

    Executor->Execute(*CompileResult.Plan, RecordedGraph, ExecutionContext, ExecuteOptions);
}

void FRFGInstance::SetCompileOptions(const FRFGCompileOptions& InCompileOptions)
{
    if (Runtime != nullptr)
    {
        Runtime->SetCompileOptions(InCompileOptions);
    }
}

const FRFGCompileOptions& FRFGInstance::GetCompileOptions() const
{
    static FRFGCompileOptions DefaultOptions;
    return Runtime != nullptr ? Runtime->GetCompileOptions() : DefaultOptions;
}

} // namespace LE
