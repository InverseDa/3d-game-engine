#include "CoreMinimal.h"
#include "Vulkan/VulkanRAL.h"

FVulkanRALQueue::FVulkanRALQueue(FVulkanRALDevice* InDevice, uint32 FamilyIndex, uint32 QueueIndex)
	: Device(InDevice)
{
	vkGetDeviceQueue(Device->VkContext.LogicalDevice, FamilyIndex, QueueIndex, &this->Handle);
}

void FVulkanRALQueue::WaitIdle()
{
	vkQueueWaitIdle(this->Handle);
}

void FVulkanRALQueue::Submit(const FRALSubmitInfo& SubmitInfo)
{
    VkCommandBuffer CmdBufferHandle = VK_NULL_HANDLE;
    if (SubmitInfo.CmdList)
    {
        CmdBufferHandle = static_cast<FVulkanRALCommandList*>(SubmitInfo.CmdList)->Handle;
    }

    std::vector<VkSemaphore> WaitSemaphores;
    std::vector<VkPipelineStageFlags> WaitStages;
    for (auto* Semaphore : SubmitInfo.WaitSemaphores)
    {
        WaitSemaphores.push_back(static_cast<FVulkanRALSemaphore*>(Semaphore)->Handle);
        WaitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    }
    std::vector<VkSemaphore> SignalSemaphores;
    for (auto* Semaphore : SubmitInfo.SignalSemaphores)
    {
        SignalSemaphores.push_back(static_cast<FVulkanRALSemaphore*>(Semaphore)->Handle);
    }

    VkFence FenceHandle = VK_NULL_HANDLE;
    if (SubmitInfo.FenceToSignal)
    {
        FenceHandle = static_cast<FVulkanRALFence*>(SubmitInfo.FenceToSignal)->Handle;
    }

    VkSubmitInfo Info;
    {
        Info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        Info.waitSemaphoreCount = static_cast<uint32>(WaitSemaphores.size());
        Info.pWaitSemaphores = WaitSemaphores.data();
        Info.pWaitDstStageMask = WaitStages.data();

        Info.commandBufferCount = CmdBufferHandle ? 1 : 0;
        Info.pCommandBuffers = &CmdBufferHandle;

        Info.signalSemaphoreCount = static_cast<uint32>(SignalSemaphores.size());
        Info.pSignalSemaphores = SignalSemaphores.data();
    }
    vkQueueSubmit(this->Handle, 1, &Info, FenceHandle);
}
