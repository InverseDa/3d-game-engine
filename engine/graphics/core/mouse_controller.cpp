#include "mouse_controller.hpp"
#include "glm/gtc/constants.hpp"

namespace ida {
void MouseMovementController::MouseMovement(GLFWwindow* window, float dt, IdaGameObject& gameObject) {
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;

    lastX = xpos;
    lastY = ypos;


    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS &&
        glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        glm::vec3 rightDir = calculateRightDir(gameObject);
        glm::vec3 translation = movementSpeed * dt * xoffset * rightDir;
        translation.y = 0;
        gameObject.transform.translation += translation;
    } else {
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
            if (glfwGetInputMode(window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED) {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            }
            gameObject.transform.rotation.y += lookSpeed * xoffset * dt;
            gameObject.transform.rotation.x -= lookSpeed * yoffset * dt;
        } else {
            if (glfwGetInputMode(window, GLFW_CURSOR) != GLFW_CURSOR_NORMAL) {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                glfwSetCursorPos(window, lastX, lastY);
            }
        }
    }

    gameObject.transform.rotation.x = glm::clamp(gameObject.transform.rotation.x, -1.5f, 1.5f);
    gameObject.transform.rotation.y = glm::mod(gameObject.transform.rotation.y, glm::two_pi<float>());
}

glm::vec3 calculateRightDir(const IdaGameObject& gameObject) {
    float yaw = gameObject.transform.rotation.y;
    return glm::vec3(std::cos(yaw), 0.0f, -std::sin(yaw));
}

} // namespace ida
