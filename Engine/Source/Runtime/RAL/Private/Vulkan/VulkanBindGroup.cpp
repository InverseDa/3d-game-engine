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

    if (Flags == 0)
    {
        LE_LOG(LogRAL, Error, "Invalid shader stage flags for bind group layout.");
    }
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
    if (Device == nullptr || Device->VkContext.LogicalDevice == VK_NULL_HANDLE || Desc.Layout == nullptr)
    {
        LE_LOG(LogRAL, Error, "UpdateDescriptorSets failed: invalid device/layout.");
        return;
    }

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
            LE_LOG(LogRAL, Warn, "UpdateDescriptorSets: binding {} not found in layout.", Item.Binding);
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
                if (Item.Buffer == nullptr)
                {
                    LE_LOG(LogRAL, Error, "UpdateDescriptorSets failed: buffer item at binding {} is null.", Item.Binding);
                    continue;
                }
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
                if (Item.TextureView == nullptr)
                {
                    LE_LOG(LogRAL, Error, "UpdateDescriptorSets failed: texture view item at binding {} is null.", Item.Binding);
                    continue;
                }
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
                if (Item.Sampler == nullptr)
                {
                    LE_LOG(LogRAL, Error, "UpdateDescriptorSets failed: sampler item at binding {} is null.", Item.Binding);
                    continue;
                }
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
                if (Item.TextureView == nullptr || Item.Sampler == nullptr)
                {
                    LE_LOG(LogRAL, Error, "UpdateDescriptorSets failed: combined image sampler item at binding {} is invalid.", Item.Binding);
                    continue;
                }
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
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "BindGroupLayout creation failed: invalid device.");
        this->Handle = VK_NULL_HANDLE;
        return;
    }

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
    const VkResult Result = vkCreateDescriptorSetLayout(Device->VkContext.LogicalDevice, &Info, nullptr, &this->Handle);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkCreateDescriptorSetLayout failed. VkResult={}", static_cast<int32>(Result));
        this->Handle = VK_NULL_HANDLE;
    }
}

FVulkanRALBindGroupLayout::~FVulkanRALBindGroupLayout()
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

FVulkanRALBindGroup::FVulkanRALBindGroup(FVulkanRALDevice* InDevice, const FRALBindGroupDesc& InDesc)
    : TVulkanResourceBase<FRALBindGroupDesc>(InDevice, InDesc)
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE || this->Desc.Layout == nullptr)
    {
        LE_LOG(LogRAL, Error, "BindGroup creation failed: invalid device/layout.");
        this->Pool = VK_NULL_HANDLE;
        this->Set = VK_NULL_HANDLE;
        return;
    }

    FVulkanRALBindGroupLayout* Layout = static_cast<FVulkanRALBindGroupLayout*>(Desc.Layout);
    if (Layout->Handle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "BindGroup creation failed: descriptor set layout handle is null.");
        this->Pool = VK_NULL_HANDLE;
        this->Set = VK_NULL_HANDLE;
        return;
    }

    std::vector<VkDescriptorPoolSize> PoolSizes{};
    CollectPoolSize(Layout->GetDesc(), PoolSizes);

    VkDescriptorPoolCreateInfo PoolInfo{};
    {
        PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        PoolInfo.maxSets = 1;
        PoolInfo.poolSizeCount = static_cast<uint32>(PoolSizes.size());
        PoolInfo.pPoolSizes = PoolSizes.data();
    }
    VkResult Result = vkCreateDescriptorPool(Device->VkContext.LogicalDevice, &PoolInfo, nullptr, &this->Pool);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkCreateDescriptorPool failed. VkResult={}", static_cast<int32>(Result));
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
        LE_LOG(LogRAL, Error, "vkAllocateDescriptorSets failed. VkResult={}", static_cast<int32>(Result));
        vkDestroyDescriptorPool(Device->VkContext.LogicalDevice, this->Pool, nullptr);
        this->Pool = VK_NULL_HANDLE;
        this->Set = VK_NULL_HANDLE;
        return;
    }

    UpdateDescriptorSets(Device, this->Set, this->Desc);
}

FVulkanRALBindGroup::~FVulkanRALBindGroup()
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
