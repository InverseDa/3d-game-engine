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
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE || this->Device->VkContext.PhysicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "Buffer creation failed: invalid Vulkan device context.");
        this->Buffer = VK_NULL_HANDLE;
        this->Memory = VK_NULL_HANDLE;
        return;
    }

    VkBufferCreateInfo BufferInfo = {};
    {
        BufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        BufferInfo.size = InDesc.Size;
        BufferInfo.usage = ToVkBufferUsage(InDesc.Usage);
        BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // TODO: kodak
    }
    VkResult Result = vkCreateBuffer(Device->VkContext.LogicalDevice, &BufferInfo, nullptr, &this->Buffer);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkCreateBuffer failed. Size={}, Usage={}, VkResult={}", InDesc.Size, InDesc.Usage, static_cast<int32>(Result));
        this->Buffer = VK_NULL_HANDLE;
        this->Memory = VK_NULL_HANDLE;
        return;
    }

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
    Result = vkAllocateMemory(Device->VkContext.LogicalDevice, &AllocateInfo, nullptr, &this->Memory);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkAllocateMemory(buffer) failed. AllocationSize={}, VkResult={}", AllocateInfo.allocationSize, static_cast<int32>(Result));
        vkDestroyBuffer(this->Device->VkContext.LogicalDevice, this->Buffer, nullptr);
        this->Buffer = VK_NULL_HANDLE;
        this->Memory = VK_NULL_HANDLE;
        return;
    }

    Result = vkBindBufferMemory(Device->VkContext.LogicalDevice, this->Buffer, this->Memory, 0);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkBindBufferMemory failed. VkResult={}", static_cast<int32>(Result));
        vkFreeMemory(this->Device->VkContext.LogicalDevice, this->Memory, nullptr);
        vkDestroyBuffer(this->Device->VkContext.LogicalDevice, this->Buffer, nullptr);
        this->Buffer = VK_NULL_HANDLE;
        this->Memory = VK_NULL_HANDLE;
        return;
    }

    LE_LOG(LogRAL, Info, "Buffer created. Size={}, Usage={}", InDesc.Size, InDesc.Usage);
}

FVulkanRALBuffer::~FVulkanRALBuffer()
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        this->MappedPtr = nullptr;
        this->Buffer = VK_NULL_HANDLE;
        this->Memory = VK_NULL_HANDLE;
        return;
    }

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
    if (this->Memory == VK_NULL_HANDLE || this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "Buffer map failed: invalid memory or device.");
        return nullptr;
    }

    void* Data = nullptr;
    const VkResult Result = vkMapMemory(Device->VkContext.LogicalDevice, Memory, Offset, Size == 0 ? VK_WHOLE_SIZE : Size, 0, &Data);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkMapMemory failed. Offset={}, Size={}, VkResult={}", Offset, Size, static_cast<int32>(Result));
        return nullptr;
    }
    this->MappedPtr = Data;
    return Data;
}

void FVulkanRALBuffer::Unmap()
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Warn, "Buffer unmap skipped: invalid device.");
        this->MappedPtr = nullptr;
        return;
    }

    if (this->MappedPtr)
    {
        vkUnmapMemory(this->Device->VkContext.LogicalDevice, this->Memory);
        this->MappedPtr = nullptr;
    }
}
