#pragma once

#include "Platform/PlatformWindow.h"

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <Windows.h>

namespace LE
{

class FWindowsPlatformWindow final : public FPlatformWindow
{
public:
    explicit FWindowsPlatformWindow(const FPlatformWindowDesc& Description);
    ~FWindowsPlatformWindow() override;

    bool IsValid() const noexcept override { return Hwnd != nullptr; }
    FPlatformSurface GetSurface() const noexcept override;
    void PumpEvents(FPlatformEventQueue& OutEvents) override;
    FPlatformWindowSize GetSize() const noexcept override { return { Width, Height }; }
    bool IsMinimized() const noexcept override { return bMinimized; }
    bool IsCloseRequested() const noexcept override { return bCloseRequested; }

private:
    void PushEvent(EPlatformEventType Type);
    void DrainPendingEvents(FPlatformEventQueue& OutEvents);
    static LRESULT CALLBACK WndProc(HWND WindowHandle, UINT Message, WPARAM WParam, LPARAM LParam);

    HWND Hwnd = nullptr;
    uint32 Width = 0;
    uint32 Height = 0;
    bool bMinimized = false;
    bool bCloseRequested = false;
    FPlatformEventQueue PendingEvents;
};

} // namespace LE
