#ifndef VULKAN_LIB_KEYBOARD_CONTROLLER_HPP
#define VULKAN_LIB_KEYBOARD_CONTROLLER_HPP

#include "window.hpp"

#include "core/game_object.hpp"

namespace ida {
class KeyboardMovementController {
  public:
    struct KeyMappings {
        int moveForward = GLFW_KEY_W;
        int moveBackward = GLFW_KEY_S;
        int moveLeft = GLFW_KEY_A;
        int moveRight = GLFW_KEY_D;
        int moveUp = GLFW_KEY_SPACE;
        int moveDown = GLFW_KEY_LEFT_SHIFT;

        int lookLeft = GLFW_KEY_LEFT;
        int lookRight = GLFW_KEY_RIGHT;
        int lookUp = GLFW_KEY_UP;
        int lookDown = GLFW_KEY_DOWN;
    };
    void MoveInPlaneXZ(GLFWwindow* window, float dt, IdaGameObject& gameObject);

    KeyMappings keys{};
    float movementSpeed = 3.0f;
    float lookSpeed = 1.5f;
};
} // namespace ida

#endif // VULKAN_LIB_KEYBOARD_CONTROLLER_HPP
