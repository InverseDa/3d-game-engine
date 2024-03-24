#ifndef VULKAN_LIB_GAME_OBJECT_HPP
#define VULKAN_LIB_GAME_OBJECT_HPP

#include <unordered_map>
#include <memory>
#include "glm/glm.hpp"

#include "model/model.hpp"

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
    using Map = std::unordered_map<id_t, IdaGameObject>;

    static IdaGameObject CreateGameObject(GameObjectType type) {
        static id_t currentId = 0;
        return IdaGameObject{currentId++, type};
    }

    static IdaGameObject MakePointLight(
        float intensity = 10.f,
        float radius = 0.1f,
        glm::vec3 color = glm::vec3(1.f));

    IdaGameObject(const IdaGameObject&) = delete;
    IdaGameObject& operator=(const IdaGameObject&) = delete;
    IdaGameObject(IdaGameObject&&) = default;
    IdaGameObject& operator=(IdaGameObject&&) = default;

    ~IdaGameObject();

    id_t GetId() const { return id_; }

    glm::vec3 color{};
    TransformComponent transform{};

    std::shared_ptr<IdaModel> model{};
    std::unique_ptr<PointLightComponent> pointLight{};

  private:
    IdaGameObject(id_t id, GameObjectType type) : id_{id}, type_{type} {}

    id_t id_;
    GameObjectType type_;
};

} // namespace ida
#endif // VULKAN_LIB_GAME_OBJECT_HPP
