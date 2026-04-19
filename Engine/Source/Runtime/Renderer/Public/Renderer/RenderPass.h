#pragma once

#include "CoreMinimal.h"
#include "Core/RFGTypes.h"

class FRFGPassContext;
class FRenderGraphBuilderBridge;
struct FRendererFrameContext;
class FRenderScene;
struct FRenderView;

struct FRenderPassSetupContext
{
    FRenderGraphBuilderBridge* GraphBridge = nullptr;
    FRFGPassHandle PassHandle;
    const FRendererFrameContext* FrameContext = nullptr;
    const FRenderScene* RenderScene = nullptr;
    const FRenderView* RenderView = nullptr;
};

struct FRenderPassRecordContext
{
    FRFGPassContext* PassContext = nullptr;
    const FRendererFrameContext* FrameContext = nullptr;
    const FRenderScene* RenderScene = nullptr;
    const FRenderView* RenderView = nullptr;
};

class RENDERER_API IRenderPass
{
public:
    virtual ~IRenderPass() = default;

public:
    virtual const char* GetPassName() const = 0;
    virtual ERFGQueueType GetQueueType() const = 0;

public:
    virtual void Setup(FRenderPassSetupContext& Context) = 0;
    virtual void Record(FRenderPassRecordContext& Context) = 0;
};
