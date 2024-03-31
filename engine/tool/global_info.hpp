#ifndef VULKAN_LIB_GLOBAL_INFO_HPP
#define VULKAN_LIB_GLOBAL_INFO_HPP

#include "glm/glm.hpp"

#include "object/camera.hpp"
#include "object/game_object.hpp"

namespace ida {

constexpr int MAX_LIGHTS = 10;

struct PointLight {
    glm::vec4 position{};
    glm::vec4 color{};
};

struct OCTreeRenderUniformPackage {
    glm::mat4 projection{1.f};
    glm::mat4 view{1.f};
    glm::mat4 inverseView{1.f};
};

struct SimpleRenderUniformPackage {
    glm::mat4 projection{1.f};
    glm::mat4 view{1.f};
    glm::mat4 inverseView{1.f};
    glm::vec4 ambientLightColor{1.f, 1.f, 1.f, .02f};
    PointLight pointLights[MAX_LIGHTS];
    int numLights;
};

struct FrameInfo {
    int frameIndex;
    float frameTime;
    vk::CommandBuffer commandBuffer;
    IdaCamera& camera;
    IdaGameObject::Map& gameObjects;
    std::unordered_map<std::string, vk::DescriptorSet> descriptorSets;
};
} // namespace ida
#endif // VULKAN_LIB_GLOBAL_INFO_HPP
