#include "descriptors.hpp"
#include "core/context.hpp"

namespace ida {
// ******************************* IdaDescriptorSetLayout *******************************
IdaDescriptorSetLayout::Builder& IdaDescriptorSetLayout::Builder::AddBinding(
    uint32_t binding,
    vk::DescriptorType descriptorType,
    vk::ShaderStageFlags stageFlags,
    uint32_t descriptorCount) {
    assert(bindings_.find(binding) == bindings_.end() && "Binding already exists");
    auto layoutBinding = vk::DescriptorSetLayoutBinding()
                             .setBinding(binding)
                             .setDescriptorCount(descriptorCount)
                             .setDescriptorType(descriptorType)
                             .setStageFlags(stageFlags);
    bindings_[binding] = layoutBinding;
    return *this;
}

std::unique_ptr<IdaDescriptorSetLayout> IdaDescriptorSetLayout::Builder::Build() const {
    return std::make_unique<IdaDescriptorSetLayout>(bindings_);
}

IdaDescriptorSetLayout::IdaDescriptorSetLayout(std::unordered_map<uint32_t, vk::DescriptorSetLayoutBinding> bindings) : bindings_(bindings) {
    std::vector<vk::DescriptorSetLayoutBinding> layoutBindings;
    for (const auto& [_, binding] : bindings) {
        layoutBindings.push_back(binding);
    }
    vk::DescriptorSetLayoutCreateInfo createInfo;
    createInfo.setBindings(layoutBindings);
    layout_ = Context::GetInstance().device.createDescriptorSetLayout(createInfo);
}

IdaDescriptorSetLayout::~IdaDescriptorSetLayout() {
    Context::GetInstance().device.destroyDescriptorSetLayout(layout_);
}

// ******************************* IdaDescriptorPool *******************************
IdaDescriptorPool::Builder& IdaDescriptorPool::Builder::AddPoolSize(vk::DescriptorType type, uint32_t count) {
    poolSizes_.push_back({type, count});
    return *this;
}

IdaDescriptorPool::Builder& IdaDescriptorPool::Builder::SetPoolFlags(vk::DescriptorPoolCreateFlags flags) {
    poolFlags_ = flags;
    return *this;
}

IdaDescriptorPool::Builder& IdaDescriptorPool::Builder::SetMaxSets(uint32_t maxSets) {
    maxSets_ = maxSets;
    return *this;
}

std::unique_ptr<IdaDescriptorPool> IdaDescriptorPool::Builder::Build() const {
    return std::make_unique<IdaDescriptorPool>(maxSets_, poolFlags_, poolSizes_);
}

IdaDescriptorPool::IdaDescriptorPool(
    uint32_t maxSets,
    vk::DescriptorPoolCreateFlags poolFlags,
    const std::vector<vk::DescriptorPoolSize>& poolSizes) {
    auto createInfo = vk::DescriptorPoolCreateInfo()
                          .setMaxSets(maxSets)
                          .setPoolSizes(poolSizes)
                          .setFlags(poolFlags);
    pool_ = Context::GetInstance().device.createDescriptorPool(createInfo);
}

IdaDescriptorPool::~IdaDescriptorPool() {
    Context::GetInstance().device.destroyDescriptorPool(pool_);
}

bool IdaDescriptorPool::AllocateDescriptor(const vk::DescriptorSetLayout descriptorSetLayout, vk::DescriptorSet& descriptor) const {
    auto allocInfo = vk::DescriptorSetAllocateInfo()
                         .setDescriptorPool(pool_)
                         .setDescriptorSetCount(1)
                         .setSetLayouts(descriptorSetLayout);
    auto result = Context::GetInstance().device.allocateDescriptorSets(&allocInfo, &descriptor);
    return result == vk::Result::eSuccess;
}

void IdaDescriptorPool::FreeDescriptors(std::vector<vk::DescriptorSet>& descriptors) const {
    Context::GetInstance().device.freeDescriptorSets(pool_, descriptors);
}

void IdaDescriptorPool::ResetPool() {
    Context::GetInstance().device.resetDescriptorPool(pool_);
}
// ******************************* IdaDescriptorWriter *******************************
IdaDescriptorWriter::IdaDescriptorWriter(ida::IdaDescriptorSetLayout& setLayout, ida::IdaDescriptorPool& pool)
    : setLayout_(setLayout), pool_(pool) {}

IdaDescriptorWriter& IdaDescriptorWriter::WriteBuffer(uint32_t binding, vk::DescriptorBufferInfo* bufferInfo) {
    auto& bindingDescription = setLayout_.bindings_[binding];
    auto writeInfo = vk::WriteDescriptorSet()
                         .setDstBinding(binding)
                         .setDescriptorType(bindingDescription.descriptorType)
                         .setPBufferInfo(bufferInfo)
                         .setDescriptorCount(1);
    writes_.push_back(writeInfo);
    return *this;
}

IdaDescriptorWriter& IdaDescriptorWriter::WriteImage(uint32_t binding, vk::DescriptorImageInfo* imageInfo) {
    auto& bindingDescription = setLayout_.bindings_[binding];
    auto writeInfo = vk::WriteDescriptorSet()
                         .setDstBinding(binding)
                         .setDescriptorType(bindingDescription.descriptorType)
                         .setPImageInfo(imageInfo)
                         .setDescriptorCount(1);
    writes_.push_back(writeInfo);
    return *this;
}

bool IdaDescriptorWriter::Build(vk::DescriptorSet& set) {
    auto result = pool_.AllocateDescriptor(setLayout_.GetDescriptorSetLayout(), set);
    if (result) {
        Overwrite(set);
    }
    return result;
}

void IdaDescriptorWriter::Overwrite(vk::DescriptorSet& set) {
    for (auto& write : writes_) {
        write.setDstSet(set);
    }
    Context::GetInstance().device.updateDescriptorSets(writes_, nullptr);
}

} // namespace ida