#ifndef ENGINE_APP_HPP
#define ENGINE_APP_HPP

#include <memory>

#include "graphics/core/window.hpp"

class Application {
public:
    Application();

    ~Application();

    void Run();

private:
    void Init();

    void Loop();

    void CleanUp();

    std::unique_ptr<ida::IdaWindow> window_;
};

#endif //ENGINE_APP_HPP
