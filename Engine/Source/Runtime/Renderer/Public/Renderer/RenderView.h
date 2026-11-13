#pragma once

#include "CoreMinimal.h"
#include "PlatformRenderProfile.h"
#include "ShadingPath.h"
#include "RHI/RHICommandList.h"


namespace LE
{

class FRHISwapchain;
class FRHITextureView;

struct FRenderView
{
    uint32 ViewId = 0;
    LE::FRHIViewport Viewport;
    ERenderPlatformProfile Profile = ERenderPlatformProfile::Desktop;
    EShadingPath ShadingPath = EShadingPath::Deferred;
    bool bIsEditorView = false;
};

struct FRenderTargetBinding
{
    LE::FRHITextureView* BackBuffer = nullptr;
    LE::FRHITextureView* SceneColor = nullptr;
    LE::FRHITextureView* DepthStencil = nullptr;
};

struct FRenderViewFamily
{
    LE::Array<FRenderView> Views;
    LE::FRHISwapchain* PrimarySwapchain = nullptr;
    FRenderTargetBinding RenderTargets;
};

} // namespace LE
