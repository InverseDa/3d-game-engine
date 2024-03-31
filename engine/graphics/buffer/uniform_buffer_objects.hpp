#ifndef ENGINE_UNIFORM_BUFFER_OBJECTS_HPP
#define ENGINE_UNIFORM_BUFFER_OBJECTS_HPP

#include "buffer.hpp"
#include "graphics/swapchain/swapchain.hpp"
#include "graphics/descriptor/descriptors.hpp"

#include <vector>

namespace ida {
template <typename UboType, vk::DescriptorType DescriptorType>
class UniformBufferObject {
  public:
    explicit UniformBufferObject(std::unique_ptr<ida::IdaDescriptorPool>& descriptorPool) {
        uboBuffers_.resize(IdaSwapChain::MAX_FRAMES_IN_FLIGHT);
        globalDescriptorSets_.resize(IdaSwapChain::MAX_FRAMES_IN_FLIGHT);
        globalSetLayout_ = IdaDescriptorSetLayout::Builder()
                               .AddBinding(0, DescriptorType, vk::ShaderStageFlagBits::eAllGraphics)
                               .Build();

        for (int i = 0; i < uboBuffers_.size(); i++) {
            uboBuffers_[i] = std::make_unique<ida::IdaBuffer>(
                sizeof(UboType),
                1,
                vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible);
            uboBuffers_[i]->Map();
        }

        for (int i = 0; i < globalDescriptorSets_.size(); i++) {
            auto bufferInfo = uboBuffers_[i]->GetDescriptorInfo();
            ida::IdaDescriptorWriter(*globalSetLayout_, *descriptorPool)
                .WriteBuffer(0, &bufferInfo)
                .Build(globalDescriptorSets_[i]);
        }
    };
    ~UniformBufferObject() = default;
    UniformBufferObject(const UniformBufferObject&) = delete;
    UniformBufferObject& operator=(const UniformBufferObject&) = delete;

    void Update(int frameIndex, UboType& data) {
        uboBuffers_[frameIndex]->WriteToBuffer(&data);
        uboBuffers_[frameIndex]->Flush();
    }

    vk::DescriptorSet GetDescriptorSet(int frameIndex) {
        return globalDescriptorSets_[frameIndex];
    }

    vk::DescriptorSetLayout GetDescriptorSetLayout() {
        return globalSetLayout_->GetDescriptorSetLayout();
    }

  private:
    std::unique_ptr<IdaDescriptorSetLayout> globalSetLayout_;

    std::vector<std::unique_ptr<IdaBuffer>> uboBuffers_;
    std::vector<vk::DescriptorSet> globalDescriptorSets_;
};

/** 用可变参数列表的方式来实现获取UniformPackageData结构体，比如：
 ** struct OCTreeRenderUniformPackage {
 **    glm::mat4 projection{1.f};
 **    glm::mat4 view{1.f};
 **    glm::mat4 inverseView{1.f};
 ** } data;
 ** 比如调用UniformPackage<glm::mat4, glm::mat4> data;
 ** auto data = data.GetStructData("model", "view")就会返回一个结构体变量，包含两个glm::mat4的成员变量model和view
 ** struct EG {
 **     glm::mat4 model;
 **     glm::mat4 view;
 ** } data;
 **/
} // namespace ida

#endif // ENGINE_UNIFORM_BUFFER_OBJECTS_HPP
