#pragma once

#include "CoreMinimal.h"
#include "PlatformRenderProfile.h"
#include "ShadingPath.h"
#include "RAL/RALCommandList.h"


namespace LE
{

class FRALSwapchain;
class FRALTextureView;

struct FRenderView
{
    uint32 ViewId = 0;
    LE::FRALViewport Viewport;
    ERenderPlatformProfile Profile = ERenderPlatformProfile::Desktop;
    EShadingPath ShadingPath = EShadingPath::Deferred;
    bool bIsEditorView = false;
};

struct FRenderTargetBinding
{
    LE::FRALTextureView* BackBuffer = nullptr;
    LE::FRALTextureView* SceneColor = nullptr;
    LE::FRALTextureView* DepthStencil = nullptr;
};

struct FRenderViewFamily
{
    LE::Array<FRenderView> Views;
    LE::FRALSwapchain* PrimarySwapchain = nullptr;
    FRenderTargetBinding RenderTargets;
};

} // namespace LE
