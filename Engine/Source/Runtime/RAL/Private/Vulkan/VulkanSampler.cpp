#include "CoreMinimal.h"
#include "Vulkan/VulkanRAL.h"

namespace LE
{

// TODO: kodak consider change to Macro or inline function

static VkFilter ToVkFilter(ESamplerFilter Filter)
{
    switch (Filter)
    {
        case ESamplerFilter::Nearest: return VK_FILTER_NEAREST;
        case ESamplerFilter::Linear: return VK_FILTER_LINEAR;
    }
    return VK_FILTER_LINEAR;
}

static VkSamplerMipmapMode ToVkMipmapMode(ESamplerMipmapMode MipmapMode)
{
    switch (MipmapMode)
    {
        case ESamplerMipmapMode::Nearest: return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        case ESamplerMipmapMode::Linear: return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
    return VK_SAMPLER_MIPMAP_MODE_NEAREST;
}

static VkSamplerAddressMode ToVkAddressMode(ESamplerAddressMode AddressMode)
{
    switch (AddressMode)
    {
        case ESamplerAddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case ESamplerAddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case ESamplerAddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case ESamplerAddressMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

static VkCompareOp ToVkCompareOp(ECompareFunction CompareFunc)
{
    switch (CompareFunc)
    {
        case ECompareFunction::Never: return VK_COMPARE_OP_NEVER;
        case ECompareFunction::Less: return VK_COMPARE_OP_LESS;
        case ECompareFunction::Equal: return VK_COMPARE_OP_EQUAL;
        case ECompareFunction::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case ECompareFunction::Greater: return VK_COMPARE_OP_GREATER;
        case ECompareFunction::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
        case ECompareFunction::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case ECompareFunction::Always: return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_NEVER;
}

static VkBorderColor ToVkBorderColor(const float* BorderColor)
{
    if (BorderColor == nullptr)
    {
        LE_LOG(LogRAL, Warn, "Sampler border color is null. Falling back to transparent black.");
        return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    }

    float R = BorderColor[0];
    float G = BorderColor[1];
    float B = BorderColor[2];
    float A = BorderColor[3];

    if (R == 0.0f && G == 0.0f && B == 0.0f && A == 0.0f)
    {
        return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    }
    if (R == 0.0f && G == 0.0f && B == 0.0f && A == 1.0f)
    {
        return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    }
    if (R == 1.0f && G == 1.0f && B == 1.0f && A == 1.0f)
    {
        return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    }

    return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
}

FVulkanRALSampler::FVulkanRALSampler(FVulkanRALDevice* InDevice, const FRALSamplerDesc& InDesc)
    : Device(InDevice)
    , Desc(InDesc)
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "Sampler creation failed: invalid device.");
        this->Handle = VK_NULL_HANDLE;
        return;
    }

    VkSamplerCreateInfo Info{};
    {
        Info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

        Info.minFilter = ToVkFilter(Desc.MinFilter);
        Info.magFilter = ToVkFilter(Desc.MagFilter);
        Info.mipmapMode = ToVkMipmapMode(Desc.MipmapMode);

        Info.addressModeU = ToVkAddressMode(Desc.AddressU);
        Info.addressModeV = ToVkAddressMode(Desc.AddressV);
        Info.addressModeW = ToVkAddressMode(Desc.AddressW);

        Info.minLod = Desc.MinLOD;
        Info.maxLod = Desc.MaxLOD;
        Info.mipLodBias = Desc.MipLODBias;

        Info.anisotropyEnable = Desc.bEnableAnisotropic ? VK_TRUE : VK_FALSE;
        Info.maxAnisotropy = Desc.MaxAnisotropy;

        Info.compareEnable = Desc.bEnableCompare ? VK_TRUE : VK_FALSE;
        Info.compareOp = ToVkCompareOp(Desc.CompareFunc);

        Info.borderColor = ToVkBorderColor(Desc.BorderColor);
        Info.unnormalizedCoordinates = VK_FALSE;
    }
    const VkResult Result = vkCreateSampler(this->Device->VkContext.LogicalDevice, &Info, nullptr, &this->Handle);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkCreateSampler failed. VkResult={}", static_cast<int32>(Result));
        this->Handle = VK_NULL_HANDLE;
    }
}

FVulkanRALSampler::~FVulkanRALSampler()
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        this->Handle = VK_NULL_HANDLE;
        return;
    }

    if (Handle != VK_NULL_HANDLE)
    {
        vkDestroySampler(this->Device->VkContext.LogicalDevice, Handle, nullptr);
        Handle = VK_NULL_HANDLE;
    }
}

} // namespace LE
