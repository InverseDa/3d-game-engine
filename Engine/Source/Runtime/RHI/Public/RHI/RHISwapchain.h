#pragma once

#include "CoreMinimal.h"
#include "RHITexture.h"
#include "RHIResource.h"

namespace LE
{

class FRHISemaphore;

enum class ERHISwapchainStatus : uint8
{
    Success,
    Suboptimal,
    OutOfDate,
    Error,
};

struct FRHIAcquireResult
{
    ERHISwapchainStatus Status = ERHISwapchainStatus::Error;
    FRHITextureView* BackBufferView = nullptr;

    bool HasImage() const noexcept
    {
        return (Status == ERHISwapchainStatus::Success || Status == ERHISwapchainStatus::Suboptimal) &&
            BackBufferView != nullptr;
    }
};

class RHI_API FRHISwapchain : public FRHIResource
{
public:
    virtual ~FRHISwapchain() = default;

public:
    /** Acquire a back buffer and signal caller-owned synchronization. */
    virtual FRHIAcquireResult AcquireNextImage(
        FRHISemaphore* SignalSemaphore,
        uint64 Timeout = UINT64_MAX) = 0;
    /** Valid only between a successful/suboptimal acquire and the following present/resize. */
    virtual FRHITextureView* GetCurrentBackBufferView() const = 0;
    /** Present the acquired image after waiting on caller-owned synchronization. */
    virtual ERHISwapchainStatus Present(FRHISemaphore* WaitSemaphore) = 0;
    /** Explicitly rebuild the swapchain. Never acquires an image. */
    virtual bool Resize(uint32 Width, uint32 Height) = 0;
};

} // namespace LE
