#include "simple_render_system.hpp"
#include "core/context.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/glm.hpp"
#include "glm/gtc/constants.hpp"

#include <array>
#include <cassert>
#include <stdexcept>

namespace ida {
struct SimplePushConstantData {
    glm::mat4 modelMatrix{1.f};
    glm::mat4 normalMatrix{1.f};
};

SimpleRenderSystem::SimpleRenderSystem(vk::RenderPass renderPass, vk::DescriptorSetLayout globalSetLayout) {
    CreatePipelineLayout(globalSetLayout);
    CreatePipeline(renderPass);
}

SimpleRenderSystem::~SimpleRenderSystem() {
    Context::GetInstance().device.destroyPipelineLayout(pipelineLayout_);
}

void SimpleRenderSystem::CreatePipelineLayout(vk::DescriptorSetLayout globalSetLayout) {
    auto pushConstantRange = vk::PushConstantRange()
                                 .setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
                                 .setOffset(0)
                                 .setSize(sizeof(SimplePushConstantData));
    std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = {globalSetLayout};

    auto pipelineLayoutCreateInfo = vk::PipelineLayoutCreateInfo()
                                        .setSetLayouts(descriptorSetLayouts)
                                        .setPushConstantRanges(pushConstantRange);
    pipelineLayout_ = Context::GetInstance().device.createPipelineLayout(pipelineLayoutCreateInfo);
}

void SimpleRenderSystem::CreatePipeline(vk::RenderPass renderPass) {
    PipelineConfigInfo pipelineConfig{};
    IdaPipeline::DefaultPipelineConfigInfo(pipelineConfig);
    pipelineConfig.renderPass = renderPass;
    pipelineConfig.pipelineLayout = pipelineLayout_;
    pipeline_ = std::make_unique<IdaPipeline>(ReadWholeFile("shaders/simple_shader.vert.spv"),
                                                 ReadWholeFile("shaders/simple_shader.frag.spv"),
                                                 pipelineConfig);
}

void SimpleRenderSystem::RenderGameObjects(FrameInfo& frameInfo) {
    auto& cmd = frameInfo.commandBuffer;

    pipeline_->Bind(cmd);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                           pipelineLayout_,
                           0,
                           frameInfo.globalDescriptorSet,
                           nullptr);
    for (auto& gameObject : frameInfo.gameObjects) {
        auto& obj = gameObject.second;
        if(obj.model == nullptr) {
            continue;
        }
        SimplePushConstantData push{};
        push.modelMatrix = obj.transform.mat4();
        push.normalMatrix = glm::transpose(glm::inverse(push.modelMatrix));
        cmd.pushConstants<SimplePushConstantData>(pipelineLayout_,
                                                  vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                                  0,
                                                  push);
        obj.model->Bind(cmd);
        obj.model->Draw(cmd);
    }
}

} // namespace ida