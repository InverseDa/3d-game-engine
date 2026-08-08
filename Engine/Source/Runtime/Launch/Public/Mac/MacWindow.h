#pragma once

#include "CoreMinimal.h"
#include "Platform/Platform.h"

namespace LE
{

class FMacWindow : public FPlatformWindow
{
public:
    FMacWindow(uint32 Width, uint32 Height, const char* Title);
    ~FMacWindow() override;

public:
    void* GetNativeHandle() const override;
    LE::FRALSurfaceDesc GetSurfaceDesc() const override;
    bool ProcessMessages() override;
    void GetSize(uint32& OutWidth, uint32& OutHeight) const override;
    bool IsMinimized() const override;

public:
    void NotifyClosed();
    void NotifyResized(uint32 NewWidth, uint32 NewHeight);

private:
    void UpdateDrawableSize() const;

private:
    void* WindowHandle = nullptr;
    void* ViewHandle = nullptr;
    void* LayerHandle = nullptr;
    void* DelegateHandle = nullptr;
    uint32 Width = 0;
    uint32 Height = 0;
    bool bRunning = true;
};

} // namespace LE
