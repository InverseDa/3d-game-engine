#pragma once

#include "Platform/PlatformWindow.h"

namespace LE
{

class FMacPlatformWindow final : public FPlatformWindow
{
public:
    explicit FMacPlatformWindow(const FPlatformWindowDesc& Description);
    ~FMacPlatformWindow() override;

    bool IsValid() const noexcept override { return WindowHandle != nullptr; }
    FPlatformSurface GetSurface() const noexcept override;
    void PumpEvents(FPlatformEventQueue& OutEvents) override;
    FPlatformWindowSize GetSize() const noexcept override { return { Width, Height }; }
    bool IsMinimized() const noexcept override { return bMinimized; }
    bool IsCloseRequested() const noexcept override { return bCloseRequested; }

    void NotifyClosed();
    void NotifyResized(uint32 NewWidth, uint32 NewHeight);
    void NotifyMinimized(bool bIsMinimized);

private:
    void PushEvent(EPlatformEventType Type);
    void DrainPendingEvents(FPlatformEventQueue& OutEvents);
    void UpdateDrawableSize() const;

    void* WindowHandle = nullptr;
    void* ViewHandle = nullptr;
    void* LayerHandle = nullptr;
    void* DelegateHandle = nullptr;
    uint32 Width = 0;
    uint32 Height = 0;
    bool bMinimized = false;
    bool bCloseRequested = false;
    FPlatformEventQueue PendingEvents;
};

} // namespace LE
