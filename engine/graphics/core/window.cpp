#include "window.hpp"
#include "log/log.hpp"
#include "context.hpp"

namespace ida {

IdaWindow::IdaWindow(int w, int h, std::string name) : width_(w), height_(h), title_(name) {
    IO::PrintLog<LogLevel::info>("GLFW window details: width: {}, height: {}, title: {}", w, h, name);
    InitGLFWWindow();
}

IdaWindow::~IdaWindow() {
    glfwDestroyWindow(window_);
    glfwTerminate();
}

void IdaWindow::Run(std::function<void()> func) {
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        func();
    }
}
void IdaWindow::InitGLFWWindow() {
    IO::Assert(glfwInit(), "GLFW Can't init");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(width_, height_, title_.c_str(), nullptr, nullptr);
    IO::Assert(window_ != nullptr, "GLFW Can't create window");

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
                IO::Assert(glfwVulkanSupported(), "GLFW Can't support vulkan");
                IO::Assert(glfwCreateWindowSurface(instance, window_, nullptr, &surface) == VK_SUCCESS, "GLFW Can't create surface");
                return surface; };
    IO::PrintLog<LogLevel::info>("GLFW Window created");
}

} // namespace ida