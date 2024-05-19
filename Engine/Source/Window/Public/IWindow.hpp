#ifndef ENGINE_IWINDOW_HPP
#define ENGINE_IWINDOW_HPP

class IWindow {
  public:
    virtual ~IWindow() = default;
    virtual void Init() = 0;
};

#endif // ENGINE_IWINDOW_HPP
