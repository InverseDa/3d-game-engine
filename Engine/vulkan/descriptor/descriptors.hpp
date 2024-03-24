#ifndef VULKAN_LIB_DESCRIPTORS_HPP
#define VULKAN_LIB_DESCRIPTORS_HPP

#include "vulkan/vulkan.hpp"
#include <vector>
#include <unordered_map>

namespace ida {
class IdaDescriptorSetLayout {
  public:
    class Builder {
      public:
        Builder() = default;
        Builder& AddBinding(
            uint32_t binding,
            vk::DescriptorType descriptorType,
            vk::ShaderStageFlags stageFlags,
            uint32_t descriptorCount = 1);
        std::unique_ptr<IdaDescriptorSetLayout> Build() const;

      private:
        std::unordered_map<uint32_t, vk::DescriptorSetLayoutBinding> bindings_;
    };
    IdaDescriptorSetLayout(std::unordered_map<uint32_t, vk::DescriptorSetLayoutBinding> bindings);
    ~IdaDescriptorSetLayout();
    IdaDescriptorSetLayout(const IdaDescriptorSetLayout&) = delete;
    IdaDescriptorSetLayout& operator=(const IdaDescriptorSetLayout&) = delete;

    vk::DescriptorSetLayout GetDescriptorSetLayout() const { return layout_; }

  private:
    vk::DescriptorSetLayout layout_;
    std::unordered_map<uint32_t, vk::DescriptorSetLayoutBinding> bindings_;

    friend class IdaDescriptorWriter;
};

class IdaDescriptorPool {
  public:
    class Builder {
      public:
        Builder() = default;
        Builder& AddPoolSize(vk::DescriptorType type, uint32_t count);
        Builder& SetPoolFlags(vk::DescriptorPoolCreateFlags flags);
        Builder& SetMaxSets(uint32_t maxSets);
        std::unique_ptr<IdaDescriptorPool> Build() const;

      private:
        std::vector<vk::DescriptorPoolSize> poolSizes_;
        uint32_t maxSets_ = 1000;
        vk::DescriptorPoolCreateFlags poolFlags_;
    };
    IdaDescriptorPool(
        uint32_t maxSets,
        vk::DescriptorPoolCreateFlags poolFlags,
        const std::vector<vk::DescriptorPoolSize>& poolSizes);
    ~IdaDescriptorPool();
    IdaDescriptorPool(const IdaDescriptorPool&) = delete;
    IdaDescriptorPool& operator=(const IdaDescriptorPool&) = delete;

    bool AllocateDescriptor(
        const vk::DescriptorSetLayout descriptorSetLayout,
        vk::DescriptorSet& descriptor) const;
    void FreeDescriptors(std::vector<vk::DescriptorSet>& descriptors) const;
    void ResetPool();

  private:
    vk::DescriptorPool pool_;
    friend class IdaDescriptorWriter;
};

class IdaDescriptorWriter {
  public:
    IdaDescriptorWriter(IdaDescriptorSetLayout& setLayout, IdaDescriptorPool& pool);

    IdaDescriptorWriter& WriteBuffer(
        uint32_t binding,
        vk::DescriptorBufferInfo* bufferInfo);
    IdaDescriptorWriter& WriteImage(
        uint32_t binding,
        vk::DescriptorImageInfo* imageInfo);

    bool Build(vk::DescriptorSet &set);
    void Overwrite(vk::DescriptorSet &set);
  private:
    IdaDescriptorSetLayout& setLayout_;
    IdaDescriptorPool& pool_;
    std::vector<vk::WriteDescriptorSet> writes_;
};

} // namespace ida

#endif // VULKAN_LIB_DESCRIPTORS_HPP
