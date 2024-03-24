#ifndef VULKAN_LIB_APP_HPP
#define VULKAN_LIB_APP_HPP

#include "vulkan/vulkan.hpp"
#include <memory>
#include <vector>

#include "log/log.hpp"
#include "render/renderer.hpp"
#include "core/context.hpp"
#include "descriptor/descriptors.hpp"
#include "../object/game_object.hpp"
#include "core/window.hpp"

class Graphics {
  public:
    Graphics(const std::string& title = "Vulkan Demo", int width = 800, int height = 600);
    ~Graphics();
    int Run();

  private:
    std::unique_ptr<ida::IdaWindow> window_;
    std::unique_ptr<ida::IdaRenderer> renderer_;
    std::unique_ptr<ida::IdaDescriptorPool> globalPool{};
    ida::IdaGameObject::Map gameObjects_;

    void LoadGameObjects();
};

namespace ida {



}

#endif // VULKAN_LIB_APP_HPP
