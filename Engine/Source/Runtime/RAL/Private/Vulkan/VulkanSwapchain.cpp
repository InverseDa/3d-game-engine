#include "CoreMinimal.h"
#include "Vulkan/VulkanRAL.h"

#include <windef.h>
#include <libloaderapi.h>

FVulkanRALSwapchain::FVulkanRALSwapchain(FVulkanRALDevice* InDevice, const FRALSwapchainDesc& InDesc)
    : FRALSwapchain()
    , Device(InDevice)
    , Desc(InDesc)
{
    this->InternalCreateSurface();

    VkSemaphoreCreateInfo SemaphoreInfo{};
    {
        SemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    }
    vkCreateSemaphore(this->Device->VkContext.LogicalDevice, &SemaphoreInfo, nullptr, &this->ImageAvailableSemaphore);
    vkCreateSemaphore(this->Device->VkContext.LogicalDevice, &SemaphoreInfo, nullptr, &this->RenderFinishedSemaphore);

    this->InternalCreateSwapchain();
    this->InternalCreateImageViews();

    // Get the first frame picture
    this->AcquireNextImage();
}

FVulkanRALSwapchain::~FVulkanRALSwapchain()
{
    vkDeviceWaitIdle(this->Device->VkContext.LogicalDevice);
    this->InternalDestroySwapchainResources();

    vkDestroySemaphore(this->Device->VkContext.LogicalDevice, this->ImageAvailableSemaphore, nullptr);
    vkDestroySemaphore(this->Device->VkContext.LogicalDevice, this->RenderFinishedSemaphore, nullptr);
    vkDestroySurfaceKHR(this->Device->VkContext.Instance, this->SurfaceHandle, nullptr);
}

void FVulkanRALSwapchain::InternalCreateSurface()
{
#if PLATFORM_WINDOWS
    VkWin32SurfaceCreateInfoKHR Info{};
    {
        Info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        Info.hinstance = GetModuleHandle(nullptr); // Get the current process handle
        Info.hwnd = static_cast<HWND>(this->Desc.WindowHandle);
    }
    vkCreateWin32SurfaceKHR(this->Device->VkContext.Instance, &Info, nullptr, &this->SurfaceHandle);
#endif
}

void FVulkanRALSwapchain::InternalCreateSwapchain()
{
    VkSurfaceCapabilitiesKHR Capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(this->Device->VkContext.PhysicalDevice, this->SurfaceHandle, &Capabilities);

    // Check the max buffer count
    uint32 MinImageCount = Capabilities.minImageCount + 1;
    if (0 < Capabilities.maxImageCount && MinImageCount > Capabilities.maxImageCount)
    {
        MinImageCount = Capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR CreateInfo{};
    {
        CreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_SWAPCHAIN_CREATE_INFO_KHR;
        CreateInfo.surface = this->SurfaceHandle;
        CreateInfo.minImageCount = MinImageCount;
        CreateInfo.imageFormat = VK_FORMAT_B8G8R8A8_SRGB; // 简化处理，实际应通过 vkGetPhysicalDeviceSurfaceFormatsKHR 查询
        CreateInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        CreateInfo.imageExtent = { this->Desc.Width, this->Desc.Height };
        CreateInfo.imageArrayLayers = 1;
        CreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // Used for render target

        CreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // TODO: kodak

        CreateInfo.preTransform = Capabilities.currentTransform;
        CreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // Non alpha window

        CreateInfo.presentMode = Desc.bEnableVsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;
        CreateInfo.clipped = true;
        CreateInfo.oldSwapchain = VK_NULL_HANDLE; // Resize needed
    }
    vkCreateSwapchainKHR(this->Device->VkContext.LogicalDevice, &CreateInfo, nullptr, &this->SwapchainHandle);
}

void FVulkanRALSwapchain::InternalCreateImageViews()
{
    uint32 ImageCount;
    {
        vkGetSwapchainImagesKHR(this->Device->VkContext.LogicalDevice, this->SwapchainHandle, &ImageCount, nullptr);
    }
    this->Images.resize(ImageCount);
    {
        vkGetSwapchainImagesKHR(this->Device->VkContext.LogicalDevice, this->SwapchainHandle, &ImageCount, this->Images.data());
    }

    this->BackBufferViews.resize(ImageCount);
    this->BackBufferTextures.resize(ImageCount);

    for (uint32 i = 0; i < ImageCount; ++i)
    {
        FVulkanRALTexture* Texture = new FVulkanRALTexture();
        {
            Texture->Image = this->Images[i];
            Texture->TextureDesc.Width = Desc.Width;
            Texture->TextureDesc.Height = Desc.Height;
            Texture->TextureDesc.Format = EPixelFormat::B8G8R8A8_SRGB;
            this->BackBufferTextures[i] = Texture;
        }
        VkImageViewCreateInfo ViewInfo{};
        {
            ViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ViewInfo.image = this->Images[i];
            ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ViewInfo.format = VK_FORMAT_B8G8R8A8_SRGB;
            ViewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            ViewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            ViewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            ViewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            ViewInfo.subresourceRange.baseMipLevel = 0;
            ViewInfo.subresourceRange.levelCount = 1;
            ViewInfo.subresourceRange.baseArrayLayer = 0;
            ViewInfo.subresourceRange.layerCount = 1;
        }
        VkImageView ViewHandle;
        {
            vkCreateImageView(this->Device->VkContext.LogicalDevice, &ViewInfo, nullptr, &ViewHandle);
        }
        FVulkanRALTextureView* View = new FVulkanRALTextureView();
        {
            View->View = ViewHandle;
            View->Owner = Texture;
            this->BackBufferViews[i] = View;
        }
    }
}

void FVulkanRALSwapchain::InternalDestroySwapchainResources()
{
    for (auto* View : BackBufferViews)
    {
        vkDestroyImageView(Device->VkContext.LogicalDevice, View->View, nullptr);
        delete View;
    }
    BackBufferViews.clear();

    for (auto* Texture : BackBufferTextures)
    {
        delete Texture;
    }
    BackBufferTextures.clear();

    if (SwapchainHandle != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(Device->VkContext.LogicalDevice, SwapchainHandle, nullptr);
        SwapchainHandle = VK_NULL_HANDLE;
    }
}

void FVulkanRALSwapchain::AcquireNextImage()
{
    auto Result = vkAcquireNextImageKHR(
        this->Device->VkContext.LogicalDevice,
        SwapchainHandle,
        UINT64_MAX,
        this->ImageAvailableSemaphore,
        VK_NULL_HANDLE,
        &this->CurrentBackBufferIndex
    );
    if (Result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        this->Resize(this->Desc.Width, this->Desc.Height);
    }
    else if (Result != VK_SUCCESS && Result != VK_SUBOPTIMAL_KHR)
    {
        // Handle Error
    }
}

FRALTextureView* FVulkanRALSwapchain::GetCurrentBackBufferView() const
{
    if (this->CurrentBackBufferIndex < BackBufferViews.size())
    {
        return BackBufferViews[CurrentBackBufferIndex];
    }
    return nullptr;
}

void FVulkanRALSwapchain::Resize(uint32 Width, uint32 Height)
{
    this->Desc.Width = Width;
    this->Desc.Height = Height;

    vkDeviceWaitIdle(this->Device->VkContext.LogicalDevice);
    this->InternalDestroySwapchainResources();
    this->InternalCreateSwapchain();
    this->InternalCreateImageViews();

    this->AcquireNextImage();
}

void FVulkanRALSwapchain::Present()
{
    VkPresentInfoKHR PresentInfo{};
    {
        PresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        PresentInfo.waitSemaphoreCount = 1;
        PresentInfo.pWaitSemaphores = &RenderFinishedSemaphore;

        VkSwapchainKHR Swapchains[] = { this->SwapchainHandle };
        PresentInfo.swapchainCount = 1;
        PresentInfo.pSwapchains = Swapchains;
        PresentInfo.pImageIndices = &this->CurrentBackBufferIndex;

        auto Result = vkQueuePresentKHR(this->Device->GetGraphicsQueueHandle(), &PresentInfo);
        if (Result == VK_ERROR_OUT_OF_DATE_KHR || Result == VK_SUBOPTIMAL_KHR)
        {
            this->Resize(Desc.Width, Desc.Height);
        }
    
        this->AcquireNextImage();
    }
}




