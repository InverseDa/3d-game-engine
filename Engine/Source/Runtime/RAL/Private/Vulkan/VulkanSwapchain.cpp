#include "CoreMinimal.h"

#include <algorithm>
#include <limits>

#if PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#endif

#include "Vulkan/VulkanRAL.h"

namespace LE
{

namespace
{
VkFormat ToVkFormat(EPixelFormat Format)
{
    switch (Format)
    {
    case EPixelFormat::R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
    case EPixelFormat::R8G8B8A8_SRGB:  return VK_FORMAT_R8G8B8A8_SRGB;
    case EPixelFormat::B8G8R8A8_SRGB:  return VK_FORMAT_B8G8R8A8_SRGB;
    default:                           return VK_FORMAT_B8G8R8A8_SRGB;
    }
}

VkSurfaceFormatKHR ChooseSurfaceFormat(const LE::Array<VkSurfaceFormatKHR>& AvailableFormats, EPixelFormat PreferredFormat)
{
    const VkFormat DesiredFormat = ToVkFormat(PreferredFormat);
    for (const VkSurfaceFormatKHR& SurfaceFormat : AvailableFormats)
    {
        if (SurfaceFormat.format == DesiredFormat)
        {
            return SurfaceFormat;
        }
    }

    for (const VkSurfaceFormatKHR& SurfaceFormat : AvailableFormats)
    {
        if (SurfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB)
        {
            return SurfaceFormat;
        }
    }

    return AvailableFormats.IsEmpty()
        ? VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }
        : AvailableFormats[0];
}

VkPresentModeKHR ChoosePresentMode(const LE::Array<VkPresentModeKHR>& AvailableModes, bool bEnableVsync)
{
    if (bEnableVsync)
    {
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    for (const VkPresentModeKHR PresentMode : AvailableModes)
    {
        if (PresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return PresentMode;
        }
    }

    for (const VkPresentModeKHR PresentMode : AvailableModes)
    {
        if (PresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR)
        {
            return PresentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}
}

FVulkanRALSwapchain::FVulkanRALSwapchain(FVulkanRALDevice* InDevice, const FRALSwapchainDesc& InDesc)
    : FRALSwapchain()
    , Device(InDevice)
    , Desc(InDesc)
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "Swapchain creation failed: invalid device.");
        return;
    }

    LE_LOG(LogRAL, Info, "Creating Vulkan swapchain. Size={}x{}, VSync={}", this->Desc.Width, this->Desc.Height, this->Desc.bEnableVsync);

    this->InternalCreateSurface();
    if (this->SurfaceHandle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "Swapchain creation failed: surface creation failed.");
        return;
    }

    VkSemaphoreCreateInfo SemaphoreInfo{};
    {
        SemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    }
    VkResult Result = vkCreateSemaphore(this->Device->VkContext.LogicalDevice, &SemaphoreInfo, nullptr, &this->ImageAvailableSemaphore);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkCreateSemaphore(ImageAvailable) failed. VkResult={}", static_cast<int32>(Result));
    }
    Result = vkCreateSemaphore(this->Device->VkContext.LogicalDevice, &SemaphoreInfo, nullptr, &this->RenderFinishedSemaphore);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkCreateSemaphore(RenderFinished) failed. VkResult={}", static_cast<int32>(Result));
    }

    this->InternalCreateSwapchain();
    if (this->SwapchainHandle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "Swapchain creation failed: vkCreateSwapchainKHR failed.");
        return;
    }
    this->InternalCreateImageViews();

    // Get the first frame picture
    this->AcquireNextImage();
    LE_LOG(LogRAL, Info, "Vulkan swapchain created with {} images.", static_cast<uint32>(this->Images.Size()));
}

FVulkanRALSwapchain::~FVulkanRALSwapchain()
{
    if (this->Device == nullptr)
    {
        LE_LOG(LogRAL, Warn, "Destroying swapchain with null device.");
        return;
    }

    if (this->Device->VkContext.LogicalDevice != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(this->Device->VkContext.LogicalDevice);
    }
    this->InternalDestroySwapchainResources();

    if (this->ImageAvailableSemaphore != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(this->Device->VkContext.LogicalDevice, this->ImageAvailableSemaphore, nullptr);
        this->ImageAvailableSemaphore = VK_NULL_HANDLE;
    }
    if (this->RenderFinishedSemaphore != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(this->Device->VkContext.LogicalDevice, this->RenderFinishedSemaphore, nullptr);
        this->RenderFinishedSemaphore = VK_NULL_HANDLE;
    }
    if (this->SurfaceHandle != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(this->Device->VkContext.Instance, this->SurfaceHandle, nullptr);
        this->SurfaceHandle = VK_NULL_HANDLE;
    }
}

void FVulkanRALSwapchain::InternalCreateSurface()
{
    if (this->Desc.Surface.Type == ERALSurfaceType::Unknown)
    {
        LE_LOG(LogRAL, Error, "InternalCreateSurface failed: surface type is unknown.");
        this->SurfaceHandle = VK_NULL_HANDLE;
        return;
    }

#if PLATFORM_WINDOWS
    if (this->Desc.Surface.Type == ERALSurfaceType::Win32)
    {
        if (this->Desc.Surface.WindowHandle == nullptr)
        {
            LE_LOG(LogRAL, Error, "InternalCreateSurface failed: window handle is null.");
            this->SurfaceHandle = VK_NULL_HANDLE;
            return;
        }

        VkWin32SurfaceCreateInfoKHR Info{};
        {
            Info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
            Info.hinstance = GetModuleHandle(nullptr);
            Info.hwnd = static_cast<HWND>(this->Desc.Surface.WindowHandle);
        }
        const VkResult Result = vkCreateWin32SurfaceKHR(this->Device->VkContext.Instance, &Info, nullptr, &this->SurfaceHandle);
        if (Result != VK_SUCCESS)
        {
            LE_LOG(LogRAL, Error, "vkCreateWin32SurfaceKHR failed. VkResult={}", static_cast<int32>(Result));
            this->SurfaceHandle = VK_NULL_HANDLE;
            return;
        }
    }
#endif

#if PLATFORM_MAC
    if (this->Desc.Surface.Type == ERALSurfaceType::MetalLayer)
    {
        if (this->Desc.Surface.LayerHandle == nullptr)
        {
            LE_LOG(LogRAL, Error, "InternalCreateSurface failed: metal layer handle is null.");
            this->SurfaceHandle = VK_NULL_HANDLE;
            return;
        }

        VkMetalSurfaceCreateInfoEXT Info{};
        {
            Info.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
            Info.pLayer = static_cast<CAMetalLayer*>(this->Desc.Surface.LayerHandle);
        }
        const VkResult Result = vkCreateMetalSurfaceEXT(this->Device->VkContext.Instance, &Info, nullptr, &this->SurfaceHandle);
        if (Result != VK_SUCCESS)
        {
            LE_LOG(LogRAL, Error, "vkCreateMetalSurfaceEXT failed. VkResult={}", static_cast<int32>(Result));
            this->SurfaceHandle = VK_NULL_HANDLE;
            return;
        }
    }
#endif

    if (this->SurfaceHandle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "InternalCreateSurface failed: unsupported surface type {}.", static_cast<uint32>(this->Desc.Surface.Type));
        return;
    }

    VkBool32 bPresentSupported = VK_FALSE;
    const VkResult PresentSupportResult = vkGetPhysicalDeviceSurfaceSupportKHR(
        this->Device->VkContext.PhysicalDevice,
        this->Device->VkContext.GraphicsFamilyIndex,
        this->SurfaceHandle,
        &bPresentSupported);
    if (PresentSupportResult != VK_SUCCESS || bPresentSupported != VK_TRUE)
    {
        LE_LOG(LogRAL, Error, "Selected graphics queue does not support present. VkResult={}, Supported={}",
            static_cast<int32>(PresentSupportResult), bPresentSupported == VK_TRUE);
        vkDestroySurfaceKHR(this->Device->VkContext.Instance, this->SurfaceHandle, nullptr);
        this->SurfaceHandle = VK_NULL_HANDLE;
        return;
    }

    LE_LOG(LogRAL, Info, "Vulkan surface created.");
}

void FVulkanRALSwapchain::InternalCreateSwapchain()
{
    if (this->SurfaceHandle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "InternalCreateSwapchain failed: surface handle is null.");
        this->SwapchainHandle = VK_NULL_HANDLE;
        return;
    }

    VkSurfaceCapabilitiesKHR Capabilities{};
    const VkResult CapResult = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(this->Device->VkContext.PhysicalDevice, this->SurfaceHandle, &Capabilities);
    if (CapResult != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed. VkResult={}", static_cast<int32>(CapResult));
        this->SwapchainHandle = VK_NULL_HANDLE;
        return;
    }

    uint32 SurfaceFormatCount = 0;
    VkResult Result = vkGetPhysicalDeviceSurfaceFormatsKHR(this->Device->VkContext.PhysicalDevice, this->SurfaceHandle, &SurfaceFormatCount, nullptr);
    if (Result != VK_SUCCESS || SurfaceFormatCount == 0)
    {
        LE_LOG(LogRAL, Error, "vkGetPhysicalDeviceSurfaceFormatsKHR(count) failed. VkResult={}, Count={}",
            static_cast<int32>(Result), SurfaceFormatCount);
        this->SwapchainHandle = VK_NULL_HANDLE;
        return;
    }
    LE::Array<VkSurfaceFormatKHR> SurfaceFormats;
    SurfaceFormats.Resize(SurfaceFormatCount);
    Result = vkGetPhysicalDeviceSurfaceFormatsKHR(this->Device->VkContext.PhysicalDevice, this->SurfaceHandle, &SurfaceFormatCount, SurfaceFormats.Data());
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkGetPhysicalDeviceSurfaceFormatsKHR(list) failed. VkResult={}", static_cast<int32>(Result));
        this->SwapchainHandle = VK_NULL_HANDLE;
        return;
    }

    uint32 PresentModeCount = 0;
    Result = vkGetPhysicalDeviceSurfacePresentModesKHR(this->Device->VkContext.PhysicalDevice, this->SurfaceHandle, &PresentModeCount, nullptr);
    if (Result != VK_SUCCESS || PresentModeCount == 0)
    {
        LE_LOG(LogRAL, Error, "vkGetPhysicalDeviceSurfacePresentModesKHR(count) failed. VkResult={}, Count={}",
            static_cast<int32>(Result), PresentModeCount);
        this->SwapchainHandle = VK_NULL_HANDLE;
        return;
    }
    LE::Array<VkPresentModeKHR> PresentModes;
    PresentModes.Resize(PresentModeCount);
    Result = vkGetPhysicalDeviceSurfacePresentModesKHR(this->Device->VkContext.PhysicalDevice, this->SurfaceHandle, &PresentModeCount, PresentModes.Data());
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkGetPhysicalDeviceSurfacePresentModesKHR(list) failed. VkResult={}", static_cast<int32>(Result));
        this->SwapchainHandle = VK_NULL_HANDLE;
        return;
    }

    const VkSurfaceFormatKHR SelectedFormat = ChooseSurfaceFormat(SurfaceFormats, this->Desc.BackBufferFormat);
    const VkPresentModeKHR SelectedPresentMode = ChoosePresentMode(PresentModes, this->Desc.bEnableVsync);

    uint32 MinImageCount = Capabilities.minImageCount + 1;
    if (0 < Capabilities.maxImageCount && MinImageCount > Capabilities.maxImageCount)
    {
        MinImageCount = Capabilities.maxImageCount;
    }

    VkExtent2D SwapchainExtent{};
    if (Capabilities.currentExtent.width != std::numeric_limits<uint32>::max())
    {
        SwapchainExtent = Capabilities.currentExtent;
        this->Desc.Width = SwapchainExtent.width;
        this->Desc.Height = SwapchainExtent.height;
    }
    else
    {
        SwapchainExtent.width = std::clamp(this->Desc.Width, Capabilities.minImageExtent.width, Capabilities.maxImageExtent.width);
        SwapchainExtent.height = std::clamp(this->Desc.Height, Capabilities.minImageExtent.height, Capabilities.maxImageExtent.height);
    }

    VkSwapchainCreateInfoKHR CreateInfo{};
    {
        CreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        CreateInfo.surface = this->SurfaceHandle;
        CreateInfo.minImageCount = MinImageCount;
        CreateInfo.imageFormat = SelectedFormat.format;
        CreateInfo.imageColorSpace = SelectedFormat.colorSpace;
        CreateInfo.imageExtent = SwapchainExtent;
        CreateInfo.imageArrayLayers = 1;
        CreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        CreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

        CreateInfo.preTransform = Capabilities.currentTransform;
        CreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

        CreateInfo.presentMode = SelectedPresentMode;
        CreateInfo.clipped = true;
        CreateInfo.oldSwapchain = VK_NULL_HANDLE;
    }
    Result = vkCreateSwapchainKHR(this->Device->VkContext.LogicalDevice, &CreateInfo, nullptr, &this->SwapchainHandle);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkCreateSwapchainKHR failed. VkResult={}", static_cast<int32>(Result));
        this->SwapchainHandle = VK_NULL_HANDLE;
        return;
    }
}

void FVulkanRALSwapchain::InternalCreateImageViews()
{
    if (this->SwapchainHandle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "InternalCreateImageViews failed: swapchain handle is null.");
        return;
    }

    uint32 ImageCount = 0;
    {
        const VkResult Result = vkGetSwapchainImagesKHR(this->Device->VkContext.LogicalDevice, this->SwapchainHandle, &ImageCount, nullptr);
        if (Result != VK_SUCCESS)
        {
            LE_LOG(LogRAL, Error, "vkGetSwapchainImagesKHR(count) failed. VkResult={}", static_cast<int32>(Result));
            return;
        }
    }
    if (ImageCount == 0)
    {
        LE_LOG(LogRAL, Error, "Swapchain returned zero images.");
        return;
    }
    this->Images.Resize(ImageCount);
    {
        const VkResult Result = vkGetSwapchainImagesKHR(this->Device->VkContext.LogicalDevice, this->SwapchainHandle, &ImageCount, this->Images.Data());
        if (Result != VK_SUCCESS)
        {
            LE_LOG(LogRAL, Error, "vkGetSwapchainImagesKHR(list) failed. VkResult={}", static_cast<int32>(Result));
            this->Images.Clear();
            return;
        }
    }

    this->BackBufferViews.Resize(ImageCount);
    this->BackBufferTextures.Resize(ImageCount);

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
            ViewInfo.format = ToVkFormat(Desc.BackBufferFormat);
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
            const VkResult Result = vkCreateImageView(this->Device->VkContext.LogicalDevice, &ViewInfo, nullptr, &ViewHandle);
            if (Result != VK_SUCCESS)
            {
                LE_LOG(LogRAL, Error, "vkCreateImageView for swapchain image[{}] failed. VkResult={}", i, static_cast<int32>(Result));
                ViewHandle = VK_NULL_HANDLE;
            }
        }
        FRALTextureViewDesc ViewDesc;
        {
            ViewDesc.Texture = Texture;
            ViewDesc.Format = TextureDesc.Format;
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
    BackBufferViews.Clear();

    for (auto* Texture : BackBufferTextures)
    {
        delete Texture;
    }
    BackBufferTextures.Clear();

    if (SwapchainHandle != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(Device->VkContext.LogicalDevice, SwapchainHandle, nullptr);
        SwapchainHandle = VK_NULL_HANDLE;
    }
}

void FVulkanRALSwapchain::AcquireNextImage()
{
    if (this->SwapchainHandle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "AcquireNextImage failed: swapchain handle is null.");
        return;
    }
    if (this->ImageAvailableSemaphore == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "AcquireNextImage failed: image-available semaphore is null.");
        return;
    }

    const VkResult Result = vkAcquireNextImageKHR(
        this->Device->VkContext.LogicalDevice,
        SwapchainHandle,
        UINT64_MAX,
        this->ImageAvailableSemaphore,
        VK_NULL_HANDLE,
        &this->CurrentImageIndex
    );
    if (Result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        LE_LOG(LogRAL, Warn, "AcquireNextImage returned OUT_OF_DATE. Triggering resize.");
        this->Resize(this->Desc.Width, this->Desc.Height);
        return;
    }
    if (Result != VK_SUCCESS && Result != VK_SUBOPTIMAL_KHR)
    {
        LE_LOG(LogRAL, Error, "vkAcquireNextImageKHR failed. VkResult={}", static_cast<int32>(Result));
        return;
    }
    
    VkQueue GraphicsQueue = VK_NULL_HANDLE;
    vkGetDeviceQueue(this->Device->VkContext.LogicalDevice, this->Device->VkContext.GraphicsFamilyIndex, 0, &GraphicsQueue);
    if (GraphicsQueue == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "AcquireNextImage failed: graphics queue is null.");
        return;
    }

    // Consume acquire semaphore on the graphics queue so the next acquire can legally reuse it.
    VkPipelineStageFlags WaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo SubmitInfo{};
    {
        SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        SubmitInfo.waitSemaphoreCount = 1;
        SubmitInfo.pWaitSemaphores = &this->ImageAvailableSemaphore;
        SubmitInfo.pWaitDstStageMask = &WaitStage;
        SubmitInfo.commandBufferCount = 0;
        SubmitInfo.pCommandBuffers = nullptr;
        SubmitInfo.signalSemaphoreCount = 0;
        SubmitInfo.pSignalSemaphores = nullptr;
    }
    const VkResult SubmitResult = vkQueueSubmit(GraphicsQueue, 1, &SubmitInfo, VK_NULL_HANDLE);
    if (SubmitResult != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkQueueSubmit(acquire semaphore consume) failed. VkResult={}", static_cast<int32>(SubmitResult));
    }
}

FRALTextureView* FVulkanRALSwapchain::GetCurrentBackBufferView() const
{
    if (this->CurrentImageIndex < BackBufferViews.Size())
    {
        return BackBufferViews[CurrentImageIndex];
    }
    return nullptr;
}

void FVulkanRALSwapchain::Resize(uint32 Width, uint32 Height)
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "Resize failed: invalid device.");
        return;
    }
    if (Width == 0 || Height == 0)
    {
        LE_LOG(LogRAL, Warn, "Skipping swapchain resize for zero-sized surface ({}x{}).", Width, Height);
        return;
    }

    this->Desc.Width = Width;
    this->Desc.Height = Height;
    LE_LOG(LogRAL, Info, "Resizing swapchain to {}x{}.", Width, Height);

    vkDeviceWaitIdle(this->Device->VkContext.LogicalDevice);
    this->InternalDestroySwapchainResources();
    this->InternalCreateSwapchain();
    if (this->SwapchainHandle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "Resize failed: swapchain recreation failed.");
        return;
    }
    this->InternalCreateImageViews();

    this->AcquireNextImage();
}

void FVulkanRALSwapchain::Present()
{
    if (this->SwapchainHandle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "Present failed: swapchain handle is null.");
        return;
    }

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
        if (GraphicsQueue == VK_NULL_HANDLE)
        {
            LE_LOG(LogRAL, Error, "Present failed: graphics queue is null.");
            return;
        }
        auto Result = vkQueuePresentKHR(GraphicsQueue, &PresentInfo);
        if (Result == VK_ERROR_OUT_OF_DATE_KHR || Result == VK_SUBOPTIMAL_KHR)
        {
            LE_LOG(LogRAL, Info, "vkQueuePresentKHR returned {}. Recreating swapchain.", static_cast<int32>(Result));
            VkSurfaceCapabilitiesKHR SurfaceCaps{};
            const VkResult SurfaceResult = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(this->Device->VkContext.PhysicalDevice, this->SurfaceHandle, &SurfaceCaps);
            if (SurfaceResult != VK_SUCCESS)
            {
                LE_LOG(LogRAL, Error, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR during present failed. VkResult={}", static_cast<int32>(SurfaceResult));
                return;
            }

            uint32 NewWidth  = SurfaceCaps.currentExtent.width;
            uint32 NewHeight = SurfaceCaps.currentExtent.height;
            if (NewWidth == 0 || NewHeight == 0)
            {
                LE_LOG(LogRAL, Warn, "Swapchain present reported a zero-sized extent. Waiting for a valid window size before recreating.");
                return;
            }

            this->Resize(NewWidth, NewHeight);
            return;
        }
        if (Result != VK_SUCCESS)
        {
            LE_LOG(LogRAL, Error, "vkQueuePresentKHR failed. VkResult={}", static_cast<int32>(Result));
            return;
        }
    
        this->AcquireNextImage();
    }
}

} // namespace LE
