#pragma once

#include "CoreMinimal.h"
#include "Compile/RFGCompiler.h"
#include "Compile/RFGPlanCache.h"
#include "Core/RFGTypes.h"

#include <memory>

class IRFGPassRegistry;
class FRFGRecordedGraph;
class FRFGCompiledPlan;

class RFG_API FRFGRuntime : public FNonCopyable
{
public:
    FRFGRuntime();
    ~FRFGRuntime();

public:
    void Initialize(IRFGPassRegistry* InPassRegistry);
    void Shutdown();

public:
    void SetPassRegistry(IRFGPassRegistry* InPassRegistry);
    IRFGPassRegistry* GetPassRegistry() const;

public:
    FRFGCompileResult Compile(
        const FRFGRecordedGraph& RecordedGraph,
        const FRFGGraphSignature& Signature = {});

public:
    std::shared_ptr<const FRFGCompiledPlan> FindCompiledPlan(const FRFGGraphSignature& Signature) const;
    void StoreCompiledPlan(
        const FRFGGraphSignature& Signature,
        const std::shared_ptr<const FRFGCompiledPlan>& CompiledPlan);

public:
    void SetCompileOptions(const FRFGCompileOptions& InCompileOptions);
    const FRFGCompileOptions& GetCompileOptions() const;

public:
    FRFGPlanCache* GetPlanCache();
    const FRFGPlanCache* GetPlanCache() const;

private:
    IRFGPassRegistry* PassRegistry = nullptr;
    FRFGCompileOptions CompileOptions;

private:
    FRFGCompiler* Compiler = nullptr;
    FRFGPlanCache* PlanCache = nullptr;
};
