#pragma once

#include "CoreMinimal.h"
#include "RALTexture.h"
#include "RALResource.h"

class RAL_API FRALSwapchain : public FRALResource
{
public:
    virtual ~FRALSwapchain() = default;

public:
    /** Get current frame of backend buffer view */
    virtual FRALTextureView* GetCurrentBackBufferView() const = 0;
    /** Present to the screen */
    virtual void Present() = 0;
    /** Resize swapchain */
    virtual void Resize(uint32 Width, uint32 Height) = 0;
};
