#include "CoreMinimal.h"
#include "Vulkan/VulkanRAL.h"

namespace
{
    uint32 FindMemoryTypeIndex(VkPhysicalDevice PhysDev, uint32 TypeBits, VkMemoryPropertyFlags Props)
    {
        VkPhysicalDeviceMemoryProperties MemProps;
        vkGetPhysicalDeviceMemoryProperties(PhysDev, &MemProps);

        for (uint32 i = 0; i < MemProps.memoryTypeCount; ++i)
        {
            if ((TypeBits & (1u << i)) && (MemProps.memoryTypes[i].propertyFlags & Props) == Props)
                return i;
        }
        return 0;
    }

    VkBufferUsageFlags ToVkBufferUsage(uint32 UsageFlags)
    {
        VkBufferUsageFlags Flags = 0;
        if (UsageFlags & (uint32)EBufferUsageFlags::VertexBuffer)  Flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        if (UsageFlags & (uint32)EBufferUsageFlags::IndexBuffer)   Flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        if (UsageFlags & (uint32)EBufferUsageFlags::UniformBuffer) Flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if (UsageFlags & (uint32)EBufferUsageFlags::StorageBuffer) Flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if (UsageFlags & (uint32)EBufferUsageFlags::IndirectArgs)  Flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        if (UsageFlags & (uint32)EBufferUsageFlags::TransferSrc)   Flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (UsageFlags & (uint32)EBufferUsageFlags::TransferDst)   Flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        return Flags;
    }

    VkMemoryPropertyFlags ToVkMemoryProps(EResourceUsage Usage)
    {
        switch (Usage)
        {
        case EResourceUsage::Upload:    return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        case EResourceUsage::Readback:  return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        case EResourceUsage::Local:     return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        }
        return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }
}

FVulkanRALBuffer::FVulkanRALBuffer(FVulkanRALDevice* InDevice, const FRALBufferDesc& InDesc)
    : TVulkanResourceBase<FRALBufferDesc>(InDevice, InDesc)
{
    VkBufferCreateInfo BufferInfo = {};
    {
        BufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        BufferInfo.size = InDesc.Size;
        BufferInfo.usage = ToVkBufferUsage(InDesc.Usage);
        BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // TODO: kodak
    }
    vkCreateBuffer(Device->VkContext.LogicalDevice, &BufferInfo, nullptr, &this->Buffer);

    VkMemoryRequirements Requirements;
    {
        vkGetBufferMemoryRequirements(Device->VkContext.LogicalDevice, this->Buffer, &Requirements);
    }

    VkMemoryAllocateInfo AllocateInfo = {};
    {
        AllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        AllocateInfo.allocationSize = Requirements.size;
        AllocateInfo.memoryTypeIndex = FindMemoryTypeIndex(Device->VkContext.PhysicalDevice, Requirements.memoryTypeBits, ToVkMemoryProps(static_cast<EResourceUsage>(Desc.Usage)));
    }
    vkAllocateMemory(Device->VkContext.LogicalDevice, &AllocateInfo, nullptr, &this->Memory);
    vkBindBufferMemory(Device->VkContext.LogicalDevice, this->Buffer, this->Memory, 0);
}

FVulkanRALBuffer::~FVulkanRALBuffer()
{
    if (this->MappedPtr)
    {
        vkUnmapMemory(this->Device->VkContext.LogicalDevice, this->Memory);
    }
    if (this->Buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(this->Device->VkContext.LogicalDevice, this->Buffer, nullptr);
    }
    if (this->Memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(this->Device->VkContext.LogicalDevice, this->Memory, nullptr);
    }
}

void* FVulkanRALBuffer::Map(uint64 Offset, uint64 Size)
{
    void* Data = nullptr;
    vkMapMemory(Device->VkContext.LogicalDevice, Memory, Offset, Size == 0 ? VK_WHOLE_SIZE : Size, 0, &Data);
    this->MappedPtr = Data;
    return Data;
}

void FVulkanRALBuffer::Unmap()
{
    if (this->MappedPtr)
    {
        vkUnmapMemory(this->Device->VkContext.LogicalDevice, this->Memory);
        this->MappedPtr = nullptr;
    }
}
