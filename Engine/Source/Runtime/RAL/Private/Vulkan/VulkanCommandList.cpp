#include "CoreMinimal.h"
#include "Vulkan/VulkanRAL.h"

FVulkanRALCommandList::FVulkanRALCommandList(FVulkanRALDevice* InDevice, EQueueType Type)
{
    VkCommandPoolCreateInfo PoolInfo{};
    {
        PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        PoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        PoolInfo.queueFamilyIndex = this->Device->VkContext.GraphicsFamilyIndex; // assume graphics
    }
    vkCreateCommandPool(this->Device->VkContext.LogicalDevice, &PoolInfo, nullptr, &this->Pool);

    VkCommandBufferAllocateInfo AllocateInfo{};
    {
        AllocateInfo.commandPool = this->Pool;
        AllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        AllocateInfo.commandBufferCount = 1;
    }
    vkAllocateCommandBuffers(this->Device->VkContext.LogicalDevice, &AllocateInfo, &this->Handle);
}

void FVulkanRALCommandList::Begin()
{
    VkCommandBufferBeginInfo BeginInfo{};
    {
        BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    }
    vkBeginCommandBuffer(this->Handle, &BeginInfo);

    vkCmdBindDescriptorSets(
        this->Handle,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        this->Device->VkContext.PipelineLayout,
        0,
        1,
        &this->Device->VkContext.BindlessDescriptorSet,
        0,
        nullptr
    );
}

void FVulkanRALCommandList::End()
{
    vkEndCommandBuffer(this->Handle);
}

void FVulkanRALCommandList::BeginRenderPass(const FRALRenderPassDesc& Desc)
{
    VkRenderPass RenderPass = this->InternalGetRenderPass(Desc, true);
    VkFramebuffer Framebuffer = this->InternalGetFramebuffer(Desc, RenderPass, true);

    std::vector<VkClearValue> ClearValues;
    // TODO: kodak

    VkClearValue ColorClear;
    ColorClear.color = { 0.f, 0.f, 0.f, 1.f };

    VkRenderPassBeginInfo Info{};
    {
        Info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        Info.renderPass = RenderPass;
        Info.framebuffer = Framebuffer;
        Info.renderArea.offset = { 0, 0 };
        Info.renderArea.extent = { 0, 0 }; // TODO: kodak
        Info.clearValueCount = 1;
        Info.pClearValues = &ColorClear;
    }
    vkCmdBeginRenderPass(this->Handle, &Info, VK_SUBPASS_CONTENTS_INLINE);
}

void FVulkanRALCommandList::DrawIndexed(uint32 IndexCount, uint32 InstanceCount, uint32 FirstIndex, int32 VertexOffset, uint32 FirstInstance)
{
    vkCmdDrawIndexed(this->Handle, IndexCount, InstanceCount, FirstIndex, VertexOffset, FirstIndex);
}
