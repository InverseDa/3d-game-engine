#pragma once

#include "CoreMinimal.h"
#include "Core/RFGTypes.h"

namespace LE
{

class FRFGPassContext;
class FRenderGraphBuilderBridge;
struct FRendererFrameContext;
class FRenderScene;
struct FRenderView;

struct FRenderPassSetupContext
{
    FRenderGraphBuilderBridge* GraphBridge = nullptr;
    LE::FRFGPassHandle PassHandle;
    const FRendererFrameContext* FrameContext = nullptr;
    const FRenderScene* RenderScene = nullptr;
    const FRenderView* RenderView = nullptr;
};

struct FRenderPassRecordContext
{
    LE::FRFGPassContext* PassContext = nullptr;
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
    virtual LE::ERFGQueueType GetQueueType() const = 0;

public:
    virtual void Setup(FRenderPassSetupContext& Context) = 0;
    virtual void Record(FRenderPassRecordContext& Context) = 0;
};

} // namespace LE
