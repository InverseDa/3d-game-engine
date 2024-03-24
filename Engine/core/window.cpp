#include "window.hpp"
#include "log/log.hpp"
#include "context.hpp"

namespace ida {

IdaWindow::IdaWindow(int w, int h, std::string name) : width_(w), height_(h), title_(name) {
    InitGLFWWindow();
}

IdaWindow::~IdaWindow() {
    glfwDestroyWindow(window_);
    glfwTerminate();
}

void IdaWindow::Run(std::function<void()> func) {
    while (!glfwWindowShouldClose(window_)) {
        func();
    }
}
void IdaWindow::InitGLFWWindow() {
    if (!glfwInit()) {
        IO::ThrowError("GLFW Can't init");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(width_, height_, title_.c_str(), nullptr, nullptr);
    if (window_ == nullptr) {
        IO::ThrowError("GLFW Can't create window");
    }
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* window, int width, int height) {
        auto idaWindow = reinterpret_cast<IdaWindow*>(glfwGetWindowUserPointer(window));
        idaWindow->resizeNow_ = true;
        idaWindow->width_ = width;
        idaWindow->height_ = height;
    });
    unsigned int count;
    const char** extensions = glfwGetRequiredInstanceExtensions(&count);
    this->extensions.resize(count);
    for (int i = 0; i < count; i++) {
        this->extensions[i] = extensions[i];
    }

    getSurfaceCallback = [&](vk::Instance instance) {
                VkSurfaceKHR surface;
                if (glfwCreateWindowSurface(instance, window_, nullptr, &surface) != VK_SUCCESS) {
                IO::ThrowError("GLFW Can't create surface");
                }
                return surface; };
}

} // namespace ida