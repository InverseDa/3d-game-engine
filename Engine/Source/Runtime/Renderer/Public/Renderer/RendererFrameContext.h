#pragma once

#include "CoreMinimal.h"
#include "RenderView.h"

class FRALCommandList;
class FRALDevice;
class FRALSwapchain;
class IRenderPipeline;

struct FRendererFrameContext
{
    uint64 FrameIndex = 0;
    FRALDevice* Device = nullptr;
    FRALSwapchain* Swapchain = nullptr;
    FRALCommandList* CommandList = nullptr;
    FRenderViewFamily ViewFamily;
    IRenderPipeline* Pipeline = nullptr;
    bool bPresentAfterRender = true;
};
