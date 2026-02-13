#include "CoreMinimal.h"
#include "RAL/RALTexture.h"
#include "Vulkan/VulkanRAL.h"

namespace
{
    uint32 FindMemoryTypeIndex(VkPhysicalDevice PhysDev, uint32 TypeBits, VkMemoryPropertyFlags Props)
    {
        VkPhysicalDeviceMemoryProperties MemProps;
        vkGetPhysicalDeviceMemoryProperties(PhysDev, &MemProps);

        for (uint32 i = 0; i < MemProps.memoryTypeCount; ++i)
        {
            if ((TypeBits & (1u << i)) && (MemProps.memoryTypes[i].propertyFlags & Props) == Props)
            {
                return i;
            }
        }
        return 0;
    }

    VkFormat ToVkFormat(EPixelFormat Format)
    {
        switch (Format)
        {
        case EPixelFormat::R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        case EPixelFormat::R8G8B8A8_SRGB:  return VK_FORMAT_R8G8B8A8_SRGB;
        case EPixelFormat::B8G8R8A8_SRGB:  return VK_FORMAT_B8G8R8A8_SRGB;
        case EPixelFormat::D32_FLOAT:      return VK_FORMAT_D32_SFLOAT;
        case EPixelFormat::D24_UNORM_S8_UINT: return VK_FORMAT_D24_UNORM_S8_UINT;
        default: return VK_FORMAT_UNDEFINED;
        }
    }

    VkImageUsageFlags ToVkImageUsage(const FRALTextureDesc& Desc)
    {
        VkImageUsageFlags Flags = 0;
        if (Desc.bIsRenderTarget)   Flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (Desc.bIsDepthStencil)   Flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (Desc.bIsShaderResource) Flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
        if (Desc.bIsUAV)            Flags |= VK_IMAGE_USAGE_STORAGE_BIT;
        Flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT; // 先预留上传
        return Flags;
    }

    VkImageAspectFlags ToVkImageAspect(const FRALTextureDesc& Desc)
    {
        if (Desc.bIsDepthStencil)
        {
            if (Desc.Format == EPixelFormat::D24_UNORM_S8_UINT)
            {
                return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            }
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }

    VkImageViewType ToVkImageViewType(const FRALTextureDesc& Desc, const FRALTextureViewDesc& ViewDesc)
    {
        if (Desc.Depth > 1)
        {
            return VK_IMAGE_VIEW_TYPE_3D;
        }
        if (ViewDesc.ArrayLayers > 1)
        {
            return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        }
        return VK_IMAGE_VIEW_TYPE_2D;
    }
}

FVulkanRALTexture::FVulkanRALTexture(FVulkanRALDevice* InDevice, const FRALTextureDesc& InDesc)
    : FVulkanRALTexture(InDevice, InDesc, VK_NULL_HANDLE, true)
{
}

FVulkanRALTexture::FVulkanRALTexture(FVulkanRALDevice* InDevice, const FRALTextureDesc& InDesc, VkImage InImage, bool bInOwnsImage)
    : TVulkanResourceBase<FRALTextureDesc>(InDevice, InDesc)
    , Image(InImage)
    , bOwnsImage(bInOwnsImage)
{
    if (this->Image != VK_NULL_HANDLE)
    {
        return;
    }

    VkImageCreateInfo Info{};
    {
        Info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        Info.imageType = (Desc.Depth > 1) ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
        Info.format = ToVkFormat(InDesc.Format);
        Info.extent = { Desc.Width, Desc.Height, Desc.Depth};
        Info.mipLevels = Desc.MipLevels;
        Info.arrayLayers = Desc.ArrayLayers;
        Info.samples = VK_SAMPLE_COUNT_1_BIT;
        Info.tiling = VK_IMAGE_TILING_OPTIMAL;
        Info.usage = ToVkImageUsage(Desc);
        Info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        Info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    vkCreateImage(Device->VkContext.LogicalDevice, &Info, nullptr, &this->Image);

    VkMemoryRequirements Req{};
    vkGetImageMemoryRequirements(Device->VkContext.LogicalDevice, this->Image, &Req);

    VkMemoryAllocateInfo AllocInfo{};
    {
        AllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        AllocInfo.allocationSize = Req.size;
        AllocInfo.memoryTypeIndex = FindMemoryTypeIndex(Device->VkContext.PhysicalDevice, Req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }
    vkAllocateMemory(Device->VkContext.LogicalDevice, &AllocInfo, nullptr, &this->Memory);
    vkBindImageMemory(Device->VkContext.LogicalDevice, this->Image, this->Memory, 0);
}

FVulkanRALTexture::~FVulkanRALTexture()
{
    if (this->Image != VK_NULL_HANDLE && this->bOwnsImage)
    {
        vkDestroyImage(Device->VkContext.LogicalDevice, this->Image, nullptr);
        this->Image = VK_NULL_HANDLE;
    }
    if (this->Memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device->VkContext.LogicalDevice, this->Memory, nullptr);
        this->Memory = VK_NULL_HANDLE;
    }
}

FVulkanRALTextureView::FVulkanRALTextureView(FVulkanRALDevice* InDevice, const FRALTextureViewDesc& InDesc)
    : TVulkanResourceBase<FRALTextureViewDesc>(InDevice, InDesc)
{
    this->InitTextureView(static_cast<FVulkanRALTexture*>(Desc.Texture), VK_NULL_HANDLE);
}

FVulkanRALTextureView::FVulkanRALTextureView(FVulkanRALDevice* InDevice, FVulkanRALTexture* InOwner, VkImageView InView, const FRALTextureViewDesc& InDesc)
    : TVulkanResourceBase<FRALTextureViewDesc>(InDevice, InDesc)
{
    this->Desc.Texture = InOwner != nullptr ? InOwner : Desc.Texture;
    this->InitTextureView(static_cast<FVulkanRALTexture*>(Desc.Texture), InView);
}

void FVulkanRALTextureView::InitTextureView(FVulkanRALTexture* InOwner, VkImageView InViewHandle)
{
    this->Owner = InOwner;
    if (this->Owner == nullptr)
    {
        return;
    }

    if (InViewHandle != VK_NULL_HANDLE)
    {
        this->View = InViewHandle;
        return;
    }

    VkImageViewCreateInfo Info{};
    {
        Info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        Info.image = this->Owner->Image;
        Info.viewType = ToVkImageViewType(this->Owner->GetDesc(), Desc);
        Info.format = ToVkFormat(Desc.Format == EPixelFormat::Unknown ? this->Owner->GetDesc().Format : Desc.Format);

        Info.subresourceRange.aspectMask = ToVkImageAspect(this->Owner->GetDesc());
        Info.subresourceRange.baseMipLevel = Desc.MipSlice;
        Info.subresourceRange.levelCount = Desc.MipLevels ? Desc.MipLevels : 1;
        Info.subresourceRange.baseArrayLayer = Desc.ArraySlice;
        Info.subresourceRange.layerCount = Desc.ArrayLayers ? Desc.ArrayLayers : 1;
    }
    vkCreateImageView(Device->VkContext.LogicalDevice, &Info, nullptr, &this->View);
}

FVulkanRALTextureView::~FVulkanRALTextureView()
{
    if (this->View != VK_NULL_HANDLE)
    {
        vkDestroyImageView(Device->VkContext.LogicalDevice, this->View, nullptr);
        this->View = VK_NULL_HANDLE;
    }
}
