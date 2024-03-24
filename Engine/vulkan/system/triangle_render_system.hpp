#ifndef VULKAN_LIB_TRIANGLE_RENDER_SYSTEM_HPP
#define VULKAN_LIB_TRIANGLE_RENDER_SYSTEM_HPP
#include "vulkan/vulkan.hpp"

#include "global_info.hpp"
#include "render/pipeline.hpp"

namespace ida {
class TriangleRenderSystem final{
  public:
    TriangleRenderSystem(vk::RenderPass renderPass, vk::DescriptorSetLayout globalSetLayout);
    ~TriangleRenderSystem();
    TriangleRenderSystem(const TriangleRenderSystem &) = delete;
    TriangleRenderSystem &operator=(const TriangleRenderSystem &) = delete;

    void Render(FrameInfo &frameInfo);

  private:
    void CreatePipelineLayout(vk::DescriptorSetLayout globalSetLayout);
    void CreatePipeline(vk::RenderPass renderPass);

    std::unique_ptr<IdaPipeline> pipeline_;
    vk::PipelineLayout pipelineLayout_;
};
}
#endif // VULKAN_LIB_TRIANGLE_RENDER_SYSTEM_HPP
