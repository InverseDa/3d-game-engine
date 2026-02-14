#include "CoreMinimal.h"
#include "Vulkan/VulkanRAL.h"

FVulkanRALSemaphore::FVulkanRALSemaphore(FVulkanRALDevice* InDevice)
    : Device(InDevice)
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "Semaphore creation failed: invalid device.");
        this->Handle = VK_NULL_HANDLE;
        return;
    }

    VkSemaphoreCreateInfo Info{};
    {
        Info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    }
    const VkResult Result = vkCreateSemaphore(this->Device->VkContext.LogicalDevice, &Info, nullptr, &this->Handle);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkCreateSemaphore failed. VkResult={}", static_cast<int32>(Result));
        this->Handle = VK_NULL_HANDLE;
    }
}

FVulkanRALSemaphore::~FVulkanRALSemaphore()
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        this->Handle = VK_NULL_HANDLE;
        return;
    }

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
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "Fence creation failed: invalid device.");
        this->Handle = VK_NULL_HANDLE;
        return;
    }

    VkFenceCreateInfo Info{};
    {
        Info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (bSignaled)
        {
            Info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        }
    }
    const VkResult Result = vkCreateFence(this->Device->VkContext.LogicalDevice, &Info, nullptr, &this->Handle);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkCreateFence failed. VkResult={}", static_cast<int32>(Result));
        this->Handle = VK_NULL_HANDLE;
    }
}

FVulkanRALFence::~FVulkanRALFence()
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        this->Handle = VK_NULL_HANDLE;
        return;
    }

    if (this->Handle != VK_NULL_HANDLE)
    {
        vkDestroyFence(this->Device->VkContext.LogicalDevice, this->Handle, nullptr);
    }
}

void FVulkanRALFence::Reset()
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE || this->Handle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Warn, "Fence reset skipped: invalid fence/device.");
        return;
    }
    const VkResult Result = vkResetFences(this->Device->VkContext.LogicalDevice, 1, &this->Handle);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkResetFences failed. VkResult={}", static_cast<int32>(Result));
    }
}

void FVulkanRALFence::Wait(uint64 Timeout)
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE || this->Handle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Warn, "Fence wait skipped: invalid fence/device.");
        return;
    }
    const VkResult Result = vkWaitForFences(this->Device->VkContext.LogicalDevice, 1, &this->Handle, VK_TRUE, Timeout);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkWaitForFences failed. VkResult={}", static_cast<int32>(Result));
    }
}

bool FVulkanRALFence::IsSignaled()
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE || this->Handle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Warn, "Fence status query on invalid fence/device.");
        return false;
    }
    return vkGetFenceStatus(this->Device->VkContext.LogicalDevice, this->Handle) == VK_SUCCESS;
}
