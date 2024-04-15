#ifndef ENGINE_OC_TREE_RENDER_SYSTEM_HPP
#define ENGINE_OC_TREE_RENDER_SYSTEM_HPP

#include "vulkan/vulkan.hpp"

#include "tool/global_info.hpp"
#include "../graphics/render/pipeline.hpp"

namespace ida {
class OCTreeRenderSystem {
  public:
    OCTreeRenderSystem(vk::RenderPass renderPass, vk::DescriptorSetLayout globalSetLayout);
    ~OCTreeRenderSystem();
    OCTreeRenderSystem(const OCTreeRenderSystem&) = delete;
    OCTreeRenderSystem& operator=(const OCTreeRenderSystem&) = delete;

    void RenderGameObjects(FrameInfo& frameInfo, std::vector<IdaGameObject>& octreeNodes);

  private:
    void CreatePipelineLayout(vk::DescriptorSetLayout globalSetLayout);
    void CreatePipeline(vk::RenderPass renderPass);

    std::unique_ptr<IdaPipeline> pipeline_;
    vk::PipelineLayout pipelineLayout_;
};
} // namespace ida
#endif // ENGINE_OC_TREE_RENDER_SYSTEM_HPP
