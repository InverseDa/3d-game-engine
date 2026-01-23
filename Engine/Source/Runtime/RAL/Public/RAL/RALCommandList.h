#pragma once

#include "CoreMinimal.h"
#include "RALBuffer.h"
#include "RALResource.h"
#include "RALPipeline.h"
#include "RALDescription.h"

class RAL_API FRALCommandList : public FRALResource
{
public:
    virtual ~FRALCommandList() = default;

public:
    virtual void Begin() = 0;
    virtual void End() = 0;

public:
    virtual void BeginRenderPass(const FRALRenderPassDesc& Desc) = 0;
    virtual void EndRenderPass() = 0;

public:
    virtual void SetGraphicsPipeline(FRALGraphicsPipeline* Pipeline) = 0;
    virtual void SetViewport(const FRALViewport& Viewport) = 0;
    virtual void SetScissorRect(const FRALScissorRect& Scissor) = 0;

public:
    virtual void SetVertexBuffer(uint32 Slot, FRALBuffer* Buffer, uint64 Offset = 0) = 0;
    virtual void SetIndexBuffer(FRALBuffer* Buffer, uint64 Offset = 0, EPixelFormat IndexFormat = EPixelFormat::R32_UINT) = 0;
    // 0: Global, 1: Pass, 2: Material, 3: Object
    virtual void SetBindGroup(uint32 SetIndex, FRALBindGroup* BindGroup) = 0;

public:
    virtual void Draw(uint32 VertexCount, uint32 InstanceCount = 1, uint32 FirstInstance = 0) = 0;
    virtual void DrawIndexed(uint32 IndexCount, uint32 InstanceCount = 1, uint32 FirstIndex = 0, int32 VertextOffset = 0, uint32 FirstInstance = 0) = 0;
};
