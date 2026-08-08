#include "Renderer/RenderFrameScheduler.h"

#include "RAL/RALCommandAllocator.h"
#include "RAL/RALCommandList.h"
#include "RAL/RALDevice.h"
#include "RAL/RALResource.h"
#include "RAL/RALSyncPrimitives.h"

namespace LE
{

bool FRenderFrameScope::DeferRelease(FRALResource* const Resource)
{
    return Scheduler != nullptr && Scheduler->DeferReleaseFromScope(SlotIndex, Generation, Resource);
}

FRenderFrameScheduler::~FRenderFrameScheduler()
{
    Shutdown();
}

bool FRenderFrameScheduler::Initialize(FRALDevice* const InDevice)
{
    if (bInitialized || InDevice == nullptr || InDevice->GetGraphicsQueue() == nullptr)
    {
        return false;
    }

    Device = InDevice;
    for (uint32 SlotIndex = 0; SlotIndex < FrameSlotCount; ++SlotIndex)
    {
        FFrameSlot& Slot = Slots[SlotIndex];
        Slot.CommandAllocator = Device->CreateCommandAllocator(EQueueType::Graphics);
        if (Slot.CommandAllocator == nullptr)
        {
            DestroySlots();
            Device = nullptr;
            return false;
        }
        Slot.CommandList = Device->CreateCommandList(Slot.CommandAllocator);
        if (Slot.CommandList == nullptr)
        {
            DestroySlots();
            Device = nullptr;
            return false;
        }
        Slot.AcquireSemaphore = Device->CreateBinarySemaphore();
        if (Slot.AcquireSemaphore == nullptr)
        {
            DestroySlots();
            Device = nullptr;
            return false;
        }
        Slot.RenderFinishedSemaphore = Device->CreateBinarySemaphore();
        if (Slot.RenderFinishedSemaphore == nullptr)
        {
            DestroySlots();
            Device = nullptr;
            return false;
        }
        Slot.CompletionFence = Device->CreateFence(true);
        if (Slot.CompletionFence == nullptr)
        {
            DestroySlots();
            Device = nullptr;
            return false;
        }
    }

    NextFrameIndex = 0;
    NextSlotIndex = 0;
    bTerminal = false;
    bInitialized = true;
    return true;
}

void FRenderFrameScheduler::Shutdown() noexcept
{
    if (Device == nullptr)
    {
        bInitialized = false;
        return;
    }

    bool bHasUnsignaledFailedSlot = false;
    for (const FFrameSlot& Slot : Slots)
    {
        bHasUnsignaledFailedSlot |= Slot.State == ESlotState::UnsignaledAfterSubmitFailure;
    }

    if (bHasUnsignaledFailedSlot && Device->GetGraphicsQueue() != nullptr)
    {
        // A failed submit may leave its reset fence permanently unsignaled. Queue
        // idle is an exceptional terminal recovery path; never wait that fence.
        Device->GetGraphicsQueue()->WaitIdle();
        for (FFrameSlot& Slot : Slots)
        {
            if (Slot.State == ESlotState::InFlight)
            {
                Slot.State = ESlotState::Ready;
            }
        }
    }
    else
    {
        WaitForAllFrames();
    }

    DestroySlots();
    Device = nullptr;
    NextFrameIndex = 0;
    NextSlotIndex = 0;
    bTerminal = false;
    bInitialized = false;
}

FRenderFrameResult FRenderFrameScheduler::ExecuteFrame(
    FRALSwapchain* const Swapchain,
    const FRenderFrameCallback& Callback)
{
    FRenderFrameResult Result;
    Result.FrameIndex = NextFrameIndex;
    Result.SlotIndex = NextSlotIndex;
    if (!bInitialized || bTerminal || Swapchain == nullptr || !Callback)
    {
        return Result;
    }

    FFrameSlot& Slot = Slots[NextSlotIndex];
    if (Slot.State == ESlotState::UnsignaledAfterSubmitFailure)
    {
        bTerminal = true;
        return Result;
    }

    Slot.CompletionFence->Wait();
    Slot.State = ESlotState::Ready;
    FlushDeferredReleases(Slot);
    if (!Slot.CommandAllocator->Reset())
    {
        bTerminal = true;
        return Result;
    }

    const FRALAcquireResult AcquireResult = Swapchain->AcquireNextImage(Slot.AcquireSemaphore);
    Result.AcquireStatus = AcquireResult.Status;
    if (!AcquireResult.HasImage())
    {
        Result.Action = AcquireResult.Status == ERALSwapchainStatus::OutOfDate
            ? ERenderFrameAction::RecreateSwapchain
            : ERenderFrameAction::Exit;
        bTerminal = AcquireResult.Status != ERALSwapchainStatus::OutOfDate;
        return Result;
    }

    ++Slot.Generation;
    Slot.bCallbackActive = true;
    FRenderFrameScope FrameScope;
    FrameScope.Scheduler = this;
    FrameScope.CommandList = Slot.CommandList;
    FrameScope.BackBufferView = AcquireResult.BackBufferView;
    FrameScope.FrameIndex = NextFrameIndex;
    FrameScope.Generation = Slot.Generation;
    FrameScope.SlotIndex = NextSlotIndex;
    Result.RecordResult = Callback(FrameScope);
    Slot.bCallbackActive = false;
    FrameScope.Scheduler = nullptr;

    if (Result.RecordResult != ERenderFrameRecordResult::Success)
    {
        bTerminal = true;
        return Result;
    }

    FRALSubmitInfo SubmitInfo;
    SubmitInfo.CmdList = Slot.CommandList;
    SubmitInfo.WaitSemaphores.PushBack(Slot.AcquireSemaphore);
    SubmitInfo.SignalSemaphores.PushBack(Slot.RenderFinishedSemaphore);
    SubmitInfo.FenceToSignal = Slot.CompletionFence;
    Slot.CompletionFence->Reset();
    Result.SubmitResult = Device->GetGraphicsQueue()->Submit(SubmitInfo);
    if (Result.SubmitResult != ERALQueueSubmitResult::Success)
    {
        Slot.State = ESlotState::UnsignaledAfterSubmitFailure;
        bTerminal = true;
        return Result;
    }

    Slot.State = ESlotState::InFlight;
    Result.PresentStatus = Swapchain->Present(Slot.RenderFinishedSemaphore);
    ++NextFrameIndex;
    NextSlotIndex = (NextSlotIndex + 1) % FrameSlotCount;

    if (Result.PresentStatus == ERALSwapchainStatus::Error)
    {
        bTerminal = true;
        return Result;
    }

    Result.Action = AcquireResult.Status == ERALSwapchainStatus::Suboptimal ||
        Result.PresentStatus == ERALSwapchainStatus::Suboptimal ||
        Result.PresentStatus == ERALSwapchainStatus::OutOfDate
        ? ERenderFrameAction::RecreateSwapchain
        : ERenderFrameAction::Continue;
    return Result;
}

void FRenderFrameScheduler::WaitForAllFrames()
{
    if (!bInitialized)
    {
        return;
    }

    for (FFrameSlot& Slot : Slots)
    {
        if (Slot.State == ESlotState::InFlight)
        {
            Slot.CompletionFence->Wait();
            Slot.State = ESlotState::Ready;
        }
        if (Slot.State == ESlotState::Ready)
        {
            FlushDeferredReleases(Slot);
        }
    }
}

bool FRenderFrameScheduler::DeferReleaseFromScope(
    const uint32 SlotIndex,
    const uint64 Generation,
    FRALResource* const Resource)
{
    if (!bInitialized || Resource == nullptr || SlotIndex >= FrameSlotCount)
    {
        return false;
    }

    FFrameSlot& Slot = Slots[SlotIndex];
    if (!Slot.bCallbackActive || Slot.Generation != Generation)
    {
        return false;
    }
    for (FRALResource* const Deferred : Slot.DeferredReleases)
    {
        if (Deferred == Resource)
        {
            return false;
        }
    }
    Slot.DeferredReleases.PushBack(Resource);
    return true;
}

void FRenderFrameScheduler::FlushDeferredReleases(FFrameSlot& Slot) noexcept
{
    for (FRALResource* const Resource : Slot.DeferredReleases)
    {
        RAL::DestroyResource(Resource);
    }
    Slot.DeferredReleases.Clear();
}

void FRenderFrameScheduler::DestroySlots() noexcept
{
    for (uint32 ReverseIndex = FrameSlotCount; ReverseIndex > 0; --ReverseIndex)
    {
        FFrameSlot& Slot = Slots[ReverseIndex - 1];
        FlushDeferredReleases(Slot);
        // A Vulkan command buffer must be freed before its owning command pool.
        RAL::DestroyResource(Slot.CommandList);
        Slot.CommandList = nullptr;
        RAL::DestroyResource(Slot.CommandAllocator);
        Slot.CommandAllocator = nullptr;
        RAL::DestroyResource(Slot.CompletionFence);
        Slot.CompletionFence = nullptr;
        RAL::DestroyResource(Slot.RenderFinishedSemaphore);
        Slot.RenderFinishedSemaphore = nullptr;
        RAL::DestroyResource(Slot.AcquireSemaphore);
        Slot.AcquireSemaphore = nullptr;
        Slot.Generation = 0;
        Slot.State = ESlotState::Ready;
        Slot.bCallbackActive = false;
    }
}

} // namespace LE
