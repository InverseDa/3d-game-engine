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
            Result.Plan = std::const_pointer_cast<FRFGCompiledPlan>(CachedPlan);
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
    if (PlanCache != nullptr && CompiledPlan != nullptr)
    {
        PlanCache->Store(Signature, CompiledPlan);
    }
}

void FRFGRuntime::SetCompileOptions(const FRFGCompileOptions& InCompileOptions)
{
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
