#include "Compile/RFGCompiler.h"

#include "Debug/RFGGraphExporter.h"
#include "Record/RFGRecordedGraph.h"

#include <sstream>

LE_DECLARE_LOG_CATEGORY(LogRFG);

FRFGCompileResult FRFGCompiler::Compile(const FRFGRecordedGraph& RecordedGraph, const FRFGGraphSignature& Signature) const
{
    FRFGCompileResult Result;
    Result.Plan = std::make_shared<FRFGCompiledPlan>();
    Result.Plan->GetMutableSignature() = Signature;

    DependencyAnalyzer.BuildDependencies(RecordedGraph, *Result.Plan);

    if (CompileOptions.bEnablePassCulling)
    {
        Culler.CullPasses(RecordedGraph, *Result.Plan);
    }

    BarrierPlanner.BuildResourceLifetimes(RecordedGraph, *Result.Plan);
    BarrierPlanner.BuildBarriers(RecordedGraph, *Result.Plan);

    std::ostringstream PassOrderStream;
    for (uint32 PassIndex = 0; PassIndex < Result.Plan->GetPasses().size(); ++PassIndex)
    {
        if (PassIndex > 0)
        {
            PassOrderStream << " -> ";
        }

        const FRFGCompiledPass& Pass = Result.Plan->GetPasses()[PassIndex];
        PassOrderStream << Pass.Name.GetData() << "[L" << Pass.DependencyLevel << ",Q" << static_cast<uint32>(Pass.Queue) << "]";
    }

    LE_LOG(
        LogRFG,
        Info,
        "RFG compile completed. Signature={}, Passes={}, Resources={}",
        Signature.Value,
        Result.Plan->GetPasses().size(),
        RecordedGraph.GetResourceNodes().size());
    LE_LOG(LogRFG, Info, "RFG pass order: {}", PassOrderStream.str());
    // TODO(rfg): FRFGGraphExporter::ExportToString is declared but not implemented yet.
    // Replace with a lightweight trace summary until the exporter is fully wired up.
    LE_LOG(LogRFG, Trace, "RFG graph summary: passes={}, resources={}",
        RecordedGraph.GetPassNodes().size(),
        RecordedGraph.GetResourceNodes().size());

    return Result;
}

void FRFGCompiler::SetCompileOptions(const FRFGCompileOptions& InCompileOptions)
{
    CompileOptions = InCompileOptions;
}

const FRFGCompileOptions& FRFGCompiler::GetCompileOptions() const
{
    return CompileOptions;
}
