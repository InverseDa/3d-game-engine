#include "CoreMinimal.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "Vulkan/VulkanRAL.h"

FVulkanRALSwapchain::FVulkanRALSwapchain(FVulkanRALDevice* InDevice, const FRALSwapchainDesc& InDesc)
    : FRALSwapchain()
    , Device(InDevice)
    , Desc(InDesc)
{
    this->InternalCreateSurface();

    VkFenceCreateInfo FenceInfo{};
    {
        FenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        FenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    }
    vkCreateFence(this->Device->VkContext.LogicalDevice, &FenceInfo, nullptr, &this->AcquireFence);

    this->InternalCreateSwapchain();
    this->InternalCreateImageViews();

    // Get the first frame picture
    this->AcquireNextImage();
}

FVulkanRALSwapchain::~FVulkanRALSwapchain()
{
    vkDeviceWaitIdle(this->Device->VkContext.LogicalDevice);
    this->InternalDestroySwapchainResources();

    if (this->AcquireFence != VK_NULL_HANDLE)
    {
        vkDestroyFence(this->Device->VkContext.LogicalDevice, this->AcquireFence, nullptr);
        this->AcquireFence = VK_NULL_HANDLE;
    }
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
        CreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
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
        FRALTextureDesc TextureDesc;
        {
            TextureDesc.Width = Desc.Width;
            TextureDesc.Height = Desc.Height;
            TextureDesc.Depth = 1;
            TextureDesc.MipLevels = 1;
            TextureDesc.ArrayLayers = 1;
            TextureDesc.Format = Desc.BackBufferFormat;
            TextureDesc.bIsRenderTarget = true;
            TextureDesc.bIsShaderResource = true;
        }
        FVulkanRALTexture* Texture = new FVulkanRALTexture(this->Device, TextureDesc, this->Images[i], false);
        this->BackBufferTextures[i] = Texture;

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
        FRALTextureViewDesc ViewDesc;
        {
            ViewDesc.Texture = Texture;
        }
        FVulkanRALTextureView* View = new FVulkanRALTextureView(this->Device, Texture, ViewHandle, ViewDesc);
        this->BackBufferViews[i] = View;
    }
}

void FVulkanRALSwapchain::InternalDestroySwapchainResources()
{
    for (auto* View : BackBufferViews)
    {
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
    vkResetFences(this->Device->VkContext.LogicalDevice, 1, &this->AcquireFence);

    const VkResult Result = vkAcquireNextImageKHR(
        this->Device->VkContext.LogicalDevice,
        SwapchainHandle,
        UINT64_MAX,
        VK_NULL_HANDLE,
        this->AcquireFence,
        &this->CurrentImageIndex
    );
    if (Result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        this->Resize(this->Desc.Width, this->Desc.Height);
        return;
    }
    if (Result != VK_SUCCESS && Result != VK_SUBOPTIMAL_KHR)
    {
        // Handle Error
        return;
    }

    vkWaitForFences(this->Device->VkContext.LogicalDevice, 1, &this->AcquireFence, VK_TRUE, UINT64_MAX);
}

FRALTextureView* FVulkanRALSwapchain::GetCurrentBackBufferView() const
{
    if (this->CurrentImageIndex < BackBufferViews.size())
    {
        return BackBufferViews[CurrentImageIndex];
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
        PresentInfo.pNext = nullptr;

        // Current render path does Queue->WaitIdle() before present, so no semaphore wait is required here.
        PresentInfo.waitSemaphoreCount = 0;
        PresentInfo.pWaitSemaphores = nullptr;

        VkSwapchainKHR Swapchains[] = { this->SwapchainHandle };
        PresentInfo.swapchainCount = 1;
        PresentInfo.pSwapchains = Swapchains;
        PresentInfo.pImageIndices = &this->CurrentImageIndex;

        VkQueue GraphicsQueue = VK_NULL_HANDLE;
        vkGetDeviceQueue(this->Device->VkContext.LogicalDevice, this->Device->VkContext.GraphicsFamilyIndex, 0, &GraphicsQueue);
        auto Result = vkQueuePresentKHR(GraphicsQueue, &PresentInfo);
        if (Result == VK_ERROR_OUT_OF_DATE_KHR || Result == VK_SUBOPTIMAL_KHR)
        {
            VkSurfaceCapabilitiesKHR SurfaceCaps{};
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(this->Device->VkContext.PhysicalDevice, this->SurfaceHandle, &SurfaceCaps);

            uint32 NewWidth  = SurfaceCaps.currentExtent.width;
            uint32 NewHeight = SurfaceCaps.currentExtent.height;

            this->Resize(NewWidth, NewHeight);
            return;
        }
    
        this->AcquireNextImage();
    }
}




