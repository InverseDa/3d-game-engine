#include "CoreMinimal.h"
#include "Vulkan/VulkanRHI.h"

namespace LE
{

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

bool MapResourceState(ERHIResourceState State, EShaderStage ShaderStage, bool bIsTexture, FVulkanResourceStateMapping& Out)
{
    switch (State)
    {
    case ERHIResourceState::Undefined:
        Out.PipelineStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        Out.ImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        return true;
    case ERHIResourceState::RenderTarget:
        if (!bIsTexture) return false;
        Out.PipelineStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        Out.AccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        Out.ImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        return true;
    case ERHIResourceState::DepthStencilWrite:
        if (!bIsTexture) return false;
        Out.PipelineStages = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        Out.AccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        Out.ImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        return true;
    case ERHIResourceState::DepthStencilRead:
        if (!bIsTexture) return false;
        Out.PipelineStages = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        Out.AccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        Out.ImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        return true;
    case ERHIResourceState::ShaderResource:
        Out.PipelineStages = ToVkShaderPipelineStages(ShaderStage);
        Out.AccessMask = VK_ACCESS_SHADER_READ_BIT;
        Out.ImageLayout = bIsTexture ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        return true;
    case ERHIResourceState::UnorderedAccess:
        Out.PipelineStages = ToVkShaderPipelineStages(ShaderStage);
        Out.AccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        Out.ImageLayout = bIsTexture ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
        return true;
    case ERHIResourceState::CopySource:
        Out.PipelineStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
        Out.AccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        Out.ImageLayout = bIsTexture ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        return true;
    case ERHIResourceState::CopyDestination:
        Out.PipelineStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
        Out.AccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        Out.ImageLayout = bIsTexture ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        return true;
    case ERHIResourceState::VertexBuffer:
        if (bIsTexture) return false;
        Out.PipelineStages = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        Out.AccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        return true;
    case ERHIResourceState::IndexBuffer:
        if (bIsTexture) return false;
        Out.PipelineStages = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        Out.AccessMask = VK_ACCESS_INDEX_READ_BIT;
        return true;
    case ERHIResourceState::ConstantBuffer:
        if (bIsTexture) return false;
        Out.PipelineStages = ToVkShaderPipelineStages(ShaderStage);
        Out.AccessMask = VK_ACCESS_UNIFORM_READ_BIT;
        return true;
    case ERHIResourceState::IndirectArgument:
        if (bIsTexture) return false;
        Out.PipelineStages = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
        Out.AccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        return true;
    case ERHIResourceState::Present:
        if (!bIsTexture) return false;
        Out.PipelineStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        Out.ImageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        return true;
    case ERHIResourceState::Unknown:
    default:
        return false;
    }
}

VkImageAspectFlags GetTextureAspectMask(const FRHITextureDesc& Desc)
{
    if (!Desc.bIsDepthStencil) return VK_IMAGE_ASPECT_COLOR_BIT;
    return Desc.Format == EPixelFormat::D24_UNORM_S8_UINT
        ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
        : VK_IMAGE_ASPECT_DEPTH_BIT;
}

bool IsDepthStencilFormat(EPixelFormat Format)
{
    return Format == EPixelFormat::D32_FLOAT || Format == EPixelFormat::D24_UNORM_S8_UINT;
}

VkAttachmentLoadOp ToVkAttachmentLoadOp(EAttachmentLoadOp Op)
{
    switch (Op)
    {
    case EAttachmentLoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
    case EAttachmentLoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case EAttachmentLoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    default: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
}

VkAttachmentStoreOp ToVkAttachmentStoreOp(EAttachmentStoreOp Op)
{
    switch (Op)
    {
    case EAttachmentStoreOp::Store: return VK_ATTACHMENT_STORE_OP_STORE;
    case EAttachmentStoreOp::DontCare: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    default: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }
}
}

FVulkanRHICommandAllocator::FVulkanRHICommandAllocator(FVulkanRHIDevice* InDevice, const EQueueType InType)
    : Device(InDevice)
    , Type(InType)
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRHI, Error, "Command allocator creation failed: invalid device.");
        this->Pool = VK_NULL_HANDLE;
        return;
    }

    if (this->Type != EQueueType::Graphics)
    {
        LE_LOG(LogRHI, Error, "Command allocator creation failed: only the graphics queue is available.");
        return;
    }

    VkCommandPoolCreateInfo PoolInfo{};
    {
        PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        PoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        PoolInfo.queueFamilyIndex = this->Device->VkContext.GraphicsFamilyIndex;
    }
    const VkResult Result = vkCreateCommandPool(this->Device->VkContext.LogicalDevice, &PoolInfo, nullptr, &this->Pool);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRHI, Error, "vkCreateCommandPool failed. VkResult={}", static_cast<int32>(Result));
        this->Pool = VK_NULL_HANDLE;
    }
}

FVulkanRHICommandAllocator::~FVulkanRHICommandAllocator()
{
    if (this->Device != nullptr && this->Device->VkContext.LogicalDevice != VK_NULL_HANDLE &&
        this->Pool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(this->Device->VkContext.LogicalDevice, this->Pool, nullptr);
    }
    this->Pool = VK_NULL_HANDLE;
}

bool FVulkanRHICommandAllocator::Reset()
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE ||
        this->Pool == VK_NULL_HANDLE)
    {
        LE_LOG(LogRHI, Error, "Command allocator reset failed: invalid device or pool.");
        return false;
    }

    const VkResult Result = vkResetCommandPool(this->Device->VkContext.LogicalDevice, this->Pool, 0);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRHI, Error, "vkResetCommandPool failed. VkResult={}", static_cast<int32>(Result));
        return false;
    }
    return true;
}

FVulkanRHICommandList::FVulkanRHICommandList(FVulkanRHICommandAllocator* InAllocator)
    : Device(InAllocator != nullptr ? InAllocator->Device : nullptr)
    , Allocator(InAllocator)
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE ||
        this->Allocator == nullptr || this->Allocator->Pool == VK_NULL_HANDLE)
    {
        LE_LOG(LogRHI, Error, "CommandList creation failed: invalid device or allocator.");
        this->Handle = VK_NULL_HANDLE;
        return;
    }

    VkCommandBufferAllocateInfo AllocateInfo{};
    {
        AllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        AllocateInfo.commandPool = this->Allocator->Pool;
        AllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        AllocateInfo.commandBufferCount = 1;
    }
    const VkResult Result = vkAllocateCommandBuffers(this->Device->VkContext.LogicalDevice, &AllocateInfo, &this->Handle);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRHI, Error, "vkAllocateCommandBuffers failed. VkResult={}", static_cast<int32>(Result));
        this->Handle = VK_NULL_HANDLE;
    }
}

FVulkanRHICommandList::~FVulkanRHICommandList()
{
    if (this->Device != nullptr && this->Device->VkContext.LogicalDevice != VK_NULL_HANDLE &&
        this->Allocator != nullptr && this->Allocator->Pool != VK_NULL_HANDLE && this->Handle != VK_NULL_HANDLE)
    {
        vkFreeCommandBuffers(this->Device->VkContext.LogicalDevice, this->Allocator->Pool, 1, &this->Handle);
    }
    this->Handle = VK_NULL_HANDLE;
}

void FVulkanRHICommandList::Begin()
{
    static_cast<void>(BeginChecked());
}

bool FVulkanRHICommandList::BeginChecked()
{
    if (this->Handle == VK_NULL_HANDLE || bRecording)
    {
        LE_LOG(LogRHI, Error, "CommandList Begin rejected an invalid or active command buffer.");
        return false;
    }

    bExecutable = false;
    CurrentPipeline = nullptr;
    bInsideRendering = false;
    bUploadCopied = false;
    bUploadComplete = false;
    UploadTexture = nullptr;
    ActiveRenderingSignature.Reset();
    PendingPresentTransitionImages.Clear();
    VkCommandBufferBeginInfo BeginInfo{};
    {
        BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    }
    const VkResult Result = vkBeginCommandBuffer(this->Handle, &BeginInfo);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRHI, Error, "vkBeginCommandBuffer failed. VkResult={}", static_cast<int32>(Result));
        return false;
    }
    bRecording = true;
    return true;
}

void FVulkanRHICommandList::End()
{
    static_cast<void>(EndChecked());
}

bool FVulkanRHICommandList::EndChecked()
{
    if (!bRecording || this->Handle == VK_NULL_HANDLE || bInsideRendering || UploadTexture != nullptr)
    {
        LE_LOG(LogRHI, Error, "CommandList End rejected an inactive buffer or an unfinished scope.");
        return false;
    }
    bRecording = false;
    const VkResult Result = vkEndCommandBuffer(this->Handle);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRHI, Error, "vkEndCommandBuffer failed. VkResult={}", static_cast<int32>(Result));
        return false;
    }
    bExecutable = true;
    return true;
}

bool FVulkanRHICommandList::TextureUploadBarrier(
    FRHITexture* Texture, ERHIResourceState Before, ERHIResourceState After)
{
    auto* const Image = dynamic_cast<FVulkanRHITexture*>(Texture);
    if (!bRecording || bInsideRendering || bUploadComplete || Image == nullptr ||
        Image->Device != Device || Image->Image == VK_NULL_HANDLE || Image->Memory == VK_NULL_HANDLE ||
        !Image->bOwnsImage || !IsTextureUploadDestination(Image->GetDesc()))
    {
        return false;
    }

    const bool bStart = Before == ERHIResourceState::Undefined &&
        After == ERHIResourceState::CopyDestination && UploadTexture == nullptr;
    const bool bFinish = Before == ERHIResourceState::CopyDestination &&
        After == ERHIResourceState::ShaderResource && UploadTexture == Image && bUploadCopied;
    if (!bStart && !bFinish)
    {
        return false;
    }

    FVulkanResourceStateMapping Source;
    FVulkanResourceStateMapping Destination;
    if (!MapResourceState(Before, EShaderStage::None, true, Source) ||
        !MapResourceState(After, EShaderStage::Pixel, true, Destination))
    {
        return false;
    }
    VkImageMemoryBarrier Barrier{};
    Barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    Barrier.srcAccessMask = Source.AccessMask;
    Barrier.dstAccessMask = Destination.AccessMask;
    Barrier.oldLayout = Source.ImageLayout;
    Barrier.newLayout = Destination.ImageLayout;
    Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image = Image->Image;
    Barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(Handle, Source.PipelineStages, Destination.PipelineStages, 0,
        0, nullptr, 0, nullptr, 1, &Barrier);
    UploadTexture = bStart ? Image : nullptr;
    bUploadComplete = bFinish;
    return true;
}

bool FVulkanRHICommandList::CopyBufferToTexture2D(
    FRHIBuffer* Source, FRHITexture* Destination, uint64 SourceOffset)
{
    auto* const Buffer = dynamic_cast<FVulkanRHIBuffer*>(Source);
    auto* const Image = dynamic_cast<FVulkanRHITexture*>(Destination);
    if (!bRecording || bInsideRendering || bUploadCopied || Image == nullptr || Image != UploadTexture ||
        Buffer == nullptr || Buffer->Device != Device || Buffer->Buffer == VK_NULL_HANDLE ||
        Buffer->Memory == VK_NULL_HANDLE || Buffer->MappedPtr != nullptr ||
        !IsBufferToTexture2DCopyValid(Buffer->GetDesc(), Image->GetDesc(), SourceOffset))
    {
        return false;
    }

    const FRHITextureDesc& Desc = Image->GetDesc();
    VkBufferImageCopy Region{};
    Region.bufferOffset = SourceOffset;
    Region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    Region.imageExtent = {Desc.Width, Desc.Height, 1};
    vkCmdCopyBufferToImage(Handle, Buffer->Buffer, Image->Image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);
    bUploadCopied = true;
    return true;
}

void FVulkanRHICommandList::ResourceBarriers(const FRHIBarrierBatch& Barriers)
{
    if (this->Handle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRHI, Error, "ResourceBarriers skipped: command buffer handle is null.");
        return;
    }

    LE::Array<VkImageMemoryBarrier> ImageBarriers;
    LE::Array<VkBufferMemoryBarrier> BufferBarriers;
    ImageBarriers.Reserve(Barriers.TextureBarriers.Size());
    BufferBarriers.Reserve(Barriers.BufferBarriers.Size());

    VkPipelineStageFlags SourceStages = 0;
    VkPipelineStageFlags DestinationStages = 0;

    for (const FRHITextureBarrierDesc& Desc : Barriers.TextureBarriers)
    {
        FVulkanRHITexture* Texture = static_cast<FVulkanRHITexture*>(Desc.Texture);
        if (Texture == nullptr || Texture->Image == VK_NULL_HANDLE)
        {
            LE_LOG(LogRHI, Error, "Texture barrier skipped: texture is null or invalid.");
            continue;
        }

        FVulkanResourceStateMapping Before;
        FVulkanResourceStateMapping After;
        if (!MapResourceState(Desc.BeforeState, Desc.BeforeShaderStage, true, Before) ||
            !MapResourceState(Desc.AfterState, Desc.AfterShaderStage, true, After))
        {
            LE_LOG(LogRHI, Error, "Texture barrier skipped: unknown or incompatible state. Before={}, After={}.",
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
        ImageBarriers.PushBack(Barrier);
    }

    for (const FRHIBufferBarrierDesc& Desc : Barriers.BufferBarriers)
    {
        FVulkanRHIBuffer* Buffer = static_cast<FVulkanRHIBuffer*>(Desc.Buffer);
        if (Buffer == nullptr || Buffer->Buffer == VK_NULL_HANDLE)
        {
            LE_LOG(LogRHI, Error, "Buffer barrier skipped: buffer is null or invalid.");
            continue;
        }

        FVulkanResourceStateMapping Before;
        FVulkanResourceStateMapping After;
        if (!MapResourceState(Desc.BeforeState, Desc.BeforeShaderStage, false, Before) ||
            !MapResourceState(Desc.AfterState, Desc.AfterShaderStage, false, After))
        {
            LE_LOG(LogRHI, Error, "Buffer barrier skipped: unknown or incompatible state. Before={}, After={}.",
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
        BufferBarriers.PushBack(Barrier);
    }

    if (ImageBarriers.IsEmpty() && BufferBarriers.IsEmpty())
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
        static_cast<uint32>(BufferBarriers.Size()),
        BufferBarriers.Data(),
        static_cast<uint32>(ImageBarriers.Size()),
        ImageBarriers.Data());
}

void FVulkanRHICommandList::BeginRenderPass(const FRHIRenderPassDesc& Desc)
{
    if (!bRecording || UploadTexture != nullptr || this->Handle == VK_NULL_HANDLE ||
        this->Device == nullptr || !this->Device->VkContext.bDynamicRenderingEnabled ||
        this->Device->VkContext.CmdBeginRendering == nullptr)
    {
        LE_LOG(LogRHI, Error, "BeginRenderPass failed: dynamic rendering is unavailable.");
        return;
    }
    if (this->bInsideRendering)
    {
        LE_LOG(LogRHI, Error, "BeginRenderPass failed: a rendering scope is already active.");
        return;
    }
    if (Desc.ColorAttachmentCount > 8 || (Desc.ColorAttachmentCount == 0 && !Desc.bHasDepthStencil))
    {
        LE_LOG(LogRHI, Error, "BeginRenderPass failed: rendering requires at least one color or depth/stencil attachment. Colors={}, HasDepthStencil={}.",
            Desc.ColorAttachmentCount, Desc.bHasDepthStencil);
        return;
    }
    const FRHIPipelineDesc_Graphics* const PipelineDesc =
        this->CurrentPipeline != nullptr && this->CurrentPipeline->Pipeline != VK_NULL_HANDLE
        ? &this->CurrentPipeline->GetDesc()
        : nullptr;
    if (PipelineDesc != nullptr && PipelineDesc->RenderTargetCount != Desc.ColorAttachmentCount)
    {
        LE_LOG(LogRHI, Error, "BeginRenderPass failed: attachment count does not match pipeline. Pass={}, Pipeline={}.",
            Desc.ColorAttachmentCount, PipelineDesc->RenderTargetCount);
        return;
    }
    const bool bPipelineHasDepthStencil = PipelineDesc != nullptr &&
        PipelineDesc->DepthStencilFormat != EPixelFormat::Unknown;
    if (PipelineDesc != nullptr && bPipelineHasDepthStencil != Desc.bHasDepthStencil)
    {
        LE_LOG(LogRHI, Error, "BeginRenderPass failed: depth/stencil attachment presence does not match pipeline. Pass={}, Pipeline={}.",
            Desc.bHasDepthStencil, bPipelineHasDepthStencil);
        return;
    }

    LE::Array<VkRenderingAttachmentInfo> ColorAttachments;
    ColorAttachments.Reserve(Desc.ColorAttachmentCount);
    FRHIRenderingAttachmentSignature RenderingSignature;
    RenderingSignature.RenderTargetCount = Desc.ColorAttachmentCount;
    uint32 AttachmentWidth = 0;
    uint32 AttachmentHeight = 0;
    this->PendingPresentTransitionImages.Clear();
    for (uint32 i = 0; i < Desc.ColorAttachmentCount; ++i)
    {
        FVulkanRHITextureView* View = static_cast<FVulkanRHITextureView*>(Desc.ColorAttachments[i].RenderTarget);
        if (View == nullptr || View->Owner == nullptr || View->View == VK_NULL_HANDLE)
        {
            LE_LOG(LogRHI, Error, "BeginRenderPass failed: color attachment {} is invalid.", i);
            return;
        }
        const FRHITextureDesc& TextureDesc = View->Owner->GetDesc();
        const EPixelFormat ViewFormat = View->GetDesc().Format != EPixelFormat::Unknown ? View->GetDesc().Format : TextureDesc.Format;
        RenderingSignature.RenderTargetFormats[i] = ViewFormat;
        if (!TextureDesc.bIsRenderTarget || ViewFormat == EPixelFormat::Unknown ||
            IsDepthStencilFormat(ViewFormat))
        {
            LE_LOG(LogRHI, Error,
                "BeginRenderPass failed: color attachment {} does not use a color render-target format. Format={}.",
                i, static_cast<uint32>(ViewFormat));
            return;
        }
        if (PipelineDesc != nullptr && ViewFormat != PipelineDesc->RenderTargetFormats[i])
        {
            LE_LOG(LogRHI, Error, "BeginRenderPass failed: color attachment {} format does not match pipeline. View={}, Pipeline={}.",
                i, static_cast<uint32>(ViewFormat), static_cast<uint32>(PipelineDesc->RenderTargetFormats[i]));
            return;
        }

        const uint32 MipSlice = View->GetDesc().MipSlice;
        const uint32 ViewWidth = std::max(1u, TextureDesc.Width >> MipSlice);
        const uint32 ViewHeight = std::max(1u, TextureDesc.Height >> MipSlice);
        if (i == 0)
        {
            AttachmentWidth = ViewWidth;
            AttachmentHeight = ViewHeight;
        }
        else if (AttachmentWidth != ViewWidth || AttachmentHeight != ViewHeight)
        {
            LE_LOG(LogRHI, Error, "BeginRenderPass failed: MRT attachment extents do not match at index {}.", i);
            return;
        }

        VkRenderingAttachmentInfo AttachmentInfo{};
        AttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        AttachmentInfo.imageView = View->View;
        AttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        AttachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE;
        AttachmentInfo.loadOp = ToVkAttachmentLoadOp(Desc.ColorAttachments[i].LoadOp);
        AttachmentInfo.storeOp = ToVkAttachmentStoreOp(Desc.ColorAttachments[i].StoreOp);
        for (uint32 Component = 0; Component < 4; ++Component)
        {
            AttachmentInfo.clearValue.color.float32[Component] = Desc.ColorAttachments[i].ClearColor[Component];
        }
        ColorAttachments.PushBack(AttachmentInfo);

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
                this->PendingPresentTransitionImages.PushBack(View->Owner->Image);
            }
        }
    }

    VkRenderingAttachmentInfo DepthStencilAttachment{};
    if (Desc.bHasDepthStencil)
    {
        FVulkanRHITextureView* View = static_cast<FVulkanRHITextureView*>(Desc.DepthStencilAttachment.DepthStencilTarget);
        if (View == nullptr || View->Owner == nullptr || View->View == VK_NULL_HANDLE)
        {
            LE_LOG(LogRHI, Error, "BeginRenderPass failed: depth/stencil attachment is invalid.");
            this->PendingPresentTransitionImages.Clear();
            return;
        }

        const FRHITextureDesc& TextureDesc = View->Owner->GetDesc();
        const EPixelFormat ViewFormat = View->GetDesc().Format != EPixelFormat::Unknown ? View->GetDesc().Format : TextureDesc.Format;
        RenderingSignature.DepthStencilFormat = ViewFormat;
        if (!TextureDesc.bIsDepthStencil || !IsDepthStencilFormat(ViewFormat))
        {
            LE_LOG(LogRHI, Error, "BeginRenderPass failed: depth/stencil attachment does not use a supported depth/stencil format. Format={}.",
                static_cast<uint32>(ViewFormat));
            this->PendingPresentTransitionImages.Clear();
            return;
        }
        if (PipelineDesc != nullptr && ViewFormat != PipelineDesc->DepthStencilFormat)
        {
            LE_LOG(LogRHI, Error, "BeginRenderPass failed: depth/stencil attachment format does not match pipeline. View={}, Pipeline={}.",
                static_cast<uint32>(ViewFormat), static_cast<uint32>(PipelineDesc->DepthStencilFormat));
            this->PendingPresentTransitionImages.Clear();
            return;
        }

        const uint32 MipSlice = View->GetDesc().MipSlice;
        const uint32 ViewWidth = std::max(1u, TextureDesc.Width >> MipSlice);
        const uint32 ViewHeight = std::max(1u, TextureDesc.Height >> MipSlice);
        if (AttachmentWidth == 0 && AttachmentHeight == 0)
        {
            AttachmentWidth = ViewWidth;
            AttachmentHeight = ViewHeight;
        }
        else if (AttachmentWidth != ViewWidth || AttachmentHeight != ViewHeight)
        {
            LE_LOG(LogRHI, Error, "BeginRenderPass failed: depth/stencil attachment extent does not match color attachments.");
            this->PendingPresentTransitionImages.Clear();
            return;
        }

        DepthStencilAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        DepthStencilAttachment.imageView = View->View;
        DepthStencilAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        DepthStencilAttachment.resolveMode = VK_RESOLVE_MODE_NONE;
        DepthStencilAttachment.loadOp = ToVkAttachmentLoadOp(Desc.DepthStencilAttachment.LoadOp);
        DepthStencilAttachment.storeOp = ToVkAttachmentStoreOp(Desc.DepthStencilAttachment.StoreOp);
        DepthStencilAttachment.clearValue.depthStencil.depth = Desc.DepthStencilAttachment.ClearDepth;
        DepthStencilAttachment.clearValue.depthStencil.stencil = Desc.DepthStencilAttachment.ClearStencil;
    }

    if (Desc.RenderArea.X < 0 || Desc.RenderArea.Y < 0 ||
        static_cast<uint32>(Desc.RenderArea.X) >= AttachmentWidth ||
        static_cast<uint32>(Desc.RenderArea.Y) >= AttachmentHeight)
    {
        LE_LOG(LogRHI, Error, "BeginRenderPass failed: render area origin is outside attachment bounds.");
        this->PendingPresentTransitionImages.Clear();
        return;
    }

    const uint32 RenderWidth = Desc.RenderArea.Width != 0 ? Desc.RenderArea.Width : AttachmentWidth - static_cast<uint32>(Desc.RenderArea.X);
    const uint32 RenderHeight = Desc.RenderArea.Height != 0 ? Desc.RenderArea.Height : AttachmentHeight - static_cast<uint32>(Desc.RenderArea.Y);
    if (RenderWidth == 0 || RenderHeight == 0 ||
        static_cast<uint64>(Desc.RenderArea.X) + RenderWidth > AttachmentWidth ||
        static_cast<uint64>(Desc.RenderArea.Y) + RenderHeight > AttachmentHeight)
    {
        LE_LOG(LogRHI, Error, "BeginRenderPass failed: render area is outside attachment bounds.");
        this->PendingPresentTransitionImages.Clear();
        return;
    }

    VkRenderingInfo Info{};
    {
        Info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        Info.renderArea.offset = { Desc.RenderArea.X, Desc.RenderArea.Y };
        Info.renderArea.extent = { RenderWidth, RenderHeight };
        Info.layerCount = 1;
        Info.colorAttachmentCount = static_cast<uint32>(ColorAttachments.Size());
        Info.pColorAttachments = ColorAttachments.IsEmpty() ? nullptr : ColorAttachments.Data();
        Info.pDepthAttachment = Desc.bHasDepthStencil ? &DepthStencilAttachment : nullptr;
        Info.pStencilAttachment = Desc.bHasDepthStencil &&
            RenderingSignature.DepthStencilFormat == EPixelFormat::D24_UNORM_S8_UINT
            ? &DepthStencilAttachment
            : nullptr;
    }
    this->Device->VkContext.CmdBeginRendering(this->Handle, &Info);
    this->ActiveRenderingSignature = RenderingSignature;
    this->bInsideRendering = true;
}

void FVulkanRHICommandList::DrawIndexed(uint32 IndexCount, uint32 InstanceCount, uint32 FirstIndex, int32 VertexOffset, uint32 FirstInstance)
{
    vkCmdDrawIndexed(this->Handle, IndexCount, InstanceCount, FirstIndex, VertexOffset, FirstInstance);
}

void FVulkanRHICommandList::SetGraphicsPipeline(FRHIPipeline_Graphics* Pipeline)
{
    if (Pipeline == nullptr)
    {
        LE_LOG(LogRHI, Warn, "SetGraphicsPipeline skipped: pipeline is null.");
        return;
    }

    FVulkanRHIPipeline_Graphics* const Candidate =
        static_cast<FVulkanRHIPipeline_Graphics*>(Pipeline);
    const FRHIPipelineDesc_Graphics& CandidateDesc = Candidate->GetDesc();
    if (this->bInsideRendering)
    {
        if (!IsRenderingSignatureCompatible(this->ActiveRenderingSignature, CandidateDesc))
        {
            LE_LOG(LogRHI, Error,
                "SetGraphicsPipeline skipped: pipeline attachment signature does not match the active rendering scope.");
            return;
        }
    }

    this->CurrentPipeline = Candidate;
    if (this->CurrentPipeline->Pipeline != VK_NULL_HANDLE)
    {
        vkCmdBindPipeline(this->Handle, VK_PIPELINE_BIND_POINT_GRAPHICS, this->CurrentPipeline->Pipeline);
    }
}

void FVulkanRHICommandList::SetViewport(const FRHIViewport& Viewport)
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

void FVulkanRHICommandList::SetScissorRect(const FRHIScissorRect& Scissor)
{
    VkRect2D Rect2D{};
    {
        Rect2D.offset = { Scissor.X, Scissor.Y };
        Rect2D.extent = { static_cast<uint32>(Scissor.Width), static_cast<uint32>(Scissor.Height) };
    }
    vkCmdSetScissor(this->Handle, 0, 1, &Rect2D);
}

void FVulkanRHICommandList::SetBindGroup(uint32 SetIndex, FRHIBindGroup* BindGroup)
{
    if (this->CurrentPipeline == nullptr || this->CurrentPipeline->PipelineLayout == VK_NULL_HANDLE || BindGroup == nullptr)
    {
        LE_LOG(LogRHI, Warn, "SetBindGroup skipped: invalid pipeline/bind group.");
        return;
    }

    const FVulkanRHIBindGroup* VkBindGroup = static_cast<FVulkanRHIBindGroup*>(BindGroup);
    const FRHIBindGroupDesc& BindGroupDesc = VkBindGroup->GetDesc();
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

void FVulkanRHICommandList::EndRenderPass()
{
    if (this->Handle == VK_NULL_HANDLE || this->Device == nullptr ||
        this->Device->VkContext.CmdEndRendering == nullptr || !this->bInsideRendering)
    {
        LE_LOG(LogRHI, Error, "EndRenderPass failed: no valid dynamic rendering scope is active.");
        return;
    }

    this->Device->VkContext.CmdEndRendering(this->Handle);
    this->bInsideRendering = false;
    this->ActiveRenderingSignature.Reset();

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
    this->PendingPresentTransitionImages.Clear();
}

void FVulkanRHICommandList::SetVertexBuffer(uint32 Slot, FRHIBuffer* Buffer, uint64 Offset)
{
    if (Buffer == nullptr)
    {
        LE_LOG(LogRHI, Error, "SetVertexBuffer failed: buffer is null.");
        return;
    }
    const FVulkanRHIBuffer* VkRHIBuffer = static_cast<FVulkanRHIBuffer*>(Buffer);
    if (VkRHIBuffer->Buffer == VK_NULL_HANDLE)
    {
        LE_LOG(LogRHI, Error, "SetVertexBuffer failed: Vulkan buffer handle is null.");
        return;
    }
    VkBuffer BufferHandle = VkRHIBuffer->Buffer;
    VkDeviceSize VkOffset = Offset;
    vkCmdBindVertexBuffers(this->Handle, Slot, 1, &BufferHandle, &VkOffset);
}

void FVulkanRHICommandList::SetIndexBuffer(FRHIBuffer* Buffer, uint64 Offset, EPixelFormat IndexFormat)
{
    if (!IsValidIndexFormat(IndexFormat))
    {
        LE_LOG(LogRHI, Error, "SetIndexBuffer failed: format {} is not R16_UINT or R32_UINT.",
            static_cast<uint32>(IndexFormat));
        return;
    }
    if (Buffer == nullptr)
    {
        LE_LOG(LogRHI, Error, "SetIndexBuffer failed: buffer is null.");
        return;
    }
    const FVulkanRHIBuffer* VkRHIBuffer = static_cast<FVulkanRHIBuffer*>(Buffer);
    if (VkRHIBuffer->Buffer == VK_NULL_HANDLE)
    {
        LE_LOG(LogRHI, Error, "SetIndexBuffer failed: Vulkan buffer handle is null.");
        return;
    }
    const VkIndexType IndexType = IndexFormat == EPixelFormat::R32_UINT
        ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
    vkCmdBindIndexBuffer(this->Handle, VkRHIBuffer->Buffer, Offset, IndexType);
}

void FVulkanRHICommandList::Draw(uint32 VertexCount, uint32 InstanceCount, uint32 FirstInstance)
{
    vkCmdDraw(this->Handle, VertexCount, InstanceCount, 0, FirstInstance);
}

void FVulkanRHICommandList::SetPushConstants(EShaderStage Stage, const void* Data, uint32 Size)
{
    if (this->CurrentPipeline == nullptr || this->CurrentPipeline->PipelineLayout == VK_NULL_HANDLE || Data == nullptr || Size == 0)
    {
        LE_LOG(LogRHI, Warn, "SetPushConstants skipped: invalid pipeline or push constant data.");
        return;
    }

    const FRHIPushConstantRangeDesc& Contract =
        this->CurrentPipeline->GetDesc().PushConstants;
    if (!Contract.AcceptsWrite(Stage, Size))
    {
        LE_LOG(LogRHI, Error,
            "SetPushConstants skipped: call does not exactly match the bound pipeline contract. Stages={}/{}, Size={}/{}.",
            static_cast<uint32>(Stage),
            static_cast<uint32>(Contract.StageFlags),
            Size,
            Contract.Size);
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

} // namespace LE
