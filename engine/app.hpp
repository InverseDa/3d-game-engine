#ifndef ENGINE_APP_HPP
#define ENGINE_APP_HPP

#include <memory>

#include "graphics/graphics.hpp"
#include "object/game_object.hpp"

class App {
  public:
    App(const std::string& title = "Vulkan Demo", int width = 800, int height = 600);
    ~App();
    int Run();

    bool InitGlobalPool() {
        globalPool_ = ida::IdaDescriptorPool::Builder()
                          .SetMaxSets(2 * ida::IdaSwapChain::MAX_FRAMES_IN_FLIGHT)
                          .AddPoolSize(vk::DescriptorType::eUniformBuffer, ida::IdaSwapChain::MAX_FRAMES_IN_FLIGHT)
                          .AddPoolSize(vk::DescriptorType::eUniformBuffer, ida::IdaSwapChain::MAX_FRAMES_IN_FLIGHT)
                          .Build();
        return globalPool_ != nullptr;
    };

    std::unique_ptr<ida::IdaDescriptorPool>& globalPool() { return globalPool_; }

  private:
    std::unique_ptr<ida::Graphics> graphics_{};
    std::unique_ptr<ida::IdaDescriptorPool> globalPool_{};
    ida::IdaGameObject::Map gameObjects_{};

    void LoadGameObjects();
};

#endif // ENGINE_APP_HPP
