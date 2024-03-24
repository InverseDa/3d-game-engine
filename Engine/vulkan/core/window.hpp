#ifndef VULKAN_LIB_WINDOW_HPP
#define VULKAN_LIB_WINDOW_HPP

#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"

#include "vulkan/vulkan.hpp"
#include <string>

#include "tools.hpp"

namespace ida {
class IdaWindow {
  public:
    IdaWindow(int w, int h, std::string name);
    ~IdaWindow();
    IdaWindow(const IdaWindow&) = delete;
    IdaWindow& operator=(const IdaWindow&) = delete;

    vk::Extent2D GetExtent() { return {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_)}; }
    bool IsResizeNow() { return resizeNow_; }
    void ResetResizeFlag() { resizeNow_ = false; }

    bool ShouldClose() { return glfwWindowShouldClose(window_); }
    GLFWwindow* GetWindow() { return window_; }

    void Run(std::function<void()> func);

    GetSurfaceCallback getSurfaceCallback;
    KeyboardEventCallback keyboardEventCallback;
    std::vector<const char*> extensions;

    std::string title_;
    int width_;
    int height_;
    bool resizeNow_ = false;

    GLFWwindow* window_;

  private:
    void InitGLFWWindow();
};
} // namespace ida

#endif // VULKAN_LIB_WINDOW_HPP
