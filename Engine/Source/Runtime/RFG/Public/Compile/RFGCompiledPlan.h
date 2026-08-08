#pragma once

#include "CoreMinimal.h"
#include "Core/RFGHandles.h"
#include "Core/RFGTypes.h"


namespace LE
{

struct FRFGDependencyEdge
{
    FRFGPassHandle SourcePass;
    FRFGPassHandle TargetPass;
    ERFGHazardType Hazard = ERFGHazardType::RAW;
};

struct FRFGBarrierTransition
{
    FRFGResourceHandle Resource;
    LE::ERALResourceState BeforeState = LE::ERALResourceState::Unknown;
    LE::ERALResourceState AfterState  = LE::ERALResourceState::Unknown;
    LE::EShaderStage BeforeShaderStage = LE::EShaderStage::None;
    LE::EShaderStage AfterShaderStage  = LE::EShaderStage::None;
    ERFGPipelineStage BeforeStage = ERFGPipelineStage::None;
    ERFGPipelineStage AfterStage  = ERFGPipelineStage::None;
    ERFGAccessType BeforeAccess   = ERFGAccessType::None;
    ERFGAccessType AfterAccess    = ERFGAccessType::None;
    uint32 BaseMipLevel = 0;
    uint32 MipCount = 1;
    uint32 BaseArrayLayer = 0;
    uint32 LayerCount = 1;
    // SrcQueue == DstQueue: 普通 Pipeline Barrier
    // SrcQueue != DstQueue: Queue Family Ownership Transfer (Release + Acquire)
    ERFGQueueType SrcQueue = ERFGQueueType::Graphics;
    ERFGQueueType DstQueue = ERFGQueueType::Graphics;
};

struct FRFGCompiledPass
{
    FRFGPassHandle Handle;
    LE::String Name;
    ERFGQueueType Queue = ERFGQueueType::Graphics;
    ERFGPassFlags Flags = ERFGPassFlags::None;
    // 拓扑层级：同 DependencyLevel 的 Pass 之间无依赖关系，可在多队列下并行执行
    // 当前单线程实现可忽略，保留供后续 AsyncCompute 调度使用
    uint32 DependencyLevel = 0;

    LE::Array<FRFGDependencyEdge> IncomingEdges;
    LE::Array<FRFGBarrierTransition> PreBarriers;
    LE::Array<FRFGBarrierTransition> PostBarriers;
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
    const LE::Array<FRFGCompiledPass>& GetPasses() const;
    const LE::Array<FRFGCompiledResourceLife>& GetResourceLifetimes() const;

public:
    FRFGGraphSignature& GetMutableSignature();
    LE::Array<FRFGCompiledPass>& GetMutablePasses();
    LE::Array<FRFGCompiledResourceLife>& GetMutableResourceLifetimes();

public:
    void Clear();

private:
    FRFGGraphSignature Signature;
    LE::Array<FRFGCompiledPass> CompiledPasses;
    LE::Array<FRFGCompiledResourceLife> ResourceLifetimes;
};

} // namespace LE
