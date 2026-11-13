#include "CoreMinimal.h"
#include "Vulkan/VulkanRHI.h"

namespace LE
{

FVulkanRHIQueue::FVulkanRHIQueue(FVulkanRHIDevice* InDevice, uint32 FamilyIndex, uint32 QueueIndex)
	: Device(InDevice)
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
    if (this->Handle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRHI, Warn, "WaitIdle skipped: queue handle is null.");
        return;
    }
    const VkResult Result = vkQueueWaitIdle(this->Handle);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRHI, Error, "vkQueueWaitIdle failed. VkResult={}", static_cast<int32>(Result));
    }
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
    if (SubmitInfo.CmdList)
    {
        CmdBufferHandle = static_cast<FVulkanRHICommandList*>(SubmitInfo.CmdList)->Handle;
        if (CmdBufferHandle == VK_NULL_HANDLE)
        {
            LE_LOG(LogRHI, Error, "Submit rejected a command list with a null Vulkan handle.");
            return ERHIQueueSubmitResult::InvalidArguments;
        }
    }

    LE::Array<VkSemaphore> WaitSemaphores;
    LE::Array<VkPipelineStageFlags> WaitStages;
    for (auto* Semaphore : SubmitInfo.WaitSemaphores)
    {
        if (Semaphore == nullptr)
        {
            LE_LOG(LogRHI, Error, "Submit rejected a null wait semaphore.");
            return ERHIQueueSubmitResult::InvalidArguments;
        }
        VkSemaphore Handle = static_cast<FVulkanRHISemaphore*>(Semaphore)->Handle;
        if (Handle == VK_NULL_HANDLE)
        {
            LE_LOG(LogRHI, Error, "Submit rejected a wait semaphore with a null Vulkan handle.");
            return ERHIQueueSubmitResult::InvalidArguments;
        }
        WaitSemaphores.PushBack(Handle);
        WaitStages.PushBack(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    }
    LE::Array<VkSemaphore> SignalSemaphores;
    for (auto* Semaphore : SubmitInfo.SignalSemaphores)
    {
        if (Semaphore == nullptr)
        {
            LE_LOG(LogRHI, Error, "Submit rejected a null signal semaphore.");
            return ERHIQueueSubmitResult::InvalidArguments;
        }
        VkSemaphore Handle = static_cast<FVulkanRHISemaphore*>(Semaphore)->Handle;
        if (Handle == VK_NULL_HANDLE)
        {
            LE_LOG(LogRHI, Error, "Submit rejected a signal semaphore with a null Vulkan handle.");
            return ERHIQueueSubmitResult::InvalidArguments;
        }
        SignalSemaphores.PushBack(Handle);
    }

    VkFence FenceHandle = VK_NULL_HANDLE;
    if (SubmitInfo.FenceToSignal)
    {
        FenceHandle = static_cast<FVulkanRHIFence*>(SubmitInfo.FenceToSignal)->Handle;
        if (FenceHandle == VK_NULL_HANDLE)
        {
            LE_LOG(LogRHI, Error, "Submit rejected a fence with a null Vulkan handle.");
            return ERHIQueueSubmitResult::InvalidArguments;
        }
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
        return ERHIQueueSubmitResult::Error;
    }
    return ERHIQueueSubmitResult::Success;
}

} // namespace LE
