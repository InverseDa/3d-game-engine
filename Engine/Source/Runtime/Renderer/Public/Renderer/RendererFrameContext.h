#pragma once

#include "CoreMinimal.h"
#include "RenderView.h"

namespace LE
{

class FRHICommandList;
class FRHIDevice;
class FRHISwapchain;
class IRenderPipeline;
class FRenderFrameScope;

struct FRendererFrameContext
{
    uint64 FrameIndex = 0;
    LE::FRHIDevice* Device = nullptr;
    LE::FRHISwapchain* Swapchain = nullptr;
    LE::FRHICommandList* CommandList = nullptr;
    /** Borrowed only while the scheduler's record callback is active. */
    LE::FRenderFrameScope* FrameScope = nullptr;
    FRenderViewFamily ViewFamily;
    IRenderPipeline* Pipeline = nullptr;
};

} // namespace LE
