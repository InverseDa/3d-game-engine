#ifndef VULKAN_LIB_BUFFER_HPP
#define VULKAN_LIB_BUFFER_HPP

#include "vulkan/vulkan.hpp"

#include <unordered_map>

namespace ida {

enum BufferType {
    VertexBuffer,
    IndexBuffer,
    UniformBuffer,
    StagingBuffer,
};

inline std::unordered_map<BufferType, std::string> BufferTypeNames = {
    {VertexBuffer, "VertexBuffer"},
    {IndexBuffer, "IndexBuffer"},
    {UniformBuffer, "UniformBuffer"},
    {StagingBuffer, "StagingBuffer"},
};

class IdaBuffer {
  public:
    struct Utils {
        static void CreateBuffer(
            vk::DeviceSize size,
            vk::BufferUsageFlags usage,
            vk::MemoryPropertyFlags properties,
            vk::Buffer& buffer,
            vk::DeviceMemory& bufferMemory);
        static uint32_t FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);
        static void CopyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size);
        static void CopyBufferToImage(vk::Buffer buffer, vk::Image image, uint32_t width, uint32_t height, uint32_t layerCount);
    };

    IdaBuffer(
        BufferType type,
        vk::DeviceSize instanceSize,
        uint32_t instanceCount,
        vk::BufferUsageFlags usage,
        vk::MemoryPropertyFlags properties,
        vk::DeviceSize minOffsetAlignment = 1);
    ~IdaBuffer();
    IdaBuffer(const IdaBuffer&) = delete;
    IdaBuffer& operator=(const IdaBuffer&) = delete;

    void Map(vk::DeviceSize size = vk::WholeSize, vk::DeviceSize offset = 0);
    void Unmap();

    void WriteToBuffer(void* data, vk::DeviceSize size = vk::WholeSize, vk::DeviceSize offset = 0);
    void Flush(vk::DeviceSize size = vk::WholeSize, vk::DeviceSize offset = 0);
    vk::DescriptorBufferInfo GetDescriptorInfo(vk::DeviceSize size = vk::WholeSize, vk::DeviceSize offset = 0);
    void Invalidate(vk::DeviceSize size = vk::WholeSize, vk::DeviceSize offset = 0);

    void WriteToIndex(void* data, int index);
    void FlushIndex(int index);
    vk::DescriptorBufferInfo GetDescriptorInfo(int index);
    void InvalidateIndex(int index);

    vk::Buffer GetBuffer() { return buffer_; }
    vk::DeviceMemory GetBufferMemory() { return bufferMemory_; }
    void* GetMappedMemory() { return mapped_; }
    vk::DeviceSize GetBufferSize() { return bufferSize_; }
    vk::BufferUsageFlags GetUsageFlags() { return usageFlags_; }
    vk::MemoryPropertyFlags GetMemoryPropertyFlags() { return memoryFlags_; }
    BufferType GetType() { return type_; }
    std::string GetTypeName() { return BufferTypeNames[type_]; }

  private:
    static vk::DeviceSize GetAlignment(vk::DeviceSize instanceSize, vk::DeviceSize minOffsetAlignment);

    BufferType type_;

    vk::BufferUsageFlags usageFlags_;
    vk::MemoryPropertyFlags memoryFlags_;

    vk::Buffer buffer_;
    vk::DeviceMemory bufferMemory_;

    vk::DeviceSize bufferSize_;
    vk::DeviceSize instanceSize_;
    vk::DeviceSize alignmentSize_;

    uint32_t instanceCount_;

    void* mapped_ = nullptr;
};

} // namespace ida

#endif // VULKAN_LIB_BUFFER_HPP
