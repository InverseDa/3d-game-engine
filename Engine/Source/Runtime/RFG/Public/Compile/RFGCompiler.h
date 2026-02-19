#pragma once

#include "CoreMinimal.h"
#include "Compile/RFGBarrierPlanner.h"
#include "Compile/RFGCompiledPlan.h"
#include "Compile/RFGCuller.h"
#include "Compile/RFGDependencyAnalyzer.h"
#include "Compile/RFGPlanCache.h"
#include "Core/RFGTypes.h"

#include <memory>

class FRFGRecordedGraph;

struct FRFGCompileResult
{
    std::shared_ptr<FRFGCompiledPlan> Plan;
    bool bFromCache = false;
};

class RFG_API FRFGCompiler
{
public:
    FRFGCompiler() = default;
    ~FRFGCompiler() = default;

public:
    void SetCompileOptions(const FRFGCompileOptions& InCompileOptions);
    const FRFGCompileOptions& GetCompileOptions() const;

public:
    FRFGCompileResult Compile(
        const FRFGRecordedGraph& RecordedGraph,
        const FRFGGraphSignature& Signature,
        FRFGPlanCache* PlanCache = nullptr) const;

private:
    FRFGCompileOptions CompileOptions;
    FRFGDependencyAnalyzer DependencyAnalyzer;
    FRFGCuller Culler;
    FRFGBarrierPlanner BarrierPlanner;
};
