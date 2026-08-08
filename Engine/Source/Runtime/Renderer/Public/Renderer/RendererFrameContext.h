#pragma once

#include "CoreMinimal.h"
#include "RenderView.h"

namespace LE
{

class FRALCommandList;
class FRALDevice;
class FRALSwapchain;
class IRenderPipeline;

struct FRendererFrameContext
{
    uint64 FrameIndex = 0;
    LE::FRALDevice* Device = nullptr;
    LE::FRALSwapchain* Swapchain = nullptr;
    LE::FRALCommandList* CommandList = nullptr;
    FRenderViewFamily ViewFamily;
    IRenderPipeline* Pipeline = nullptr;
    bool bPresentAfterRender = true;
};

} // namespace LE
