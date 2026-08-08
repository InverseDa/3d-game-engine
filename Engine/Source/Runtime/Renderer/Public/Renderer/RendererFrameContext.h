#pragma once

#include "CoreMinimal.h"
#include "RenderView.h"

namespace LE
{

class FRALCommandList;
class FRALDevice;
class FRALSwapchain;
class IRenderPipeline;
class FRenderFrameScope;

struct FRendererFrameContext
{
    uint64 FrameIndex = 0;
    LE::FRALDevice* Device = nullptr;
    LE::FRALSwapchain* Swapchain = nullptr;
    LE::FRALCommandList* CommandList = nullptr;
    /** Borrowed only while the scheduler's record callback is active. */
    LE::FRenderFrameScope* FrameScope = nullptr;
    FRenderViewFamily ViewFamily;
    IRenderPipeline* Pipeline = nullptr;
};

} // namespace LE
