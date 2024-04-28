#include "graphics.hpp"
#include "core/context.hpp"

namespace ida {
Graphics::~Graphics() {
    renderer_.reset();
    window_.reset();
    //    gameObjects_.clear();
    /**
     * @Warning: Context::Quit() must be called after all other game objects are destroyed
     */
    //    Context::Quit();
}

bool Graphics::InitGraphics(const std::string& title, int width, int height) {
    IO::PrintLog<LogLevel::info>("Initializing engine graphic utils");
    return InitWindow(title, width, height) && InitVulkanInstance() && InitRenderer();
}

bool Graphics::InitWindow(const std::string& title, int width, int height) {
    window_ = std::make_unique<IdaWindow>(width, height, title);
    return window_ != nullptr;
};

bool Graphics::InitVulkanInstance() {
    Context::Init(window_->extensions, window_->getSurfaceCallback);
    return Context::GetInstance().device != nullptr;
}

bool Graphics::InitRenderer() {
    renderer_ = std::make_unique<IdaRenderer>(*window_);
    return renderer_ != nullptr;
}

} // namespace ida