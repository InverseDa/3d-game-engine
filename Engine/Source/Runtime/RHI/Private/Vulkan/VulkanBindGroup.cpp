#include "CoreMinimal.h"
#include "Vulkan/VulkanRHI.h"

namespace LE
{

namespace
{
VkShaderStageFlags ToVkStageFlags(EShaderStage StageFlags)
{
    VkShaderStageFlags Flags = 0;
    if (EnumHasAnyFlags(StageFlags, EShaderStage::Vertex))
    {
        Flags |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if (EnumHasAnyFlags(StageFlags, EShaderStage::Pixel))
    {
        Flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if (EnumHasAnyFlags(StageFlags, EShaderStage::Compute))
    {
        Flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    }
    if (EnumHasAnyFlags(StageFlags, EShaderStage::Geometry))
    {
        Flags |= VK_SHADER_STAGE_GEOMETRY_BIT;
    }
    if (EnumHasAnyFlags(StageFlags, EShaderStage::Hull))
    {
        Flags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    }
    if (EnumHasAnyFlags(StageFlags, EShaderStage::Domain))
    {
        Flags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    }

    if (Flags == 0)
    {
        LE_LOG(LogRHI, Error, "Invalid shader stage flags for bind group layout.");
    }
    assert(Flags != 0);
    return Flags;
}

VkDescriptorType ToVkDescriptorType(ERHIBindGroupItemType Type)
{
    switch (Type)
    {
    case ERHIBindGroupItemType::UniformBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case ERHIBindGroupItemType::StorageBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case ERHIBindGroupItemType::SampledImage:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case ERHIBindGroupItemType::StorageImage:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case ERHIBindGroupItemType::Sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case ERHIBindGroupItemType::CombinedImageSampler:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    default:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
}

void CollectPoolSize(const FRHIBindGroupLayoutDesc& Desc, LE::Array<VkDescriptorPoolSize>& OutPoolSizes)
{
    OutPoolSizes.Clear();

    for (const FRHIBindGroupLayoutItem& Item : Desc.Bindings)
    {
        VkDescriptorType Type = ToVkDescriptorType(Item.Type);

        bool bFound = false;
        for (VkDescriptorPoolSize& Size : OutPoolSizes)
        {
            if (Size.type == Type)
            {
                Size.descriptorCount += Item.Count;
                bFound = true;
                break;
            }
        }
        if (!bFound)
        {
            VkDescriptorPoolSize Size{};
            Size.type = Type;
            Size.descriptorCount = Item.Count;
            OutPoolSizes.PushBack(Size);
        }
    }
}

const FRHIBindGroupLayoutItem* FindLayoutItem(const FRHIBindGroupLayoutDesc& LayoutDesc, uint32 Binding)
{
    for (const auto& Item : LayoutDesc.Bindings)
    {
        if (Item.Binding == Binding)
        {
            return &Item;
        }
    }
    return nullptr;
}
}

static void UpdateDescriptorSets(FVulkanRHIDevice* Device, VkDescriptorSet Set, const FRHIBindGroupDesc& Desc)
{
    if (Device == nullptr || Device->VkContext.LogicalDevice == VK_NULL_HANDLE || Desc.Layout == nullptr)
    {
        LE_LOG(LogRHI, Error, "UpdateDescriptorSets failed: invalid device/layout.");
        return;
    }

    const FRHIBindGroupLayoutDesc& LayoutDesc = Desc.Layout->GetDesc();

    LE::Array<VkWriteDescriptorSet> Writes;
    LE::Array<VkDescriptorBufferInfo> BufferInfos;
    LE::Array<VkDescriptorImageInfo> ImageInfos;

    Writes.Reserve(Desc.Items.Size());
    BufferInfos.Reserve(Desc.Items.Size());
    ImageInfos.Reserve(Desc.Items.Size());

    for (const FRHIBindGroupItem& Item : Desc.Items)
    {
        const FRHIBindGroupLayoutItem* LayoutItem = FindLayoutItem(LayoutDesc, Item.Binding);
        if (LayoutItem == nullptr)
        {
            LE_LOG(LogRHI, Warn, "UpdateDescriptorSets: binding {} not found in layout.", Item.Binding);
            continue;
        }

        VkWriteDescriptorSet Write{};
        {
            Write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            Write.dstSet = Set;
            Write.dstBinding = Item.Binding;
            Write.dstArrayElement = 0;
            Write.descriptorCount = 1;
            Write.descriptorType = ToVkDescriptorType(LayoutItem->Type);
        }

        switch (LayoutItem->Type)
        {
        case ERHIBindGroupItemType::UniformBuffer:
        case ERHIBindGroupItemType::StorageBuffer: {
            if (Item.Buffer == nullptr)
            {
                LE_LOG(LogRHI, Error, "UpdateDescriptorSets failed: buffer item at binding {} is null.", Item.Binding);
                continue;
            }
            FVulkanRHIBuffer* VkBuffer = static_cast<FVulkanRHIBuffer*>(Item.Buffer);
            VkDescriptorBufferInfo Info{};
            {
                Info.buffer = VkBuffer->Buffer;
                Info.offset = Item.Offset;
                Info.range = Item.Range == 0 ? VK_WHOLE_SIZE : Item.Range;
            }
            BufferInfos.EmplaceBack(Info);
            Write.pBufferInfo = &BufferInfos.Back();
            break;
        }
        case ERHIBindGroupItemType::SampledImage:
        case ERHIBindGroupItemType::StorageImage: {
            if (Item.TextureView == nullptr)
            {
                LE_LOG(LogRHI, Error, "UpdateDescriptorSets failed: texture view item at binding {} is null.", Item.Binding);
                continue;
            }
            FVulkanRHITextureView* VkView = static_cast<FVulkanRHITextureView*>(Item.TextureView);
            VkDescriptorImageInfo Info{};
            {
                Info.imageView = VkView->View;
                Info.imageLayout = (LayoutItem->Type == ERHIBindGroupItemType::StorageImage) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            ImageInfos.EmplaceBack(Info);
            Write.pImageInfo = &ImageInfos.Back();
            break;
        }
        case ERHIBindGroupItemType::Sampler: {
            if (Item.Sampler == nullptr)
            {
                LE_LOG(LogRHI, Error, "UpdateDescriptorSets failed: sampler item at binding {} is null.", Item.Binding);
                continue;
            }
            FVulkanRHISampler* VkSampler = static_cast<FVulkanRHISampler*>(Item.Sampler);
            VkDescriptorImageInfo Info{};
            {
                Info.sampler = VkSampler->Handle;
            }
            ImageInfos.EmplaceBack(Info);
            Write.pImageInfo = &ImageInfos.Back();
            break;
        }
        case ERHIBindGroupItemType::CombinedImageSampler: {
            if (Item.TextureView == nullptr || Item.Sampler == nullptr)
            {
                LE_LOG(LogRHI, Error, "UpdateDescriptorSets failed: combined image sampler item at binding {} is invalid.", Item.Binding);
                continue;
            }
            FVulkanRHITextureView* VkView = static_cast<FVulkanRHITextureView*>(Item.TextureView);
            FVulkanRHISampler* VkSampler = static_cast<FVulkanRHISampler*>(Item.Sampler);
            VkDescriptorImageInfo Info{};
            {
                Info.sampler = VkSampler->Handle;
                Info.imageView = VkView->View;
                Info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            ImageInfos.EmplaceBack(Info);
            Write.pImageInfo = &ImageInfos.Back();
            break;
        }
        }
        Writes.EmplaceBack(Write);
    }

    if (!Writes.IsEmpty())
    {
        vkUpdateDescriptorSets(Device->VkContext.LogicalDevice, static_cast<uint32>(Writes.Size()), Writes.Data(), 0, nullptr);
    }
}

FVulkanRHIBindGroupLayout::FVulkanRHIBindGroupLayout(FVulkanRHIDevice* InDevice, const FRHIBindGroupLayoutDesc& InDesc) : TVulkanResourceBase<FRHIBindGroupLayoutDesc>(InDevice, InDesc)
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRHI, Error, "BindGroupLayout creation failed: invalid device.");
        this->Handle = VK_NULL_HANDLE;
        return;
    }

    LE::Array<VkDescriptorSetLayoutBinding> Bindings;
    Bindings.Reserve(Desc.Bindings.Size());

    for (const FRHIBindGroupLayoutItem& Item : Desc.Bindings)
    {
        VkDescriptorSetLayoutBinding Binding{};
        {
            Binding.binding = Item.Binding;
            Binding.descriptorCount = Item.Count;
            Binding.descriptorType = ToVkDescriptorType(Item.Type);
            Binding.stageFlags = ToVkStageFlags(Item.StageFlags);
        }
        Bindings.PushBack(Binding);
    }

    VkDescriptorSetLayoutCreateInfo Info{};
    {
        Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        Info.bindingCount = static_cast<uint32>(Bindings.Size());
        Info.pBindings = Bindings.Data();
    }
    const VkResult Result = vkCreateDescriptorSetLayout(Device->VkContext.LogicalDevice, &Info, nullptr, &this->Handle);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRHI, Error, "vkCreateDescriptorSetLayout failed. VkResult={}", static_cast<int32>(Result));
        this->Handle = VK_NULL_HANDLE;
    }
}

FVulkanRHIBindGroupLayout::~FVulkanRHIBindGroupLayout()
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        this->Handle = VK_NULL_HANDLE;
        return;
    }

    if (this->Handle != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(Device->VkContext.LogicalDevice, this->Handle, nullptr);
        this->Handle = VK_NULL_HANDLE;
    }
}

FVulkanRHIBindGroup::FVulkanRHIBindGroup(FVulkanRHIDevice* InDevice, const FRHIBindGroupDesc& InDesc) : TVulkanResourceBase<FRHIBindGroupDesc>(InDevice, InDesc)
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE || this->Desc.Layout == nullptr)
    {
        LE_LOG(LogRHI, Error, "BindGroup creation failed: invalid device/layout.");
        this->Pool = VK_NULL_HANDLE;
        this->Set = VK_NULL_HANDLE;
        return;
    }

    FVulkanRHIBindGroupLayout* Layout = static_cast<FVulkanRHIBindGroupLayout*>(Desc.Layout);
    if (Layout->Handle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRHI, Error, "BindGroup creation failed: descriptor set layout handle is null.");
        this->Pool = VK_NULL_HANDLE;
        this->Set = VK_NULL_HANDLE;
        return;
    }

    LE::Array<VkDescriptorPoolSize> PoolSizes;
    CollectPoolSize(Layout->GetDesc(), PoolSizes);

    VkDescriptorPoolCreateInfo PoolInfo{};
    {
        PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        PoolInfo.maxSets = 1;
        PoolInfo.poolSizeCount = static_cast<uint32>(PoolSizes.Size());
        PoolInfo.pPoolSizes = PoolSizes.Data();
    }
    VkResult Result = vkCreateDescriptorPool(Device->VkContext.LogicalDevice, &PoolInfo, nullptr, &this->Pool);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRHI, Error, "vkCreateDescriptorPool failed. VkResult={}", static_cast<int32>(Result));
        this->Pool = VK_NULL_HANDLE;
        this->Set = VK_NULL_HANDLE;
        return;
    }

    VkDescriptorSetAllocateInfo AllocInfo{};
    {
        AllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        AllocInfo.descriptorPool = this->Pool;
        AllocInfo.descriptorSetCount = 1;
        AllocInfo.pSetLayouts = &Layout->Handle;
    }
    Result = vkAllocateDescriptorSets(Device->VkContext.LogicalDevice, &AllocInfo, &this->Set);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRHI, Error, "vkAllocateDescriptorSets failed. VkResult={}", static_cast<int32>(Result));
        vkDestroyDescriptorPool(Device->VkContext.LogicalDevice, this->Pool, nullptr);
        this->Pool = VK_NULL_HANDLE;
        this->Set = VK_NULL_HANDLE;
        return;
    }

    UpdateDescriptorSets(Device, this->Set, this->Desc);
}

FVulkanRHIBindGroup::~FVulkanRHIBindGroup()
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        this->Pool = VK_NULL_HANDLE;
        this->Set = VK_NULL_HANDLE;
        return;
    }

    if (this->Pool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(Device->VkContext.LogicalDevice, this->Pool, nullptr);
        this->Pool = VK_NULL_HANDLE;
        this->Set = VK_NULL_HANDLE; // Set
    }
}

} // namespace LE
