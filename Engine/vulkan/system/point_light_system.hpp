#ifndef VULKAN_LIB_POINT_LIGHT_SYSTEM_HPP
#define VULKAN_LIB_POINT_LIGHT_SYSTEM_HPP

#include "vulkan/vulkan.hpp"

#include "global_info.hpp"
#include "render/pipeline.hpp"

namespace ida {
class PointLightSystem {
  public:
    PointLightSystem(vk::RenderPass renderPass, vk::DescriptorSetLayout globalSetLayout);
    ~PointLightSystem();
    PointLightSystem(const PointLightSystem&) = delete;
    PointLightSystem& operator=(const PointLightSystem&) = delete;

    void Update(FrameInfo& frameInfo, GlobalUbo& globalUbo);
    void Render(FrameInfo& frameInfo);

  private:
    void CreatePipelineLayout(vk::DescriptorSetLayout globalSetLayout);
    void CreatePipeline(vk::RenderPass renderPass);

    std::unique_ptr<IdaPipeline> pipeline_;
    vk::PipelineLayout pipelineLayout_;
};
} // namespace ida

#endif // VULKAN_LIB_POINT_LIGHT_SYSTEM_HPP
