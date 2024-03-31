#ifndef VULKAN_LIB_APP_HPP
#define VULKAN_LIB_APP_HPP

#include "vulkan/vulkan.hpp"
#include <memory>
#include <vector>

#include "log/log.hpp"
#include "render/renderer.hpp"
#include "core/context.hpp"
#include "descriptor/descriptors.hpp"
#include "object/game_object.hpp"
#include "core/window.hpp"

namespace ida {
/**
 * @brief The Graphics class is the main
 * class for the vulkan core. It is responsible
 * for initializing the window, renderer, and global
 * descriptor pool.
 */
class Graphics {
  public:
    Graphics() = default;
    ~Graphics();

    bool InitGraphics(const std::string& title = "Vulkan Demo", int width = 800, int height = 600) {
        return InitWindow(title, width, height) && InitVulkanInstance() && InitRenderer() && InitGlobalPool();
    }

    std::unique_ptr<ida::IdaWindow>& window() { return window_; }
    std::unique_ptr<ida::IdaRenderer>& renderer() { return renderer_; }
    std::unique_ptr<ida::IdaDescriptorPool>& globalPool() { return globalPool_; }

  private:
    std::unique_ptr<ida::IdaWindow> window_{};
    std::unique_ptr<ida::IdaRenderer> renderer_{};
    std::unique_ptr<ida::IdaDescriptorPool> globalPool_{};

    bool InitWindow(const std::string& title = "Vulkan Demo", int width = 800, int height = 600) {
        window_ = std::make_unique<IdaWindow>(width, height, title);
        return window_ != nullptr;
    };

    bool InitVulkanInstance() {
        Context::Init(window_->extensions, window_->getSurfaceCallback);
        return Context::GetInstance().device != nullptr;
    }

    bool InitRenderer() {
        renderer_ = std::make_unique<IdaRenderer>(*window_);
        return renderer_ != nullptr;
    }

    bool InitGlobalPool() {
        globalPool_ = ida::IdaDescriptorPool::Builder()
                          .SetMaxSets(ida::IdaSwapChain::MAX_FRAMES_IN_FLIGHT)
                          .AddPoolSize(vk::DescriptorType::eUniformBuffer, ida::IdaSwapChain::MAX_FRAMES_IN_FLIGHT)
                          .Build();
        return globalPool_ != nullptr;
    };
};

} // namespace ida

#endif // VULKAN_LIB_APP_HPP
