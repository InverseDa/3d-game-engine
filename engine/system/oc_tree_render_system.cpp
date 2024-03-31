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
struct OCTreePushData {
    glm::mat4 model{1.f};
};

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
                           frameInfo.descriptorSets["octree"],
                           nullptr);
    for (auto& gameObject : frameInfo.gameObjects) {
        auto& obj = gameObject.second;
        if (obj.model == nullptr) {
            continue;
        }
        OCTreePushData pushData{};
        pushData.model = obj.transform.mat4();
        cmd.pushConstants<OCTreePushData>(pipelineLayout_,
                                          vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                          0,
                                          pushData);
        obj.model->Bind(cmd);
        obj.model->Draw(cmd);
    }
}

void OCTreeRenderSystem::CreatePipelineLayout(vk::DescriptorSetLayout globalSetLayout) {
    auto& device = Context::GetInstance().device;
    auto pushConstantRange = vk::PushConstantRange()
                                 .setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
                                 .setOffset(0)
                                 .setSize(sizeof(OCTreePushData));
    auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo()
                                  .setSetLayoutCount(1)
                                  .setPSetLayouts(&globalSetLayout)
                                  .setPushConstantRangeCount(1)
                                  .setPPushConstantRanges(&pushConstantRange);
    pipelineLayout_ = device.createPipelineLayout(pipelineLayoutInfo);
}

void OCTreeRenderSystem::CreatePipeline(vk::RenderPass renderPass) {
    PipelineConfigInfo pipelineConfig{};
    IdaPipeline::DefaultPipelineConfigInfo(pipelineConfig);
    IdaPipeline::SwitchToLinePolygonMode(pipelineConfig);
    pipelineConfig.renderPass = renderPass;
    pipelineConfig.pipelineLayout = pipelineLayout_;
    pipeline_ = std::make_unique<IdaPipeline>(ReadWholeFile("shaders/oc_render.vert.spv"),
                                              ReadWholeFile("shaders/oc_render.frag.spv"),
                                              pipelineConfig);
}

} // namespace ida