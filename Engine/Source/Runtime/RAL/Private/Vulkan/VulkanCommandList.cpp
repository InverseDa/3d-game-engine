#include "CoreMinimal.h"
#include "Vulkan/VulkanRAL.h"

namespace
{
struct FVulkanResourceStateMapping
{
    VkPipelineStageFlags PipelineStages = 0;
    VkAccessFlags AccessMask = 0;
    VkImageLayout ImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

VkPipelineStageFlags ToVkShaderPipelineStages(EShaderStage ShaderStage)
{
    VkPipelineStageFlags Result = 0;
    if (EnumHasAnyFlags(ShaderStage, EShaderStage::Vertex)) Result |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    if (EnumHasAnyFlags(ShaderStage, EShaderStage::Pixel)) Result |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    if (EnumHasAnyFlags(ShaderStage, EShaderStage::Compute)) Result |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    if (EnumHasAnyFlags(ShaderStage, EShaderStage::Geometry)) Result |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
    if (EnumHasAnyFlags(ShaderStage, EShaderStage::Hull)) Result |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT;
    if (EnumHasAnyFlags(ShaderStage, EShaderStage::Domain)) Result |= VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
    return Result != 0 ? Result : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
}

bool MapResourceState(ERALResourceState State, EShaderStage ShaderStage, bool bIsTexture, FVulkanResourceStateMapping& Out)
{
    switch (State)
    {
    case ERALResourceState::Undefined:
        Out.PipelineStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        Out.ImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        return true;
    case ERALResourceState::RenderTarget:
        if (!bIsTexture) return false;
        Out.PipelineStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        Out.AccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        Out.ImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        return true;
    case ERALResourceState::DepthStencilWrite:
        if (!bIsTexture) return false;
        Out.PipelineStages = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        Out.AccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        Out.ImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        return true;
    case ERALResourceState::DepthStencilRead:
        if (!bIsTexture) return false;
        Out.PipelineStages = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        Out.AccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        Out.ImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        return true;
    case ERALResourceState::ShaderResource:
        Out.PipelineStages = ToVkShaderPipelineStages(ShaderStage);
        Out.AccessMask = VK_ACCESS_SHADER_READ_BIT;
        Out.ImageLayout = bIsTexture ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        return true;
    case ERALResourceState::UnorderedAccess:
        Out.PipelineStages = ToVkShaderPipelineStages(ShaderStage);
        Out.AccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        Out.ImageLayout = bIsTexture ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
        return true;
    case ERALResourceState::CopySource:
        Out.PipelineStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
        Out.AccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        Out.ImageLayout = bIsTexture ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        return true;
    case ERALResourceState::CopyDestination:
        Out.PipelineStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
        Out.AccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        Out.ImageLayout = bIsTexture ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        return true;
    case ERALResourceState::VertexBuffer:
        if (bIsTexture) return false;
        Out.PipelineStages = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        Out.AccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        return true;
    case ERALResourceState::IndexBuffer:
        if (bIsTexture) return false;
        Out.PipelineStages = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        Out.AccessMask = VK_ACCESS_INDEX_READ_BIT;
        return true;
    case ERALResourceState::ConstantBuffer:
        if (bIsTexture) return false;
        Out.PipelineStages = ToVkShaderPipelineStages(ShaderStage);
        Out.AccessMask = VK_ACCESS_UNIFORM_READ_BIT;
        return true;
    case ERALResourceState::IndirectArgument:
        if (bIsTexture) return false;
        Out.PipelineStages = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
        Out.AccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        return true;
    case ERALResourceState::Present:
        if (!bIsTexture) return false;
        Out.PipelineStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        Out.ImageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        return true;
    case ERALResourceState::Unknown:
    default:
        return false;
    }
}

VkImageAspectFlags GetTextureAspectMask(const FRALTextureDesc& Desc)
{
    if (!Desc.bIsDepthStencil) return VK_IMAGE_ASPECT_COLOR_BIT;
    return Desc.Format == EPixelFormat::D24_UNORM_S8_UINT
        ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
        : VK_IMAGE_ASPECT_DEPTH_BIT;
}
}

FVulkanRALCommandList::FVulkanRALCommandList(FVulkanRALDevice* InDevice, EQueueType Type)
    : Device(InDevice)
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "CommandList creation failed: invalid device.");
        this->Pool = VK_NULL_HANDLE;
        this->Handle = VK_NULL_HANDLE;
        return;
    }

    VkCommandPoolCreateInfo PoolInfo{};
    {
        PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        PoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        PoolInfo.queueFamilyIndex = this->Device->VkContext.GraphicsFamilyIndex; // assume graphics
    }
    VkResult Result = vkCreateCommandPool(this->Device->VkContext.LogicalDevice, &PoolInfo, nullptr, &this->Pool);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkCreateCommandPool failed. VkResult={}", static_cast<int32>(Result));
        this->Pool = VK_NULL_HANDLE;
        this->Handle = VK_NULL_HANDLE;
        return;
    }

    VkCommandBufferAllocateInfo AllocateInfo{};
    {
        AllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        AllocateInfo.commandPool = this->Pool;
        AllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        AllocateInfo.commandBufferCount = 1;
    }
    Result = vkAllocateCommandBuffers(this->Device->VkContext.LogicalDevice, &AllocateInfo, &this->Handle);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkAllocateCommandBuffers failed. VkResult={}", static_cast<int32>(Result));
        vkDestroyCommandPool(this->Device->VkContext.LogicalDevice, this->Pool, nullptr);
        this->Pool = VK_NULL_HANDLE;
        this->Handle = VK_NULL_HANDLE;
        return;
    }
}

FVulkanRALCommandList::~FVulkanRALCommandList()
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        this->FramebufferCache.clear();
        this->Pool = VK_NULL_HANDLE;
        return;
    }

    // 销毁缓存的 Framebuffer
    for (auto& Pair : this->FramebufferCache)
    {
        vkDestroyFramebuffer(this->Device->VkContext.LogicalDevice, Pair.second, nullptr);
    }
    this->FramebufferCache.clear();

    // Command buffers are automatically freed when pool is destroyed
    if (this->Pool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(this->Device->VkContext.LogicalDevice, this->Pool, nullptr);
        this->Pool = VK_NULL_HANDLE;
    }
}

void FVulkanRALCommandList::Begin()
{
    if (this->Handle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "CommandList Begin failed: command buffer handle is null.");
        return;
    }

    VkCommandBufferBeginInfo BeginInfo{};
    {
        BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    }
    const VkResult Result = vkBeginCommandBuffer(this->Handle, &BeginInfo);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkBeginCommandBuffer failed. VkResult={}", static_cast<int32>(Result));
    }
}

void FVulkanRALCommandList::End()
{
    if (this->Handle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "CommandList End failed: command buffer handle is null.");
        return;
    }
    const VkResult Result = vkEndCommandBuffer(this->Handle);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkEndCommandBuffer failed. VkResult={}", static_cast<int32>(Result));
    }
}

void FVulkanRALCommandList::ResourceBarriers(const FRALBarrierBatch& Barriers)
{
    if (this->Handle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "ResourceBarriers skipped: command buffer handle is null.");
        return;
    }

    std::vector<VkImageMemoryBarrier> ImageBarriers;
    std::vector<VkBufferMemoryBarrier> BufferBarriers;
    ImageBarriers.reserve(Barriers.TextureBarriers.size());
    BufferBarriers.reserve(Barriers.BufferBarriers.size());

    VkPipelineStageFlags SourceStages = 0;
    VkPipelineStageFlags DestinationStages = 0;

    for (const FRALTextureBarrierDesc& Desc : Barriers.TextureBarriers)
    {
        FVulkanRALTexture* Texture = static_cast<FVulkanRALTexture*>(Desc.Texture);
        if (Texture == nullptr || Texture->Image == VK_NULL_HANDLE)
        {
            LE_LOG(LogRAL, Error, "Texture barrier skipped: texture is null or invalid.");
            continue;
        }

        FVulkanResourceStateMapping Before;
        FVulkanResourceStateMapping After;
        if (!MapResourceState(Desc.BeforeState, Desc.BeforeShaderStage, true, Before) ||
            !MapResourceState(Desc.AfterState, Desc.AfterShaderStage, true, After))
        {
            LE_LOG(LogRAL, Error, "Texture barrier skipped: unknown or incompatible state. Before={}, After={}.",
                static_cast<uint32>(Desc.BeforeState), static_cast<uint32>(Desc.AfterState));
            continue;
        }

        VkImageMemoryBarrier Barrier{};
        Barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        Barrier.srcAccessMask = Before.AccessMask;
        Barrier.dstAccessMask = After.AccessMask;
        Barrier.oldLayout = Before.ImageLayout;
        Barrier.newLayout = After.ImageLayout;
        Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        Barrier.image = Texture->Image;
        Barrier.subresourceRange.aspectMask = GetTextureAspectMask(Texture->GetDesc());
        Barrier.subresourceRange.baseMipLevel = Desc.BaseMipLevel;
        Barrier.subresourceRange.levelCount = Desc.MipCount;
        Barrier.subresourceRange.baseArrayLayer = Desc.BaseArrayLayer;
        Barrier.subresourceRange.layerCount = Desc.LayerCount;

        SourceStages |= Before.PipelineStages;
        DestinationStages |= After.PipelineStages;
        ImageBarriers.push_back(Barrier);
    }

    for (const FRALBufferBarrierDesc& Desc : Barriers.BufferBarriers)
    {
        FVulkanRALBuffer* Buffer = static_cast<FVulkanRALBuffer*>(Desc.Buffer);
        if (Buffer == nullptr || Buffer->Buffer == VK_NULL_HANDLE)
        {
            LE_LOG(LogRAL, Error, "Buffer barrier skipped: buffer is null or invalid.");
            continue;
        }

        FVulkanResourceStateMapping Before;
        FVulkanResourceStateMapping After;
        if (!MapResourceState(Desc.BeforeState, Desc.BeforeShaderStage, false, Before) ||
            !MapResourceState(Desc.AfterState, Desc.AfterShaderStage, false, After))
        {
            LE_LOG(LogRAL, Error, "Buffer barrier skipped: unknown or incompatible state. Before={}, After={}.",
                static_cast<uint32>(Desc.BeforeState), static_cast<uint32>(Desc.AfterState));
            continue;
        }

        VkBufferMemoryBarrier Barrier{};
        Barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        Barrier.srcAccessMask = Before.AccessMask;
        Barrier.dstAccessMask = After.AccessMask;
        Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        Barrier.buffer = Buffer->Buffer;
        Barrier.offset = Desc.Offset;
        Barrier.size = Desc.Size == ~0ull ? VK_WHOLE_SIZE : Desc.Size;

        SourceStages |= Before.PipelineStages;
        DestinationStages |= After.PipelineStages;
        BufferBarriers.push_back(Barrier);
    }

    if (ImageBarriers.empty() && BufferBarriers.empty())
    {
        return;
    }

    vkCmdPipelineBarrier(
        this->Handle,
        SourceStages,
        DestinationStages,
        0,
        0,
        nullptr,
        static_cast<uint32>(BufferBarriers.size()),
        BufferBarriers.data(),
        static_cast<uint32>(ImageBarriers.size()),
        ImageBarriers.data());
}

void FVulkanRALCommandList::BeginRenderPass(const FRALRenderPassDesc& Desc)
{
    if (this->CurrentPipeline == nullptr || this->CurrentPipeline->RenderPass == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Warn, "BeginRenderPass skipped: pipeline or render pass is invalid.");
        return;
    }

    VkRenderPass RenderPass = this->CurrentPipeline->RenderPass;
    VkFramebuffer Framebuffer = this->InternalGetFramebuffer(Desc, RenderPass, true);
    if (Framebuffer == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "BeginRenderPass failed: framebuffer creation failed.");
        return;
    }

    // 获取渲染区域尺寸
    uint32 Width = 0, Height = 0;
    if (Desc.ColorAttachmentCount > 0)
    {
        FVulkanRALTextureView* View = static_cast<FVulkanRALTextureView*>(
            Desc.ColorAttachments[0].RenderTarget);
        if (View == nullptr || View->Owner == nullptr)
        {
            LE_LOG(LogRAL, Error, "BeginRenderPass failed: first color attachment is invalid.");
            return;
        }
        const FRALTextureDesc& TexDesc = View->Owner->GetDesc();
        Width = TexDesc.Width;
        Height = TexDesc.Height;
    }

    this->PendingPresentTransitionImages.clear();
    for (uint32 i = 0; i < Desc.ColorAttachmentCount; ++i)
    {
        FVulkanRALTextureView* View = static_cast<FVulkanRALTextureView*>(Desc.ColorAttachments[i].RenderTarget);
        if (View == nullptr || View->Owner == nullptr)
        {
            continue;
        }

        // Swapchain-wrapped images are externally owned and do not have VkDeviceMemory allocated here.
        if (View->Owner->Image != VK_NULL_HANDLE && View->Owner->Memory == VK_NULL_HANDLE)
        {
            bool bAlreadyTracked = false;
            for (VkImage TrackedImage : this->PendingPresentTransitionImages)
            {
                if (TrackedImage == View->Owner->Image)
                {
                    bAlreadyTracked = true;
                    break;
                }
            }
            if (!bAlreadyTracked)
            {
                this->PendingPresentTransitionImages.push_back(View->Owner->Image);
            }
        }
    }

    VkClearValue ColorClear;
    ColorClear.color = { 0.f, 0.f, 0.f, 1.f };

    VkRenderPassBeginInfo Info{};
    {
        Info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        Info.renderPass = RenderPass;
        Info.framebuffer = Framebuffer;
        Info.renderArea.offset = { 0, 0 };
        Info.renderArea.extent = { Width, Height };
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
    if (Pipeline == nullptr)
    {
        LE_LOG(LogRAL, Warn, "SetGraphicsPipeline skipped: pipeline is null.");
        return;
    }

    this->CurrentPipeline = static_cast<FVulkanRALPipeline_Graphics*>(Pipeline);
    if (this->CurrentPipeline->Pipeline != VK_NULL_HANDLE)
    {
        vkCmdBindPipeline(this->Handle, VK_PIPELINE_BIND_POINT_GRAPHICS, this->CurrentPipeline->Pipeline);
    }
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
    if (this->CurrentPipeline == nullptr || this->CurrentPipeline->PipelineLayout == VK_NULL_HANDLE || BindGroup == nullptr)
    {
        LE_LOG(LogRAL, Warn, "SetBindGroup skipped: invalid pipeline/bind group.");
        return;
    }

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
    if (this->Handle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "EndRenderPass failed: command buffer handle is null.");
        return;
    }

    vkCmdEndRenderPass(this->Handle);

    for (VkImage Image : this->PendingPresentTransitionImages)
    {
        VkImageMemoryBarrier Barrier{};
        {
            Barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            Barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            Barrier.dstAccessMask = 0;
            Barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            Barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            Barrier.image = Image;
            Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            Barrier.subresourceRange.baseMipLevel = 0;
            Barrier.subresourceRange.levelCount = 1;
            Barrier.subresourceRange.baseArrayLayer = 0;
            Barrier.subresourceRange.layerCount = 1;
        }

        vkCmdPipelineBarrier(
            this->Handle,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &Barrier
        );
    }
    this->PendingPresentTransitionImages.clear();
}

void FVulkanRALCommandList::SetVertexBuffer(uint32 Slot, FRALBuffer* Buffer, uint64 Offset)
{
    if (Buffer == nullptr)
    {
        LE_LOG(LogRAL, Error, "SetVertexBuffer failed: buffer is null.");
        return;
    }
    const FVulkanRALBuffer* VkRALBuffer = static_cast<FVulkanRALBuffer*>(Buffer);
    if (VkRALBuffer->Buffer == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "SetVertexBuffer failed: Vulkan buffer handle is null.");
        return;
    }
    VkBuffer BufferHandle = VkRALBuffer->Buffer;
    VkDeviceSize VkOffset = Offset;
    vkCmdBindVertexBuffers(this->Handle, Slot, 1, &BufferHandle, &VkOffset);
}

void FVulkanRALCommandList::SetIndexBuffer(FRALBuffer* Buffer, uint64 Offset, EPixelFormat IndexFormat)
{
    if (Buffer == nullptr)
    {
        LE_LOG(LogRAL, Error, "SetIndexBuffer failed: buffer is null.");
        return;
    }
    const FVulkanRALBuffer* VkRALBuffer = static_cast<FVulkanRALBuffer*>(Buffer);
    if (VkRALBuffer->Buffer == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "SetIndexBuffer failed: Vulkan buffer handle is null.");
        return;
    }
    VkIndexType IndexType = (IndexFormat == EPixelFormat::R32_UINT) ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
    vkCmdBindIndexBuffer(this->Handle, VkRALBuffer->Buffer, Offset, IndexType);
}

void FVulkanRALCommandList::Draw(uint32 VertexCount, uint32 InstanceCount, uint32 FirstInstance)
{
    vkCmdDraw(this->Handle, VertexCount, InstanceCount, 0, FirstInstance);
}

void FVulkanRALCommandList::SetPushConstants(EShaderStage Stage, const void* Data, uint32 Size)
{
    if (this->CurrentPipeline == nullptr || this->CurrentPipeline->PipelineLayout == VK_NULL_HANDLE || Data == nullptr || Size == 0)
    {
        LE_LOG(LogRAL, Warn, "SetPushConstants skipped: invalid pipeline or push constant data.");
        return;
    }

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

VkFramebuffer FVulkanRALCommandList::InternalGetFramebuffer(
    const FRALRenderPassDesc& Desc,
    VkRenderPass Pass,
    bool bIsCreate)
{
    // 1. 收集图像视图和尺寸
    std::vector<VkImageView> Attachments;
    uint32 Width = 0, Height = 0;

    for (uint32 i = 0; i < Desc.ColorAttachmentCount; ++i)
    {
        FVulkanRALTextureView* View = static_cast<FVulkanRALTextureView*>(
            Desc.ColorAttachments[i].RenderTarget);
        if (View == nullptr || View->Owner == nullptr || View->View == VK_NULL_HANDLE)
        {
            LE_LOG(LogRAL, Error, "Framebuffer creation failed: color attachment {} is invalid.", i);
            return VK_NULL_HANDLE;
        }
        Attachments.push_back(View->View);

        if (i == 0) {
            const FRALTextureDesc& TexDesc = View->Owner->GetDesc();
            Width = TexDesc.Width;
            Height = TexDesc.Height;
        }
    }

    if (Desc.bHasDepthStencil)
    {
        FVulkanRALTextureView* DepthView = static_cast<FVulkanRALTextureView*>(
            Desc.DepthStencilAttachment.DepthStencilTarget);
        if (DepthView == nullptr)
        {
            LE_LOG(LogRAL, Error, "Framebuffer creation failed: depth view is null.");
            return VK_NULL_HANDLE;
        }
        if (DepthView->View == VK_NULL_HANDLE)
        {
            LE_LOG(LogRAL, Error, "Framebuffer creation failed: depth image view handle is null.");
            return VK_NULL_HANDLE;
        }
        Attachments.push_back(DepthView->View);
    }

    // 2. 生成缓存哈希
    uint64 Hash = reinterpret_cast<uint64>(Pass);
    for (VkImageView View : Attachments) {
        Hash ^= reinterpret_cast<uint64>(View) + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
    }

    // 3. 检查缓存
    auto It = this->FramebufferCache.find(Hash);
    if (It != this->FramebufferCache.end()) {
        return It->second;
    }

    if (!bIsCreate) {
        return VK_NULL_HANDLE;
    }

    // 4. 创建 Framebuffer
    VkFramebufferCreateInfo FbInfo{};
    FbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    FbInfo.renderPass = Pass;
    FbInfo.attachmentCount = static_cast<uint32>(Attachments.size());
    FbInfo.pAttachments = Attachments.data();
    FbInfo.width = Width;
    FbInfo.height = Height;
    FbInfo.layers = 1;

    VkFramebuffer Framebuffer = VK_NULL_HANDLE;
    const VkResult Result = vkCreateFramebuffer(this->Device->VkContext.LogicalDevice, &FbInfo, nullptr, &Framebuffer);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkCreateFramebuffer failed. VkResult={}", static_cast<int32>(Result));
        return VK_NULL_HANDLE;
    }

    this->FramebufferCache[Hash] = Framebuffer;
    return Framebuffer;
}


