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

    bool InitGraphics(const std::string& title = "Vulkan Demo", int width = 800, int height = 600);
    std::unique_ptr<ida::IdaWindow>& window() { return window_; }
    std::unique_ptr<ida::IdaRenderer>& renderer() { return renderer_; }

  private:
    std::unique_ptr<ida::IdaWindow> window_{};
    std::unique_ptr<ida::IdaRenderer> renderer_{};

    bool InitWindow(const std::string& title, int width, int height);
    bool InitVulkanInstance();
    bool InitRenderer();
};

} // namespace ida

#endif // VULKAN_LIB_APP_HPP
