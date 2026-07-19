#include "Compile/RFGCompiler.h"

#include "Debug/RFGGraphExporter.h"
#include "Record/RFGRecordedGraph.h"

#include <sstream>
#include <unordered_set>

LE_DECLARE_LOG_CATEGORY(LogRFG);

namespace
{
bool ValidateBarrierDeclarations(const FRFGRecordedGraph& RecordedGraph, const FRFGCompiledPlan& CompiledPlan)
{
    bool bValid = true;

    for (const FRFGCompiledPass& CompiledPass : CompiledPlan.GetPasses())
    {
        const FRFGPassNode& PassNode = RecordedGraph.GetPassNode(CompiledPass.Handle);
        std::unordered_set<uint32> AccessedResourceIds;

        for (const FRFGPassResourceAccess& ResourceAccess : PassNode.ResourceAccesses)
        {
            if (!AccessedResourceIds.insert(ResourceAccess.Resource.Id).second)
            {
                LE_LOG(LogRFG, Error,
                    "RFG compile failed: pass '{}' declares resource {} more than once. Pass-internal transitions are not supported.",
                    PassNode.Name.GetData(),
                    ResourceAccess.Resource.Id);
                bValid = false;
            }

            if (ResourceAccess.Access.State == ERALResourceState::Unknown)
            {
                LE_LOG(LogRFG, Error,
                    "RFG compile failed: pass '{}' uses resource {} without an explicit RAL resource state.",
                    PassNode.Name.GetData(),
                    ResourceAccess.Resource.Id);
                bValid = false;
            }

            const FRFGResourceNode& ResourceNode = RecordedGraph.GetResourceNode(ResourceAccess.Resource);
            if (ResourceNode.Desc.Type == ERFGResourceType::Texture)
            {
                const FRFGTextureDesc& TextureDesc = ResourceNode.Desc.Texture;
                const bool bUsesWholeTexture =
                    ResourceAccess.Access.BaseMipLevel == 0 &&
                    ResourceAccess.Access.MipCount == TextureDesc.MipLevels &&
                    ResourceAccess.Access.BaseArrayLayer == 0 &&
                    ResourceAccess.Access.LayerCount == TextureDesc.ArrayLayers;

                if (!bUsesWholeTexture)
                {
                    LE_LOG(LogRFG, Error,
                        "RFG compile failed: pass '{}' uses a partial subresource range for resource {}. Current barrier tracking is whole-resource only.",
                        PassNode.Name.GetData(),
                        ResourceAccess.Resource.Id);
                    bValid = false;
                }
            }
        }
    }

    return bValid;
}
}

FRFGCompileResult FRFGCompiler::Compile(const FRFGRecordedGraph& RecordedGraph, const FRFGGraphSignature& Signature) const
{
    FRFGCompileResult Result;
    std::shared_ptr<FRFGCompiledPlan> MutablePlan = std::make_shared<FRFGCompiledPlan>();

    DependencyAnalyzer.BuildDependencies(RecordedGraph, *MutablePlan);

    if (CompileOptions.bEnablePassCulling)
    {
        Culler.CullPasses(RecordedGraph, *MutablePlan);
    }

    if (!ValidateBarrierDeclarations(RecordedGraph, *MutablePlan))
    {
        return Result;
    }

    BarrierPlanner.BuildResourceLifetimes(RecordedGraph, *MutablePlan);
    BarrierPlanner.BuildBarriers(RecordedGraph, *MutablePlan);
    // Dependency analysis clears its output before rebuilding it. Assign the
    // graph identity only after all mutable compilation phases have completed.
    MutablePlan->GetMutableSignature() = Signature;

    std::ostringstream PassOrderStream;
    for (uint32 PassIndex = 0; PassIndex < MutablePlan->GetPasses().size(); ++PassIndex)
    {
        if (PassIndex > 0)
        {
            PassOrderStream << " -> ";
        }

        const FRFGCompiledPass& Pass = MutablePlan->GetPasses()[PassIndex];
        PassOrderStream << Pass.Name.GetData() << "[L" << Pass.DependencyLevel << ",Q" << static_cast<uint32>(Pass.Queue) << "]";
    }

    LE_LOG(
        LogRFG,
        Info,
        "RFG compile completed. Signature={}, Passes={}, Resources={}",
        Signature.Value,
        MutablePlan->GetPasses().size(),
        RecordedGraph.GetResourceNodes().size());
    LE_LOG(LogRFG, Info, "RFG pass order: {}", PassOrderStream.str());
    // TODO(rfg): FRFGGraphExporter::ExportToString is declared but not implemented yet.
    // Replace with a lightweight trace summary until the exporter is fully wired up.
    LE_LOG(LogRFG, Trace, "RFG graph summary: passes={}, resources={}",
        RecordedGraph.GetPassNodes().size(),
        RecordedGraph.GetResourceNodes().size());

    Result.Plan = std::move(MutablePlan);
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
