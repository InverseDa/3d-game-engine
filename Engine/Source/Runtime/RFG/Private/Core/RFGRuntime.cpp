#include "Core/RFGRuntime.h"

FRFGRuntime::FRFGRuntime()
    : Compiler(new FRFGCompiler())
    , PlanCache(new FRFGPlanCache())
{
}

FRFGRuntime::~FRFGRuntime()
{
    Shutdown();

    delete Compiler;
    Compiler = nullptr;

    delete PlanCache;
    PlanCache = nullptr;
}

void FRFGRuntime::Initialize(IRFGPassRegistry* InPassRegistry)
{
    PassRegistry = InPassRegistry;

    if (Compiler != nullptr)
    {
        Compiler->SetCompileOptions(CompileOptions);
    }
}

void FRFGRuntime::Shutdown()
{
    if (PlanCache != nullptr)
    {
        PlanCache->Clear();
    }

    PassRegistry = nullptr;
}

void FRFGRuntime::SetPassRegistry(IRFGPassRegistry* InPassRegistry)
{
    PassRegistry = InPassRegistry;
}

IRFGPassRegistry* FRFGRuntime::GetPassRegistry() const
{
    return PassRegistry;
}

FRFGCompileResult FRFGRuntime::Compile(const FRFGRecordedGraph& RecordedGraph, const FRFGGraphSignature& Signature)
{
    if (Compiler == nullptr)
    {
        return {};
    }

    if (CompileOptions.bEnablePlanCache && PlanCache != nullptr && Signature.Value != 0)
    {
        if (std::shared_ptr<const FRFGCompiledPlan> CachedPlan = PlanCache->Find(Signature))
        {
            FRFGCompileResult Result;
            Result.Plan = std::move(CachedPlan);
            Result.bFromCache = true;
            return Result;
        }
    }

    FRFGCompileResult Result = Compiler->Compile(RecordedGraph, Signature);
    if (CompileOptions.bEnablePlanCache && PlanCache != nullptr && Result.Plan != nullptr && Signature.Value != 0)
    {
        PlanCache->Store(Signature, Result.Plan);
    }

    return Result;
}

std::shared_ptr<const FRFGCompiledPlan> FRFGRuntime::FindCompiledPlan(const FRFGGraphSignature& Signature) const
{
    return PlanCache != nullptr ? PlanCache->Find(Signature) : nullptr;
}

void FRFGRuntime::StoreCompiledPlan(
    const FRFGGraphSignature& Signature,
    const std::shared_ptr<const FRFGCompiledPlan>& CompiledPlan)
{
    if (PlanCache != nullptr && CompiledPlan != nullptr && Signature.Value != 0)
    {
        PlanCache->Store(Signature, CompiledPlan);
    }
}

void FRFGRuntime::SetCompileOptions(const FRFGCompileOptions& InCompileOptions)
{
    const bool bOptionsChanged =
        CompileOptions.bEnablePassCulling != InCompileOptions.bEnablePassCulling ||
        CompileOptions.bEnableBarrierElision != InCompileOptions.bEnableBarrierElision ||
        CompileOptions.bEnableStateMerging != InCompileOptions.bEnableStateMerging ||
        CompileOptions.bEnablePlanCache != InCompileOptions.bEnablePlanCache ||
        CompileOptions.bDeterministicSort != InCompileOptions.bDeterministicSort;

    if (bOptionsChanged && PlanCache != nullptr)
    {
        // Compile options affect plan contents but are intentionally not part
        // of the graph signature, so cached plans must not survive a change.
        PlanCache->Clear();
    }

    CompileOptions = InCompileOptions;

    if (Compiler != nullptr)
    {
        Compiler->SetCompileOptions(CompileOptions);
    }
}

const FRFGCompileOptions& FRFGRuntime::GetCompileOptions() const
{
    return CompileOptions;
}

FRFGPlanCache* FRFGRuntime::GetPlanCache()
{
    return PlanCache;
}

const FRFGPlanCache* FRFGRuntime::GetPlanCache() const
{
    return PlanCache;
}
