#ifndef ENGINE_MOUSE_CONTROLLER_HPP
#define ENGINE_MOUSE_CONTROLLER_HPP

#include "window.hpp"
#include "object/game_object.hpp"

namespace ida {
class MouseMovementController {
  public:
    void MouseMovement(GLFWwindow* window, float dt, IdaGameObject& gameObject);

  private:
    bool firstMouse = true;
    float lastX = 0.0f;
    float lastY = 0.0f;
    float movementSpeed = 3.0f;
    float lookSpeed = 1.5f;
};

glm::vec3 calculateRightDir(const IdaGameObject& gameObject);

} // namespace ida

#endif // ENGINE_MOUSE_CONTROLLER_HPP
