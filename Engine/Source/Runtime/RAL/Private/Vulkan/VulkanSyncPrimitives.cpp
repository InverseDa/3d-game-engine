#include "CoreMinimal.h"
#include "Vulkan/VulkanRAL.h"

FVulkanRALSemaphore::FVulkanRALSemaphore(FVulkanRALDevice* InDevice)
    : Device(InDevice)
{
    VkSemaphoreCreateInfo Info{};
    {
        Info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    }
    vkCreateSemaphore(this->Device->VkContext.LogicalDevice, &Info, nullptr, &this->Handle);
}

FVulkanRALSemaphore::~FVulkanRALSemaphore()
{
    if (this->Handle != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(this->Device->VkContext.LogicalDevice, this->Handle, nullptr);
    }
}

// ***********************************************************************************************
// ********************************** Regular Math Calc ******************************************
// ***********************************************************************************************

FVulkanRALFence::FVulkanRALFence(FVulkanRALDevice* InDevice, bool bSignaled)
    : Device(InDevice)
{
    VkFenceCreateInfo Info{};
    {
        Info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (bSignaled)
        {
            Info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        }
    }
    vkCreateFence(this->Device->VkContext.LogicalDevice, &Info, nullptr, &this->Handle);
}

FVulkanRALFence::~FVulkanRALFence()
{
    if (this->Handle != VK_NULL_HANDLE)
    {
        vkDestroyFence(this->Device->VkContext.LogicalDevice, this->Handle, nullptr);
    }
}

void FVulkanRALFence::Reset()
{
    vkResetFences(this->Device->VkContext.LogicalDevice, 1, &this->Handle);
}

void FVulkanRALFence::Wait(uint64 Timeout)
{
    vkWaitForFences(this->Device->VkContext.LogicalDevice, 1, &this->Handle, VK_TRUE, Timeout);
}

bool FVulkanRALFence::IsSignaled()
{
    return vkGetFenceStatus(this->Device->VkContext.LogicalDevice, this->Handle) == VK_SUCCESS;
}
