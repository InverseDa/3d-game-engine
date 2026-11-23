#pragma once

#include "CoreMinimal.h"

namespace LE
{

class FRFGRecordedGraph;
class FRFGCompiledPlan;

enum class ERFGGraphExportFormat : uint8
{
    Dot,
    Json,
};

class RFG_API FRFGGraphExporter
{
public:
    FRFGGraphExporter() = default;
    ~FRFGGraphExporter() = default;

public:
    LE::String ExportRecordedGraph(const FRFGRecordedGraph& RecordedGraph, ERFGGraphExportFormat Format) const;
    LE::String ExportCompiledPlan(const FRFGCompiledPlan& CompiledPlan, ERFGGraphExportFormat Format) const;
};

} // namespace LE
