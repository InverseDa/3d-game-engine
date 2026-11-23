#include "CoreMinimal.h"
#include "Vulkan/VulkanRHI.h"

namespace LE
{

FVulkanRHIShader::FVulkanRHIShader(FVulkanRHIDevice* InDevice, const FRHIShaderDesc& InDesc) : TVulkanResourceBase<FRHIShaderDesc>(InDevice, InDesc)
{
    if (Device == nullptr || Device->VkContext.LogicalDevice == VK_NULL_HANDLE || Desc.ByteCode == nullptr || Desc.ByteCodeSize == 0 || (Desc.ByteCodeSize % 4) != 0)
    {
        LE_LOG(LogRHI, Error, "Shader creation failed: invalid input. LogicalDeviceValid={}, ByteCodeValid={}, ByteCodeSize={}", Device != nullptr && Device->VkContext.LogicalDevice != VK_NULL_HANDLE, Desc.ByteCode != nullptr, Desc.ByteCodeSize);
        this->Module = VK_NULL_HANDLE;
        return;
    }

    VkShaderModuleCreateInfo Info{};
    {
        Info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        Info.codeSize = Desc.ByteCodeSize;
        Info.pCode = static_cast<const uint32*>(Desc.ByteCode);
    }
    const VkResult Result = vkCreateShaderModule(Device->VkContext.LogicalDevice, &Info, nullptr, &this->Module);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRHI, Error, "vkCreateShaderModule failed. VkResult={}", static_cast<int32>(Result));
        this->Module = VK_NULL_HANDLE;
        return;
    }

    LE_LOG(LogRHI, Info, "Shader module created. Stage={}, EntryPoint={}", static_cast<uint32>(Desc.Stage), Desc.EntryPoint.GetData());
}

FVulkanRHIShader::~FVulkanRHIShader()
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        this->Module = VK_NULL_HANDLE;
        return;
    }

    if (this->Module != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(this->Device->VkContext.LogicalDevice, this->Module, nullptr);
        this->Module = VK_NULL_HANDLE;
    }
}

} // namespace LE
