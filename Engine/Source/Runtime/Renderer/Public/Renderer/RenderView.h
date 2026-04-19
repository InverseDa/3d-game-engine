#pragma once

#include "CoreMinimal.h"
#include "PlatformRenderProfile.h"
#include "ShadingPath.h"
#include "RAL/RALCommandList.h"

#include <vector>

class FRALSwapchain;
class FRALTextureView;

struct FRenderView
{
    uint32 ViewId = 0;
    FRALViewport Viewport;
    ERenderPlatformProfile Profile = ERenderPlatformProfile::Desktop;
    EShadingPath ShadingPath = EShadingPath::Deferred;
    bool bIsEditorView = false;
};

struct FRenderTargetBinding
{
    FRALTextureView* BackBuffer = nullptr;
    FRALTextureView* SceneColor = nullptr;
    FRALTextureView* DepthStencil = nullptr;
};

struct FRenderViewFamily
{
    std::vector<FRenderView> Views;
    FRALSwapchain* PrimarySwapchain = nullptr;
    FRenderTargetBinding RenderTargets;
};
