#include "oc_tree_render_system.hpp"
#include "../graphics/core/context.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/glm.hpp"
#include "glm/gtc/constants.hpp"

#include <array>
#include <cassert>
#include <stdexcept>

namespace ida {
OCTreeRenderSystem::OCTreeRenderSystem(vk::RenderPass renderPass, vk::DescriptorSetLayout globalSetLayout) {
    CreatePipelineLayout(globalSetLayout);
    CreatePipeline(renderPass);
}

OCTreeRenderSystem::~OCTreeRenderSystem() {
    Context::GetInstance().device.destroyPipelineLayout(pipelineLayout_);
}

void OCTreeRenderSystem::RenderGameObjects(FrameInfo& frameInfo) {
    auto& cmd = frameInfo.commandBuffer;
    pipeline_->Bind(cmd);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                           pipelineLayout_,
                           0,
                           frameInfo.globalDescriptorSet,
                           nullptr);
}

void OCTreeRenderSystem::CreatePipelineLayout(vk::DescriptorSetLayout globalSetLayout) {
    auto& device = Context::GetInstance().device;
    auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo()
                                  .setSetLayoutCount(1)
                                  .setPSetLayouts(&globalSetLayout);
    pipelineLayout_ = device.createPipelineLayout(pipelineLayoutInfo);
}

void OCTreeRenderSystem::CreatePipeline(vk::RenderPass renderPass) {
    PipelineConfigInfo pipelineConfig{};
    IdaPipeline::DefaultPipelineConfigInfo(pipelineConfig);
    pipelineConfig.renderPass = renderPass;
    pipelineConfig.pipelineLayout = pipelineLayout_;
    pipeline_ = std::make_unique<IdaPipeline>(ReadWholeFile("shaders/simple_shader.vert.spv"),
                                              ReadWholeFile("shaders/simple_shader.frag.spv"),
                                              pipelineConfig);
}

} // namespace ida