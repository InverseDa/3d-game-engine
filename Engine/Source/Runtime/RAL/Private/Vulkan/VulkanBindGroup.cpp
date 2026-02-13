#include "CoreMinimal.h"
#include "Vulkan/VulkanRAL.h"

namespace {
VkShaderStageFlags ToVkStageFlags(EShaderStage StageFlags)
{
    VkShaderStageFlags Flags = 0;
    if (EnumHasAnyFlags(StageFlags, EShaderStage::Vertex))   Flags |= VK_SHADER_STAGE_VERTEX_BIT;
    if (EnumHasAnyFlags(StageFlags, EShaderStage::Pixel))    Flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (EnumHasAnyFlags(StageFlags, EShaderStage::Compute))  Flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    if (EnumHasAnyFlags(StageFlags, EShaderStage::Geometry)) Flags |= VK_SHADER_STAGE_GEOMETRY_BIT;
    if (EnumHasAnyFlags(StageFlags, EShaderStage::Hull))     Flags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    if (EnumHasAnyFlags(StageFlags, EShaderStage::Domain))   Flags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;

    assert(Flags != 0);
    return Flags;
}

VkDescriptorType ToVkDescriptorType(ERALBindGroupItemType Type)
{
    switch (Type)
    {
        case ERALBindGroupItemType::UniformBuffer:        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case ERALBindGroupItemType::StorageBuffer:        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case ERALBindGroupItemType::SampledImage:         return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case ERALBindGroupItemType::StorageImage:         return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case ERALBindGroupItemType::Sampler:              return VK_DESCRIPTOR_TYPE_SAMPLER;
        case ERALBindGroupItemType::CombinedImageSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        default:                                          return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
}

void CollectPoolSize(const FRALBindGroupLayoutDesc& Desc, std::vector<VkDescriptorPoolSize>& OutPoolSizes)
{
    OutPoolSizes.clear();

    for (const FRALBindGroupLayoutItem& Item : Desc.Bindings)
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
            OutPoolSizes.push_back(Size);
        }
    }
}

const FRALBindGroupLayoutItem* FindLayoutItem(const FRALBindGroupLayoutDesc& LayoutDesc, uint32 Binding)
{
    for (const auto& Item : LayoutDesc.Bindings)
    {
        if (Item.Binding == Binding) return &Item;
    }
    return nullptr;
}
}

static void UpdateDescriptorSets(FVulkanRALDevice* Device, VkDescriptorSet Set, const FRALBindGroupDesc& Desc)
{
    const FRALBindGroupLayoutDesc& LayoutDesc = Desc.Layout->GetDesc();

    std::vector<VkWriteDescriptorSet> Writes;
    std::vector<VkDescriptorBufferInfo> BufferInfos;
    std::vector<VkDescriptorImageInfo> ImageInfos;

    Writes.reserve(Desc.Items.size());
    BufferInfos.reserve(Desc.Items.size());
    ImageInfos.reserve(Desc.Items.size());

    for (const FRALBindGroupItem& Item : Desc.Items)
    {
        const FRALBindGroupLayoutItem* LayoutItem = FindLayoutItem(LayoutDesc, Item.Binding);
        if (LayoutItem == nullptr)
        {
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
            case ERALBindGroupItemType::UniformBuffer:
            case ERALBindGroupItemType::StorageBuffer:
            {
                FVulkanRALBuffer* VkBuffer = static_cast<FVulkanRALBuffer*>(Item.Buffer);
                VkDescriptorBufferInfo Info{};
                {
                    Info.buffer = VkBuffer->Buffer;
                    Info.offset = Item.Offset;
                    Info.range = Item.Range == 0 ? VK_WHOLE_SIZE : Item.Range;
                }
                BufferInfos.emplace_back(Info);
                Write.pBufferInfo = &BufferInfos.back();
                break;
            }
            case ERALBindGroupItemType::SampledImage:
            case ERALBindGroupItemType::StorageImage:
            {
                FVulkanRALTextureView* VkView = static_cast<FVulkanRALTextureView*>(Item.TextureView);
                VkDescriptorImageInfo Info{};
                {
                    Info.imageView = VkView->View;
                    Info.imageLayout = (LayoutItem->Type == ERALBindGroupItemType::StorageImage) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }
                ImageInfos.emplace_back(Info);
                Write.pImageInfo = &ImageInfos.back();
                break;
            }
            case ERALBindGroupItemType::Sampler:
            {
                FVulkanRALSampler* VkSampler = static_cast<FVulkanRALSampler*>(Item.Sampler);
                VkDescriptorImageInfo Info{};
                {
                    Info.sampler = VkSampler->Handle;
                }
                ImageInfos.emplace_back(Info);
                Write.pImageInfo = &ImageInfos.back();
                break;
            }
            case ERALBindGroupItemType::CombinedImageSampler:
            {
                FVulkanRALTextureView* VkView = static_cast<FVulkanRALTextureView*>(Item.TextureView);
                FVulkanRALSampler* VkSampler = static_cast<FVulkanRALSampler*>(Item.Sampler);
                VkDescriptorImageInfo Info{};
                {
                    Info.sampler = VkSampler->Handle;
                    Info.imageView = VkView->View;
                    Info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }
                ImageInfos.emplace_back(Info);
                Write.pImageInfo = &ImageInfos.back();
                break;
            }
        }
        Writes.emplace_back(Write);
    }

    if (Writes.empty() != true)
    {
        vkUpdateDescriptorSets(Device->VkContext.LogicalDevice, static_cast<uint32>(Writes.size()), Writes.data(), 0, nullptr);
    }
}

FVulkanRALBindGroupLayout::FVulkanRALBindGroupLayout(FVulkanRALDevice* InDevice, const FRALBindGroupLayoutDesc& InDesc)
    : TVulkanResourceBase<FRALBindGroupLayoutDesc>(InDevice, InDesc)
{
    std::vector<VkDescriptorSetLayoutBinding> Bindings{};
    Bindings.reserve(Desc.Bindings.size());

    for (const FRALBindGroupLayoutItem& Item : Desc.Bindings)
    {
        VkDescriptorSetLayoutBinding Binding{};
        {
            Binding.binding = Item.Binding;
            Binding.descriptorCount = Item.Count;
            Binding.descriptorType = ToVkDescriptorType(Item.Type);
            Binding.stageFlags = ToVkStageFlags(Item.StageFlags);
        }
        Bindings.push_back(Binding);
    }

    VkDescriptorSetLayoutCreateInfo Info{};
    {
        Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        Info.bindingCount = static_cast<uint32>(Bindings.size());
        Info.pBindings = Bindings.data();
    }
    vkCreateDescriptorSetLayout(Device->VkContext.LogicalDevice, &Info, nullptr, &this->Handle);
}

FVulkanRALBindGroupLayout::~FVulkanRALBindGroupLayout()
{
    if (this->Handle != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(Device->VkContext.LogicalDevice, this->Handle, nullptr);
        this->Handle = VK_NULL_HANDLE;
    }
}

FVulkanRALBindGroup::FVulkanRALBindGroup(FVulkanRALDevice* InDevice, const FRALBindGroupDesc& InDesc)
    : TVulkanResourceBase<FRALBindGroupDesc>(InDevice, InDesc)
{
    FVulkanRALBindGroupLayout* Layout = static_cast<FVulkanRALBindGroupLayout*>(Desc.Layout);

    std::vector<VkDescriptorPoolSize> PoolSizes{};
    CollectPoolSize(Layout->GetDesc(), PoolSizes);

    VkDescriptorPoolCreateInfo PoolInfo{};
    {
        PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        PoolInfo.maxSets = 1;
        PoolInfo.poolSizeCount = static_cast<uint32>(PoolSizes.size());
        PoolInfo.pPoolSizes = PoolSizes.data();
    }
    vkCreateDescriptorPool(Device->VkContext.LogicalDevice, &PoolInfo, nullptr, &this->Pool);

    VkDescriptorSetAllocateInfo AllocInfo{};
    {
        AllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        AllocInfo.descriptorPool = this->Pool;
        AllocInfo.descriptorSetCount = 1;
        AllocInfo.pSetLayouts = &Layout->Handle;
    }
    vkAllocateDescriptorSets(Device->VkContext.LogicalDevice, &AllocInfo, &this->Set);

    UpdateDescriptorSets(Device, this->Set, this->Desc);
}

FVulkanRALBindGroup::~FVulkanRALBindGroup()
{
    if (this->Pool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(Device->VkContext.LogicalDevice, this->Pool, nullptr);
        this->Pool = VK_NULL_HANDLE;
        this->Set = VK_NULL_HANDLE; // Set
    }
}
