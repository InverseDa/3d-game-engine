#include "CoreMinimal.h"
#include "RAL/RALTexture.h"
#include "Vulkan/VulkanRAL.h"

static VkFormat ToVkFormat(EPixelFormat Format)
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

static VkImageUsageFlags ToVkImageUsage(const FRALTextureDesc& Desc)
{
    VkImageUsageFlags Flags = 0;
    if (Desc.bIsRenderTarget)   Flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (Desc.bIsDepthStencil)   Flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (Desc.bIsShaderResource) Flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (Desc.bIsUAV)            Flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    Flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT; // 先预留上传
    return Flags;
}
