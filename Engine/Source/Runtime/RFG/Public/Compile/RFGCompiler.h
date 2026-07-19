#pragma once

#include "CoreMinimal.h"
#include "Compile/RFGBarrierPlanner.h"
#include "Compile/RFGCompiledPlan.h"
#include "Compile/RFGCuller.h"
#include "Compile/RFGDependencyAnalyzer.h"
#include "Core/RFGTypes.h"

#include <memory>

class FRFGRecordedGraph;

struct FRFGCompileResult
{
    std::shared_ptr<const FRFGCompiledPlan> Plan;
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
        const FRFGGraphSignature& Signature) const;

private:
    FRFGCompileOptions CompileOptions;
    FRFGDependencyAnalyzer DependencyAnalyzer;
    FRFGCuller Culler;
    FRFGBarrierPlanner BarrierPlanner;
};
