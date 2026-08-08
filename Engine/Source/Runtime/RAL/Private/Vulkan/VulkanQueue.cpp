#include "CoreMinimal.h"
#include "Vulkan/VulkanRAL.h"

namespace LE
{

FVulkanRALQueue::FVulkanRALQueue(FVulkanRALDevice* InDevice, uint32 FamilyIndex, uint32 QueueIndex)
	: Device(InDevice)
{
    if (Device == nullptr || Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "FVulkanRALQueue ctor failed: invalid device.");
        this->Handle = VK_NULL_HANDLE;
        return;
    }

	vkGetDeviceQueue(Device->VkContext.LogicalDevice, FamilyIndex, QueueIndex, &this->Handle);
    if (this->Handle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "vkGetDeviceQueue failed. FamilyIndex={}, QueueIndex={}", FamilyIndex, QueueIndex);
        return;
    }
    LE_LOG(LogRAL, Info, "Vulkan queue acquired. FamilyIndex={}, QueueIndex={}", FamilyIndex, QueueIndex);
}

void FVulkanRALQueue::WaitIdle()
{
    if (this->Handle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Warn, "WaitIdle skipped: queue handle is null.");
        return;
    }
    const VkResult Result = vkQueueWaitIdle(this->Handle);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkQueueWaitIdle failed. VkResult={}", static_cast<int32>(Result));
    }
}

void FVulkanRALQueue::Submit(const FRALSubmitInfo& SubmitInfo)
{
    if (this->Handle == VK_NULL_HANDLE)
    {
        return;
    }

    VkCommandBuffer CmdBufferHandle = VK_NULL_HANDLE;
    if (SubmitInfo.CmdList)
    {
        CmdBufferHandle = static_cast<FVulkanRALCommandList*>(SubmitInfo.CmdList)->Handle;
    }

    LE::Array<VkSemaphore> WaitSemaphores;
    LE::Array<VkPipelineStageFlags> WaitStages;
    for (auto* Semaphore : SubmitInfo.WaitSemaphores)
    {
        if (Semaphore == nullptr)
        {
            LE_LOG(LogRAL, Warn, "Submit ignored a null wait semaphore.");
            continue;
        }
        VkSemaphore Handle = static_cast<FVulkanRALSemaphore*>(Semaphore)->Handle;
        if (Handle == VK_NULL_HANDLE)
        {
            LE_LOG(LogRAL, Warn, "Submit ignored a wait semaphore with null Vulkan handle.");
            continue;
        }
        WaitSemaphores.PushBack(Handle);
        WaitStages.PushBack(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    }
    LE::Array<VkSemaphore> SignalSemaphores;
    for (auto* Semaphore : SubmitInfo.SignalSemaphores)
    {
        if (Semaphore == nullptr)
        {
            LE_LOG(LogRAL, Warn, "Submit ignored a null signal semaphore.");
            continue;
        }
        VkSemaphore Handle = static_cast<FVulkanRALSemaphore*>(Semaphore)->Handle;
        if (Handle == VK_NULL_HANDLE)
        {
            LE_LOG(LogRAL, Warn, "Submit ignored a signal semaphore with null Vulkan handle.");
            continue;
        }
        SignalSemaphores.PushBack(Handle);
    }

    VkFence FenceHandle = VK_NULL_HANDLE;
    if (SubmitInfo.FenceToSignal)
    {
        FenceHandle = static_cast<FVulkanRALFence*>(SubmitInfo.FenceToSignal)->Handle;
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
        LE_LOG(LogRAL, Warn, "Submit skipped: empty submit info.");
        return;
    }

    const VkResult Result = vkQueueSubmit(this->Handle, 1, &Info, FenceHandle);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkQueueSubmit failed. VkResult={}", static_cast<int32>(Result));
    }
}

} // namespace LE
