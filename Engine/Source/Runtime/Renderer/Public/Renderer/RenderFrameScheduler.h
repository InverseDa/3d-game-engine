#pragma once

#include "CoreMinimal.h"
#include "RHI/RHIQueue.h"
#include "RHI/RHISwapchain.h"

namespace LE
{

class FRHICommandAllocator;
class FRHICommandList;
class FRHIDevice;
class FRHIFence;
class FRHIResource;
class FRHISemaphore;
class FRenderFrameScheduler;
class FSceneRenderer;

enum class ERenderFrameAction : uint8
{
    Continue,
    RecreateSwapchain,
    Exit,
};

enum class ERenderFrameRecordResult : uint8
{
    Success,
    InvalidArguments,
    Error,
};

struct FRenderFrameResult
{
    ERenderFrameAction Action = ERenderFrameAction::Exit;
    ERenderFrameRecordResult RecordResult = ERenderFrameRecordResult::InvalidArguments;
    ERHISwapchainStatus AcquireStatus = ERHISwapchainStatus::Error;
    ERHIQueueSubmitResult SubmitResult = ERHIQueueSubmitResult::InvalidArguments;
    ERHISwapchainStatus PresentStatus = ERHISwapchainStatus::Error;
    uint64 FrameIndex = 0;
    uint32 SlotIndex = 0;
};

/**
 * Borrowed, synchronous view of the frame slot currently being recorded.
 * DeferRelease transfers ownership only while the ExecuteFrame callback is active.
 */
class RENDERER_API FRenderFrameScope final : public FNonCopyable
{
public:
    FRHICommandList* GetCommandList() const noexcept { return CommandList; }
    FRHITextureView* GetBackBufferView() const noexcept { return BackBufferView; }
    uint64 GetFrameIndex() const noexcept { return FrameIndex; }
    uint32 GetSlotIndex() const noexcept { return SlotIndex; }

    bool DeferRelease(FRHIResource* Resource);

private:
    friend class FRenderFrameScheduler;
    friend class FSceneRenderer;

    FRenderFrameScheduler* Scheduler = nullptr;
    FRHICommandList* CommandList = nullptr;
    FRHITextureView* BackBufferView = nullptr;
    uint64 FrameIndex = 0;
    uint64 Generation = 0;
    uint32 SlotIndex = 0;
};

using FRenderFrameCallback = LE::Function<ERenderFrameRecordResult(FRenderFrameScope& Frame)>;

class RENDERER_API FRenderFrameScheduler final : public FNonCopyable
{
public:
    static constexpr uint32 FrameSlotCount = 2;

    FRenderFrameScheduler() = default;
    ~FRenderFrameScheduler();

public:
    bool Initialize(FRHIDevice* InDevice);
    void Shutdown() noexcept;
    bool IsInitialized() const noexcept { return bInitialized; }

    /** Callback records only; the scheduler exclusively owns fence reset, queue submit, and present. */
    FRenderFrameResult ExecuteFrame(FRHISwapchain* Swapchain, const FRenderFrameCallback& Callback);
    void WaitForAllFrames();

private:
    enum class ESlotState : uint8
    {
        Ready,
        InFlight,
        UnsignaledAfterSubmitFailure,
    };

    struct FFrameSlot
    {
        FRHICommandAllocator* CommandAllocator = nullptr;
        FRHICommandList* CommandList = nullptr;
        FRHISemaphore* AcquireSemaphore = nullptr;
        FRHISemaphore* RenderFinishedSemaphore = nullptr;
        FRHIFence* CompletionFence = nullptr;
        LE::Array<FRHIResource*> DeferredReleases;
        uint64 Generation = 0;
        ESlotState State = ESlotState::Ready;
        bool bCallbackActive = false;
    };

    bool DeferReleaseFromScope(uint32 SlotIndex, uint64 Generation, FRHIResource* Resource);
    bool IsAtFrameBoundaryFor(const FRHIDevice& InDevice) const noexcept;
    void DrainQueueForRetirement();
    void FlushDeferredReleases(FFrameSlot& Slot) noexcept;
    void DestroySlots() noexcept;

    friend class FRenderFrameScope;
    friend class FSceneRenderer;

    FRHIDevice* Device = nullptr;
    FFrameSlot Slots[FrameSlotCount];
    uint64 NextFrameIndex = 0;
    uint32 NextSlotIndex = 0;
    bool bInitialized = false;
    bool bTerminal = false;
    bool bDeviceLost = false;
};

} // namespace LE
