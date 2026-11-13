#pragma once

#include "CoreMinimal.h"
#include "RHIBuffer.h"
#include "RHIResource.h"
#include "RHIPipeline.h"
#include "RHIDescription.h"


namespace LE
{

class FRHIBuffer;
class FRHIPipeline_Graphics;
class FRHIBindGroup;
class FRHITextureView;

struct FRHIViewport
{
    float X = 0.0f;
    float Y = 0.0f;
    float Width = 0.0f;
    float Height = 0.0f;
    float MinDepth = 0.0f;
    float MaxDepth = 1.0f;
};

struct FRHIScissorRect
{
    int32 X = 0;
    int32 Y = 0;
    int32 Width = 0;
    int32 Height = 0;
};

class RHI_API FRHICommandList : public FRHIResource
{
public:
    virtual ~FRHICommandList() = default;

public:
    virtual void Begin() = 0;
    virtual void End() = 0;

    virtual void ResourceBarriers(const FRHIBarrierBatch& Barriers) = 0;

public:
    virtual void BeginRenderPass(const FRHIRenderPassDesc& Desc) = 0;
    virtual void EndRenderPass() = 0;

public:
    virtual void SetGraphicsPipeline(FRHIPipeline_Graphics* Pipeline) = 0;
    virtual void SetViewport(const FRHIViewport& Viewport) = 0;
    virtual void SetScissorRect(const FRHIScissorRect& Scissor) = 0;

public:
    virtual void SetVertexBuffer(uint32 Slot, FRHIBuffer* Buffer, uint64 Offset = 0) = 0;
    virtual void SetIndexBuffer(FRHIBuffer* Buffer, uint64 Offset = 0, EPixelFormat IndexFormat = EPixelFormat::R32_UINT) = 0;
    // 0: Global, 1: Pass, 2: Material, 3: Object
    virtual void SetBindGroup(uint32 SetIndex, FRHIBindGroup* BindGroup) = 0;

public:
    virtual void SetPushConstants(EShaderStage Stage, const void* Data, uint32 Size) = 0;

public:
    virtual void Draw(uint32 VertexCount, uint32 InstanceCount = 1, uint32 FirstInstance = 0) = 0;
    virtual void DrawIndexed(uint32 IndexCount, uint32 InstanceCount = 1, uint32 FirstIndex = 0, int32 VertexOffset = 0, uint32 FirstInstance = 0) = 0;
};

} // namespace LE
