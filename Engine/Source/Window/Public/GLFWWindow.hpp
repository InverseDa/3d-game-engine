#ifndef ENGINE_GLFWWINDOW_HPP
#define ENGINE_GLFWWINDOW_HPP

#include <GLFW/glfw3.h>
#include <string>
#include <memory>

class GLFWWindow {
  public:
    GLFWWindow();
    ~GLFWWindow();

  private:
    std::shared_ptr<GLFWwindow> _window;
    std::string _title;
    uint32_t _width;
    uint32_t _height;
    bool _fullscreen;
    bool _vsync;
    bool _resizable;
};

#endif // ENGINE_GLFWWINDOW_HPP
