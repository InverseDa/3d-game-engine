#ifndef VULKAN_LIB_SIMPLE_RENDER_SYSTEM_HPP
#define VULKAN_LIB_SIMPLE_RENDER_SYSTEM_HPP

#include "vulkan/vulkan.hpp"

#include "global_info.hpp"
#include "render/pipeline.hpp"

namespace ida {
class SimpleRenderSystem {
  public:
    SimpleRenderSystem(vk::RenderPass renderPass, vk::DescriptorSetLayout globalSetLayout);
    ~SimpleRenderSystem();
    SimpleRenderSystem(const SimpleRenderSystem &) = delete;
    SimpleRenderSystem &operator=(const SimpleRenderSystem &) = delete;

    void RenderGameObjects(FrameInfo &frameInfo);

  private:
    void CreatePipelineLayout(vk::DescriptorSetLayout globalSetLayout);
    void CreatePipeline(vk::RenderPass renderPass);

    std::unique_ptr<IdaPipeline> pipeline_;
    vk::PipelineLayout pipelineLayout_;
};
}

#endif // VULKAN_LIB_SIMPLE_RENDER_SYSTEM_HPP
