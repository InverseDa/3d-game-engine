#include "triangle_render_system.hpp"
#include "core/context.hpp"

namespace ida {
struct TrianglePushConstantData {
    glm::vec3 color = {1.f, 0.f, 0.f};
};


TriangleRenderSystem::TriangleRenderSystem(vk::RenderPass renderPass, vk::DescriptorSetLayout globalSetLayout) {
    CreatePipelineLayout(globalSetLayout);
    CreatePipeline(renderPass);
}

TriangleRenderSystem::~TriangleRenderSystem() {
    Context::GetInstance().device.destroyPipelineLayout(pipelineLayout_);
}

void TriangleRenderSystem::Render(FrameInfo& frameInfo) {
    auto& cmd = frameInfo.commandBuffer;

    pipeline_->Bind(cmd);
    auto& obj = frameInfo.gameObjects.at(0);
    auto data = TrianglePushConstantData{};
    cmd.pushConstants(pipelineLayout_, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
                      sizeof(TrianglePushConstantData), &data);
    obj.model->Bind(cmd);
    obj.model->Draw(cmd);
}

void TriangleRenderSystem::CreatePipelineLayout(vk::DescriptorSetLayout globalSetLayout) {
    auto pushConstantRange = vk::PushConstantRange()
                                 .setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
                                 .setOffset(0)
                                 .setSize(sizeof(TrianglePushConstantData));
    std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = {globalSetLayout};

    auto pipelineLayoutCreateInfo = vk::PipelineLayoutCreateInfo()
                                        .setSetLayouts(descriptorSetLayouts)
                                        .setPushConstantRanges(pushConstantRange);
    pipelineLayout_ = Context::GetInstance().device.createPipelineLayout(pipelineLayoutCreateInfo);
}

void TriangleRenderSystem::CreatePipeline(vk::RenderPass renderPass) {
    PipelineConfigInfo pipelineConfig{};
    IdaPipeline::DefaultPipelineConfigInfo(pipelineConfig);
    IdaPipeline::EnableAlphaBlending(pipelineConfig);
    pipelineConfig.renderPass = renderPass;
    pipelineConfig.pipelineLayout = pipelineLayout_;
    pipeline_ = std::make_unique<IdaPipeline>(ReadWholeFile("shaders/triangle.vert.spv"),
                                              ReadWholeFile("shaders/triangle.frag.spv"),
                                              pipelineConfig);
}

} // namespace ida
