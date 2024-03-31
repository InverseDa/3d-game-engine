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

  private:
    std::unique_ptr<ida::Graphics> graphics_;
    ida::IdaGameObject::Map gameObjects_;

    void LoadGameObjects();
};

#endif // ENGINE_APP_HPP
