#include "buffer.hpp"
#include "core/context.hpp"

namespace ida {
/**
 * Returns the minimum instance size required to be compatible with devices minOffsetAlignment
 *
 * @param instanceSize The size of an instance
 * @param minOffsetAlignment The minimum required alignment, in bytes, for the offset member (eg
 * minUniformBufferOffsetAlignment)
 *
 * @return vk::Result of the buffer mapping call
 */
vk::DeviceSize IdaBuffer::GetAlignment(vk::DeviceSize instanceSize, vk::DeviceSize minOffsetAlignment) {
    return minOffsetAlignment > 0 ? (instanceSize + minOffsetAlignment - 1) & ~(minOffsetAlignment - 1) : instanceSize;
}

void IdaBuffer::Utils::CreateBuffer(
    vk::DeviceSize size,
    vk::BufferUsageFlags usage,
    vk::MemoryPropertyFlags properties,
    vk::Buffer& buffer,
    vk::DeviceMemory& bufferMemory) {
    auto& device = Context::GetInstance().device;
    auto createInfo = vk::BufferCreateInfo()
                          .setSize(size)
                          .setUsage(usage)
                          .setSharingMode(vk::SharingMode::eExclusive);
    buffer = device.createBuffer(createInfo);

    auto memRequirements = Context::GetInstance().device.getBufferMemoryRequirements(buffer);
    auto allocInfo = vk::MemoryAllocateInfo()
                         .setAllocationSize(memRequirements.size)
                         .setMemoryTypeIndex(FindMemoryType(memRequirements.memoryTypeBits, properties));
    bufferMemory = device.allocateMemory(allocInfo);
    device.bindBufferMemory(buffer, bufferMemory, 0);
}

uint32_t IdaBuffer::Utils::FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
    auto memProperties = Context::GetInstance().phyDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    IO::ThrowError("Failed to find suitable memory type!");
}

void IdaBuffer::Utils::CopyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size) {
    auto& ctx = Context::GetInstance();
    ctx.ExecuteCommandBuffer(Context::GetInstance().graphicsQueue, [&](vk::CommandBuffer cmdBuf) {
        auto copyRegion = vk::BufferCopy()
                              .setSize(size);
        cmdBuf.copyBuffer(srcBuffer, dstBuffer, copyRegion);
    });
}

void IdaBuffer::Utils::CopyBufferToImage(vk::Buffer buffer, vk::Image image, uint32_t width, uint32_t height, uint32_t layerCount) {
    auto& ctx = Context::GetInstance();
    ctx.ExecuteCommandBuffer(Context::GetInstance().graphicsQueue, [&](vk::CommandBuffer cmdBuf) {
        auto region = vk::BufferImageCopy()
                          .setBufferOffset(0)
                          .setBufferRowLength(0)
                          .setBufferImageHeight(0)
                          .setImageSubresource(vk::ImageSubresourceLayers()
                                                   .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                   .setMipLevel(0)
                                                   .setBaseArrayLayer(0)
                                                   .setLayerCount(layerCount))
                          .setImageExtent(vk::Extent3D()
                                              .setWidth(width)
                                              .setHeight(height)
                                              .setDepth(1));
        cmdBuf.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, region);
    });
}

// **************************************** IdaBuffer ****************************************
IdaBuffer::IdaBuffer(
    BufferType type,
    vk::DeviceSize instanceSize,
    uint32_t instanceCount,
    vk::BufferUsageFlags usageFlags,
    vk::MemoryPropertyFlags properties,
    vk::DeviceSize minOffsetAlignment)
    : type_(type), instanceSize_(instanceSize), instanceCount_(instanceCount), usageFlags_(usageFlags), memoryFlags_(properties) {
    alignmentSize_ = GetAlignment(instanceSize, minOffsetAlignment);
    bufferSize_ = alignmentSize_ * instanceCount;
    Utils::CreateBuffer(bufferSize_, usageFlags_, memoryFlags_, buffer_, bufferMemory_);
}

IdaBuffer::~IdaBuffer() {
    IO::PrintLog(LOG_LEVEL::LOG_LEVEL_INFO, "Buffer destroyed, Type: {}", GetTypeName());
    auto& device = Context::GetInstance().device;
    Unmap();
    device.destroyBuffer(buffer_);
    device.freeMemory(bufferMemory_);
}

void IdaBuffer::Map(vk::DeviceSize size, vk::DeviceSize offset) {
    auto& device = Context::GetInstance().device;
    mapped_ = device.mapMemory(bufferMemory_, offset, size, vk::MemoryMapFlags());
}

void IdaBuffer::Unmap() {
    auto& device = Context::GetInstance().device;
    if (mapped_) {
        device.unmapMemory(bufferMemory_);
        mapped_ = nullptr;
    }
}

void IdaBuffer::WriteToBuffer(void* data, vk::DeviceSize size, vk::DeviceSize offset) {
    if (size = vk::WholeSize) {
        memcpy(mapped_, data, bufferSize_);
    } else {
        char* dst = static_cast<char*>(mapped_) + offset;
        memcpy(dst, data, size);
    }
}

void IdaBuffer::Flush(vk::DeviceSize size, vk::DeviceSize offset) {
    auto& device = Context::GetInstance().device;
    auto range = vk::MappedMemoryRange()
                     .setMemory(bufferMemory_)
                     .setSize(size)
                     .setOffset(offset);
    device.flushMappedMemoryRanges(range);
}

vk::DescriptorBufferInfo IdaBuffer::GetDescriptorInfo(vk::DeviceSize size, vk::DeviceSize offset) {
    return vk::DescriptorBufferInfo()
        .setBuffer(buffer_)
        .setOffset(offset)
        .setRange(size);
}

void IdaBuffer::Invalidate(vk::DeviceSize size, vk::DeviceSize offset) {
    auto& device = Context::GetInstance().device;
    auto range = vk::MappedMemoryRange()
                     .setMemory(bufferMemory_)
                     .setSize(size)
                     .setOffset(offset);
    device.invalidateMappedMemoryRanges(range);
}

void IdaBuffer::WriteToIndex(void* data, int index) {
    WriteToBuffer(data, instanceSize_, index * alignmentSize_);
}

void IdaBuffer::FlushIndex(int index) {
    Flush(alignmentSize_, index * alignmentSize_);
}

vk::DescriptorBufferInfo IdaBuffer::GetDescriptorInfo(int index) {
    return GetDescriptorInfo(alignmentSize_, index * alignmentSize_);
}

void IdaBuffer::InvalidateIndex(int index) {
    Invalidate(alignmentSize_, index * alignmentSize_);
}

} // namespace ida
