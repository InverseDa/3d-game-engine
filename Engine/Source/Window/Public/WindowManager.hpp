#ifndef ENGINE_WINDOWMANAGER_HPP
#define ENGINE_WINDOWMANAGER_HPP

#include "Source/Type/Public/ManagerBase.hpp"
#include "Source/Window/Public/GLFWWindow.hpp"

#include <vector>
#include <unordered_map>
#include <memory>
#include <string>

class WindowManager : public ManagerBase {
  public:
    struct Builder {
        Builder(const std::string& index);
        Builder& AddExtension(const char* extension);
        Builder& SetTitle(const std::string& title);
        Builder& SetWidth(uint32_t width);
        Builder& SetHeight(uint32_t height);
        Builder& SetFullscreen(bool fullscreen);
        Builder& SetVsync(bool vsync);
        Builder& SetResizable(bool resizable);
        std::shared_ptr<GLFWWindow> Build(const std::string _index, const std::string& title, uint32_t width, uint32_t height, bool fullscreen, bool vsync, bool resizable);

        std::string _title;
        uint32_t _width;
        uint32_t _height;
        bool _fullscreen;
        bool _vsync;
        bool _resizable;
        std::vector<const char*> _extensions;
        std::string _index;
    };

    WindowManager();
    ~WindowManager();

    void Init() override;
    std::shared_ptr<GLFWWindow> GetWindow(const std::string& title) {
        return _windows[title];
    }

  private:
    std::vector<const char*> _extensions;
    std::unordered_map<std::string, std::shared_ptr<GLFWWindow>> _windows;
};

#endif // ENGINE_WINDOWMANAGER_HPP
