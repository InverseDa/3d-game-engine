#ifndef VULKAN_LIB_CAMERA_HPP
#define VULKAN_LIB_CAMERA_HPP

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/glm.hpp"

namespace ida {
class IdaCamera {
  public:
    void SetOrthographicProjection(float left, float right, float bottom, float top, float near, float far);
    void SetPerspectiveProjection(float fov, float aspect, float near, float far);

    void SetViewDirection(glm::vec3 position, glm::vec3 direction, glm::vec3 up);
    void SetViewTarget(glm::vec3 position, glm::vec3 target, glm::vec3 up);
    void SetViewYXZ(glm::vec3 position, glm::vec3 rotation);

    const glm::mat4& GetProjection() const { return projection; }
    const glm::mat4& GetView() const { return view; }
    const glm::mat4& GetInverseView() const { return inverseView; }
    const glm::vec3 GetPosition() const { return glm::vec3(inverseView[3]); }

  private:
    glm::mat4 projection;
    glm::mat4 view;
    glm::mat4 inverseView;
};
} // namespace ida

#endif // VULKAN_LIB_CAMERA_HPP
