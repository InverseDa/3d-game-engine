#include "CoreMinimal.h"
#include "Vulkan/VulkanRAL.h"

FVulkanRALCommandList::FVulkanRALCommandList(FVulkanRALDevice* InDevice, EQueueType Type)
    : Device(InDevice)
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
        AllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        AllocateInfo.commandPool = this->Pool;
        AllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        AllocateInfo.commandBufferCount = 1;
    }
    vkAllocateCommandBuffers(this->Device->VkContext.LogicalDevice, &AllocateInfo, &this->Handle);
}

FVulkanRALCommandList::~FVulkanRALCommandList()
{
    // Command buffers are automatically freed when pool is destroyed
    if (this->Pool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(this->Device->VkContext.LogicalDevice, this->Pool, nullptr);
        this->Pool = VK_NULL_HANDLE;
    }
}

void FVulkanRALCommandList::Begin()
{
    VkCommandBufferBeginInfo BeginInfo{};
    {
        BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    }
    vkBeginCommandBuffer(this->Handle, &BeginInfo);
}

void FVulkanRALCommandList::End()
{
    vkEndCommandBuffer(this->Handle);
}

void FVulkanRALCommandList::BeginRenderPass(const FRALRenderPassDesc& Desc)
{
    VkRenderPass RenderPass = this->CurrentPipeline->RenderPass;
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
    vkCmdDrawIndexed(this->Handle, IndexCount, InstanceCount, FirstIndex, VertexOffset, FirstInstance);
}

void FVulkanRALCommandList::SetGraphicsPipeline(FRALPipeline_Graphics* Pipeline)
{
    this->CurrentPipeline = static_cast<FVulkanRALPipeline_Graphics*>(Pipeline);
    vkCmdBindPipeline(this->Handle, VK_PIPELINE_BIND_POINT_GRAPHICS, this->CurrentPipeline->Pipeline);
}

void FVulkanRALCommandList::SetViewport(const FRALViewport& Viewport)
{
    VkViewport VkView = {};
    {
        VkView.x = Viewport.X;
        VkView.y = Viewport.Y;
        VkView.width = Viewport.Width;
        VkView.height = Viewport.Height;
        VkView.minDepth = Viewport.MinDepth;
        VkView.maxDepth = Viewport.MaxDepth;
    }
    vkCmdSetViewport(this->Handle, 0, 1, &VkView);
}

void FVulkanRALCommandList::SetScissorRect(const FRALScissorRect& Scissor)
{
    VkRect2D Rect2D{};
    {
        Rect2D.offset = { Scissor.X, Scissor.Y };
        Rect2D.extent = { static_cast<uint32>(Scissor.Width), static_cast<uint32>(Scissor.Height) };
    }
    vkCmdSetScissor(this->Handle, 0, 1, &Rect2D);
}

void FVulkanRALCommandList::SetBindGroup(uint32 SetIndex, FRALBindGroup* BindGroup)
{
    const FVulkanRALBindGroup* VkBindGroup = static_cast<FVulkanRALBindGroup*>(BindGroup);
    const FRALBindGroupDesc& BindGroupDesc = VkBindGroup->GetDesc();
    const uint32 ActualSetIndex = BindGroupDesc.Layout ? BindGroupDesc.Layout->GetDesc().SetIndex : SetIndex;

    VkDescriptorSet Sets[] = { VkBindGroup->Set };
    vkCmdBindDescriptorSets(
        this->Handle,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        this->CurrentPipeline->PipelineLayout,
        ActualSetIndex,
        1,
        Sets,
        0,
        nullptr
    );
}

void FVulkanRALCommandList::EndRenderPass()
{
    vkCmdEndRenderPass(this->Handle);
}

void FVulkanRALCommandList::SetVertexBuffer(uint32 Slot, FRALBuffer* Buffer, uint64 Offset)
{
    const FVulkanRALBuffer* VkRALBuffer = static_cast<FVulkanRALBuffer*>(Buffer);
    VkBuffer BufferHandle = VkRALBuffer->Buffer;
    VkDeviceSize VkOffset = Offset;
    vkCmdBindVertexBuffers(this->Handle, Slot, 1, &BufferHandle, &VkOffset);
}

void FVulkanRALCommandList::SetIndexBuffer(FRALBuffer* Buffer, uint64 Offset, EPixelFormat IndexFormat)
{
    const FVulkanRALBuffer* VkRALBuffer = static_cast<FVulkanRALBuffer*>(Buffer);
    VkIndexType IndexType = (IndexFormat == EPixelFormat::R32_UINT) ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
    vkCmdBindIndexBuffer(this->Handle, VkRALBuffer->Buffer, Offset, IndexType);
}

void FVulkanRALCommandList::Draw(uint32 VertexCount, uint32 InstanceCount, uint32 FirstInstance)
{
    vkCmdDraw(this->Handle, VertexCount, InstanceCount, 0, FirstInstance);
}

void FVulkanRALCommandList::SetPushConstants(EShaderStage Stage, const void* Data, uint32 Size)
{
    // Map EShaderStage to VkShaderStageFlags
    VkShaderStageFlags StageFlags = 0;
    if (EnumHasAnyFlags(Stage, EShaderStage::Vertex))   StageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
    if (EnumHasAnyFlags(Stage, EShaderStage::Pixel))    StageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (EnumHasAnyFlags(Stage, EShaderStage::Compute))  StageFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
    if (EnumHasAnyFlags(Stage, EShaderStage::Geometry)) StageFlags |= VK_SHADER_STAGE_GEOMETRY_BIT;
    if (EnumHasAnyFlags(Stage, EShaderStage::Hull))     StageFlags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    if (EnumHasAnyFlags(Stage, EShaderStage::Domain))   StageFlags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;

    vkCmdPushConstants(this->Handle, this->CurrentPipeline->PipelineLayout, StageFlags, 0, Size, Data);
}


