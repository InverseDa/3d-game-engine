#include "CoreMinimal.h"
#include "Vulkan/VulkanRAL.h"

FVulkanRALShader::FVulkanRALShader(FVulkanRALDevice* InDevice, const FRALShaderDesc& InDesc)
    : TVulkanResourceBase<FRALShaderDesc>(InDevice, InDesc)
{
    if (Device == nullptr ||
        Device->VkContext.LogicalDevice == VK_NULL_HANDLE ||
        Desc.ByteCode == nullptr ||
        Desc.ByteCodeSize == 0 ||
        (Desc.ByteCodeSize % 4) != 0)
    {
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
        this->Module = VK_NULL_HANDLE;
    }
}

FVulkanRALShader::~FVulkanRALShader()
{
    if (this->Module != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(this->Device->VkContext.LogicalDevice, this->Module, nullptr);
        this->Module = VK_NULL_HANDLE;
    }
}
