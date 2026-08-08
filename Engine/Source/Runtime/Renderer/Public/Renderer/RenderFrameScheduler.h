#pragma once

#include "CoreMinimal.h"
#include "RAL/RALQueue.h"
#include "RAL/RALSwapchain.h"

namespace LE
{

class FRALCommandAllocator;
class FRALCommandList;
class FRALDevice;
class FRALFence;
class FRALResource;
class FRALSemaphore;
class FRenderFrameScheduler;

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
    ERALSwapchainStatus AcquireStatus = ERALSwapchainStatus::Error;
    ERALQueueSubmitResult SubmitResult = ERALQueueSubmitResult::InvalidArguments;
    ERALSwapchainStatus PresentStatus = ERALSwapchainStatus::Error;
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
    FRALCommandList* GetCommandList() const noexcept { return CommandList; }
    FRALTextureView* GetBackBufferView() const noexcept { return BackBufferView; }
    uint64 GetFrameIndex() const noexcept { return FrameIndex; }
    uint32 GetSlotIndex() const noexcept { return SlotIndex; }

    bool DeferRelease(FRALResource* Resource);

private:
    friend class FRenderFrameScheduler;

    FRenderFrameScheduler* Scheduler = nullptr;
    FRALCommandList* CommandList = nullptr;
    FRALTextureView* BackBufferView = nullptr;
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
    bool Initialize(FRALDevice* InDevice);
    void Shutdown() noexcept;
    bool IsInitialized() const noexcept { return bInitialized; }

    /** Callback records only; the scheduler exclusively owns fence reset, queue submit, and present. */
    FRenderFrameResult ExecuteFrame(FRALSwapchain* Swapchain, const FRenderFrameCallback& Callback);
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
        FRALCommandAllocator* CommandAllocator = nullptr;
        FRALCommandList* CommandList = nullptr;
        FRALSemaphore* AcquireSemaphore = nullptr;
        FRALSemaphore* RenderFinishedSemaphore = nullptr;
        FRALFence* CompletionFence = nullptr;
        LE::Array<FRALResource*> DeferredReleases;
        uint64 Generation = 0;
        ESlotState State = ESlotState::Ready;
        bool bCallbackActive = false;
    };

    bool DeferReleaseFromScope(uint32 SlotIndex, uint64 Generation, FRALResource* Resource);
    void FlushDeferredReleases(FFrameSlot& Slot) noexcept;
    void DestroySlots() noexcept;

    friend class FRenderFrameScope;

    FRALDevice* Device = nullptr;
    FFrameSlot Slots[FrameSlotCount];
    uint64 NextFrameIndex = 0;
    uint32 NextSlotIndex = 0;
    bool bInitialized = false;
    bool bTerminal = false;
};

} // namespace LE
