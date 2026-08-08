#pragma once

#include "CoreMinimal.h"
#include "RALTexture.h"
#include "RALResource.h"

namespace LE
{

class FRALSemaphore;

enum class ERALSwapchainStatus : uint8
{
    Success,
    Suboptimal,
    OutOfDate,
    Error,
};

struct FRALAcquireResult
{
    ERALSwapchainStatus Status = ERALSwapchainStatus::Error;
    FRALTextureView* BackBufferView = nullptr;

    bool HasImage() const noexcept
    {
        return (Status == ERALSwapchainStatus::Success || Status == ERALSwapchainStatus::Suboptimal) &&
            BackBufferView != nullptr;
    }
};

class RAL_API FRALSwapchain : public FRALResource
{
public:
    virtual ~FRALSwapchain() = default;

public:
    /** Acquire a back buffer and signal caller-owned synchronization. */
    virtual FRALAcquireResult AcquireNextImage(
        FRALSemaphore* SignalSemaphore,
        uint64 Timeout = UINT64_MAX) = 0;
    /** Valid only between a successful/suboptimal acquire and the following present/resize. */
    virtual FRALTextureView* GetCurrentBackBufferView() const = 0;
    /** Present the acquired image after waiting on caller-owned synchronization. */
    virtual ERALSwapchainStatus Present(FRALSemaphore* WaitSemaphore) = 0;
    /** Explicitly rebuild the swapchain. Never acquires an image. */
    virtual bool Resize(uint32 Width, uint32 Height) = 0;
};

} // namespace LE
