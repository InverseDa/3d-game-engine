#include "CoreMinimal.h"
#include "Vulkan/VulkanRHI.h"

namespace LE
{

FVulkanRHIQueue::FVulkanRHIQueue(FVulkanRHIDevice* InDevice, uint32 FamilyIndex, uint32 QueueIndex) : Device(InDevice)
{
    if (Device == nullptr || Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRHI, Error, "FVulkanRHIQueue ctor failed: invalid device.");
        this->Handle = VK_NULL_HANDLE;
        return;
    }

    vkGetDeviceQueue(Device->VkContext.LogicalDevice, FamilyIndex, QueueIndex, &this->Handle);
    if (this->Handle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRHI, Error, "vkGetDeviceQueue failed. FamilyIndex={}, QueueIndex={}", FamilyIndex, QueueIndex);
        return;
    }
    LE_LOG(LogRHI, Info, "Vulkan queue acquired. FamilyIndex={}, QueueIndex={}", FamilyIndex, QueueIndex);
}

void FVulkanRHIQueue::WaitIdle()
{
    static_cast<void>(WaitIdleChecked());
}

ERHIFenceWaitResult FVulkanRHIQueue::WaitIdleChecked()
{
    if (this->Handle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRHI, Warn, "WaitIdle skipped: queue handle is null.");
        return ERHIFenceWaitResult::Error;
    }
    const VkResult Result = vkQueueWaitIdle(this->Handle);
    if (Result == VK_SUCCESS)
    {
        return ERHIFenceWaitResult::Success;
    }
    LE_LOG(LogRHI, Error, "vkQueueWaitIdle failed. VkResult={}", static_cast<int32>(Result));
    return Result == VK_ERROR_DEVICE_LOST ? ERHIFenceWaitResult::DeviceLost : ERHIFenceWaitResult::Error;
}

ERHIQueueSubmitResult FVulkanRHIQueue::Submit(const FRHISubmitInfo& SubmitInfo)
{
    if (!SubmitInfo.IsStructurallyValid())
    {
        LE_LOG(LogRHI, Error, "Submit rejected structurally invalid submit info.");
        return ERHIQueueSubmitResult::InvalidArguments;
    }
    if (this->Handle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRHI, Error, "Submit failed: queue handle is null.");
        return ERHIQueueSubmitResult::Error;
    }

    VkCommandBuffer CmdBufferHandle = VK_NULL_HANDLE;
    auto* const CommandList = dynamic_cast<FVulkanRHICommandList*>(SubmitInfo.CmdList);
    if (SubmitInfo.CmdList != nullptr)
    {
        if (CommandList == nullptr || CommandList->Device != Device || CommandList->Handle == VK_NULL_HANDLE || !CommandList->bExecutable)
        {
            LE_LOG(LogRHI, Error, "Submit rejected an invalid or non-executable command list.");
            return ERHIQueueSubmitResult::InvalidArguments;
        }
        CmdBufferHandle = CommandList->Handle;
    }

    LE::Array<VkSemaphore> WaitSemaphores;
    LE::Array<VkPipelineStageFlags> WaitStages;
    for (auto* Semaphore : SubmitInfo.WaitSemaphores)
    {
        const auto* VulkanSemaphore = dynamic_cast<FVulkanRHISemaphore*>(Semaphore);
        if (VulkanSemaphore == nullptr || VulkanSemaphore->Device != Device || VulkanSemaphore->Handle == VK_NULL_HANDLE)
        {
            LE_LOG(LogRHI, Error, "Submit rejected an invalid wait semaphore.");
            return ERHIQueueSubmitResult::InvalidArguments;
        }
        if (!WaitSemaphores.TryPushBack(VulkanSemaphore->Handle) || !WaitStages.TryPushBack(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT))
        {
            return ERHIQueueSubmitResult::Error;
        }
    }
    LE::Array<VkSemaphore> SignalSemaphores;
    for (auto* Semaphore : SubmitInfo.SignalSemaphores)
    {
        const auto* VulkanSemaphore = dynamic_cast<FVulkanRHISemaphore*>(Semaphore);
        if (VulkanSemaphore == nullptr || VulkanSemaphore->Device != Device || VulkanSemaphore->Handle == VK_NULL_HANDLE)
        {
            LE_LOG(LogRHI, Error, "Submit rejected an invalid signal semaphore.");
            return ERHIQueueSubmitResult::InvalidArguments;
        }
        if (!SignalSemaphores.TryPushBack(VulkanSemaphore->Handle))
        {
            return ERHIQueueSubmitResult::Error;
        }
    }

    VkFence FenceHandle = VK_NULL_HANDLE;
    if (SubmitInfo.FenceToSignal)
    {
        const auto* Fence = dynamic_cast<FVulkanRHIFence*>(SubmitInfo.FenceToSignal);
        if (Fence == nullptr || Fence->Device != Device || Fence->Handle == VK_NULL_HANDLE)
        {
            LE_LOG(LogRHI, Error, "Submit rejected an invalid fence.");
            return ERHIQueueSubmitResult::InvalidArguments;
        }
        FenceHandle = Fence->Handle;
    }

    VkSubmitInfo Info{};
    {
        Info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        Info.pNext = nullptr;

        Info.waitSemaphoreCount = static_cast<uint32>(WaitSemaphores.Size());
        Info.pWaitSemaphores = WaitSemaphores.IsEmpty() ? nullptr : WaitSemaphores.Data();
        Info.pWaitDstStageMask = WaitStages.IsEmpty() ? nullptr : WaitStages.Data();

        Info.commandBufferCount = CmdBufferHandle ? 1 : 0;
        Info.pCommandBuffers = CmdBufferHandle ? &CmdBufferHandle : nullptr;

        Info.signalSemaphoreCount = static_cast<uint32>(SignalSemaphores.Size());
        Info.pSignalSemaphores = SignalSemaphores.IsEmpty() ? nullptr : SignalSemaphores.Data();
    }

    if (Info.commandBufferCount == 0 && Info.waitSemaphoreCount == 0 && Info.signalSemaphoreCount == 0 && FenceHandle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRHI, Error, "Submit rejected empty submit info.");
        return ERHIQueueSubmitResult::InvalidArguments;
    }

    const VkResult Result = vkQueueSubmit(this->Handle, 1, &Info, FenceHandle);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRHI, Error, "vkQueueSubmit failed. VkResult={}", static_cast<int32>(Result));
        return Result == VK_ERROR_DEVICE_LOST ? ERHIQueueSubmitResult::DeviceLost : ERHIQueueSubmitResult::Error;
    }
    if (CommandList != nullptr)
    {
        // BeginChecked uses ONE_TIME_SUBMIT_BIT.
        CommandList->bExecutable = false;
    }
    return ERHIQueueSubmitResult::Success;
}

} // namespace LE
