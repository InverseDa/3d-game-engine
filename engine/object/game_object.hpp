#ifndef VULKAN_LIB_GAME_OBJECT_HPP
#define VULKAN_LIB_GAME_OBJECT_HPP

#include <unordered_map>
#include <memory>
#include "glm/glm.hpp"

#include "graphics/model/model.hpp"

namespace ida {

enum GameObjectType {
    Camera,
    Model,
    Light,
};

inline std::unordered_map<GameObjectType, std::string> GameObjectTypeNames = {
    {Camera, "Camera"},
    {Model, "Model"},
    {Light, "Light"},
};

struct TransformComponent {
    glm::vec3 translation{};
    glm::vec3 rotation{};
    glm::vec3 scale{1.f};

    glm::mat4 mat4();

    glm::mat3 normalMatrix();
};

struct PointLightComponent {
    float lightIntensity = 1.0f;
};

class IdaGameObject {
  public:
    using id_t = unsigned int;
    using name_t = std::string;
    using Map = std::unordered_map<name_t, IdaGameObject>;

    template <GameObjectType T>
    static IdaGameObject CreateGameObject(const std::string& name) {
        return IdaGameObject{name, T};
    }

    static IdaGameObject MakePointLight(
        const std::string& lightName,
        float intensity = 10.f,
        float radius = 0.1f,
        glm::vec3 color = glm::vec3(1.f));

    IdaGameObject(const IdaGameObject&) = delete;
    IdaGameObject& operator=(const IdaGameObject&) = delete;
    IdaGameObject(IdaGameObject&&) = default;
    IdaGameObject& operator=(IdaGameObject&&) = default;

    ~IdaGameObject();

    name_t GetName() const { return name_; }

    glm::vec3 color{};
    TransformComponent transform{};

    std::shared_ptr<IdaModel> model{};
    std::unique_ptr<PointLightComponent> pointLight{};

  private:
    IdaGameObject(name_t name, GameObjectType type) : name_{name}, type_{type} {}

    name_t name_;
    GameObjectType type_;
};

} // namespace ida
#endif // VULKAN_LIB_GAME_OBJECT_HPP
