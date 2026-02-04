#include "CoreMinimal.h"
#include "Vulkan/VulkanRAL.h"

FVulkanRALShader::FVulkanRALShader(FVulkanRALDevice* InDevice, const FRALShaderDesc& InDesc)
    : TVulkanResourceBase<FRALShaderDesc>(InDevice, InDesc)
{
    assert(Desc.ByteCode && Desc.ByteCodeSize % 4 == 0);

    VkShaderModuleCreateInfo Info{};
    {
        Info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        Info.codeSize = Desc.ByteCodeSize;
        Info.pCode = static_cast<const uint32*>(Desc.ByteCode);
    }
    vkCreateShaderModule(Device->VkContext.LogicalDevice, &Info, nullptr, &this->Module);
}

FVulkanRALShader::~FVulkanRALShader()
{
    if (this->Module != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(this->Device->VkContext.LogicalDevice, this->Module, nullptr);
        this->Module = VK_NULL_HANDLE;
    }
}
