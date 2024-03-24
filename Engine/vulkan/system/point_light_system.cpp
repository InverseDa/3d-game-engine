#include "point_light_system.hpp"
#include "core/context.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/glm.hpp"
#include "glm/gtc/constants.hpp"
#include "glm/gtx/rotate_vector.hpp"

#include <map>

namespace ida {
struct PointLightPushConstants {
    glm::vec4 position;
    glm::vec4 color;
    float radius;
};

PointLightSystem::PointLightSystem(vk::RenderPass renderPass, vk::DescriptorSetLayout globalSetLayout) {
    CreatePipelineLayout(globalSetLayout);
    CreatePipeline(renderPass);
}

PointLightSystem::~PointLightSystem() {
    Context::GetInstance().device.destroyPipelineLayout(pipelineLayout_);
}

void PointLightSystem::Update(FrameInfo& frameInfo, GlobalUbo& globalUbo) {
    auto rotateLight = glm::rotate(glm::mat4(1.f), 0.5f * frameInfo.frameTime, {0.f, -1.f, 0.f});
    int lightIndex = 0;
    for (auto& kv : frameInfo.gameObjects) {
        auto& obj = kv.second;
        if (obj.pointLight == nullptr)
            continue;
        IO::Assert(lightIndex < MAX_LIGHTS, "Too many point lights");
        obj.transform.translation = glm::vec3(rotateLight * glm::vec4(obj.transform.translation, 1.f));

        globalUbo.pointLights[lightIndex].position = glm::vec4(obj.transform.translation, 1.f);
        globalUbo.pointLights[lightIndex].color = glm::vec4(obj.color, obj.pointLight->lightIntensity);

        lightIndex++;
    }
    globalUbo.numLights = lightIndex;
}

void PointLightSystem::Render(FrameInfo& frameInfo) {
    std::map<float, IdaGameObject::id_t> sorted;
    for (auto& kv : frameInfo.gameObjects) {
        auto& obj = kv.second;
        if (obj.pointLight == nullptr)
            continue;
        auto offset = frameInfo.camera.GetPosition() - obj.transform.translation;
        float disSquared = glm::dot(offset, offset);
        sorted[disSquared] = obj.GetId();
    }
    pipeline_->Bind(frameInfo.commandBuffer);
    frameInfo.commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                               pipelineLayout_,
                                               0,
                                               frameInfo.globalDescriptorSet,
                                               nullptr);
    for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
        auto& obj = frameInfo.gameObjects.at(it->second);
        PointLightPushConstants pushConstants{};
        pushConstants.position = glm::vec4(obj.transform.translation, 1.f);
        pushConstants.color = glm::vec4(obj.color, obj.pointLight->lightIntensity);
        pushConstants.radius = obj.transform.scale.x;

        frameInfo.commandBuffer.pushConstants<PointLightPushConstants>(pipelineLayout_,
                                                                       vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                                                       0,
                                                                       pushConstants);
        frameInfo.commandBuffer.draw(6, 1, 0, 0);
    }
}

void PointLightSystem::CreatePipelineLayout(vk::DescriptorSetLayout globalSetLayout) {
    auto pushConstantRange = vk::PushConstantRange()
                                 .setStageFlags(vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eVertex)
                                 .setOffset(0)
                                 .setSize(sizeof(PointLightPushConstants));
    std::vector<vk::DescriptorSetLayout> layouts = {globalSetLayout};

    auto createInfo = vk::PipelineLayoutCreateInfo()
                          .setSetLayouts(layouts)
                          .setPushConstantRanges(pushConstantRange);
    pipelineLayout_ = Context::GetInstance().device.createPipelineLayout(createInfo);
}

void PointLightSystem::CreatePipeline(vk::RenderPass renderPass) {
    PipelineConfigInfo pipelineConfigInfo{};
    IdaPipeline::DefaultPipelineConfigInfo(pipelineConfigInfo);
    IdaPipeline::EnableAlphaBlending(pipelineConfigInfo);
    pipelineConfigInfo.attributeDescriptions.clear();
    pipelineConfigInfo.bindingDescriptions.clear();
    pipelineConfigInfo.renderPass = renderPass;
    pipelineConfigInfo.pipelineLayout = pipelineLayout_;
    pipeline_ = std::make_unique<IdaPipeline>(ReadWholeFile("shaders/point_light.vert.spv"),
                                              ReadWholeFile("shaders/point_light.frag.spv"),
                                              pipelineConfigInfo);
}

} // namespace ida
