#pragma once

#include "CoreMinimal.h"
#include "Core/RFGHandles.h"
#include "Core/RFGTypes.h"

#include <vector>

struct FRFGDependencyEdge
{
    FRFGPassHandle SourcePass;
    FRFGPassHandle TargetPass;
    ERFGHazardType Hazard = ERFGHazardType::RAW;
};

struct FRFGBarrierTransition
{
    FRFGResourceHandle Resource;
    ERFGPipelineStage BeforeStage = ERFGPipelineStage::None;
    ERFGPipelineStage AfterStage = ERFGPipelineStage::None;
    ERFGAccessType BeforeAccess = ERFGAccessType::None;
    ERFGAccessType AfterAccess = ERFGAccessType::None;
};

struct FRFGCompiledPass
{
    FRFGPassHandle Handle;
    FString Name;
    ERFGQueueType Queue = ERFGQueueType::Graphics;
    ERFGPassFlags Flags = ERFGPassFlags::None;

    std::vector<FRFGDependencyEdge> IncomingEdges;
    std::vector<FRFGBarrierTransition> PreBarriers;
    std::vector<FRFGBarrierTransition> PostBarriers;
};

struct FRFGCompiledResourceLife
{
    FRFGResourceHandle Resource;
    uint32 FirstPassIndex = 0xFFFFFFFFu;
    uint32 LastPassIndex = 0xFFFFFFFFu;
    bool bTransient = true;
};

class RFG_API FRFGCompiledPlan
{
public:
    FRFGCompiledPlan() = default;
    ~FRFGCompiledPlan() = default;

public:
    const FRFGGraphSignature& GetSignature() const;
    const std::vector<FRFGCompiledPass>& GetPasses() const;
    const std::vector<FRFGCompiledResourceLife>& GetResourceLifetimes() const;

public:
    FRFGGraphSignature& GetMutableSignature();
    std::vector<FRFGCompiledPass>& GetMutablePasses();
    std::vector<FRFGCompiledResourceLife>& GetMutableResourceLifetimes();

public:
    void Clear();

private:
    FRFGGraphSignature Signature;
    std::vector<FRFGCompiledPass> CompiledPasses;
    std::vector<FRFGCompiledResourceLife> ResourceLifetimes;
};
