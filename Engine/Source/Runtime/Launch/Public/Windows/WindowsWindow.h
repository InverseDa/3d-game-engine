#pragma once

#include "CoreMinimal.h"
#include "Platform/Platform.h"
#include <Windows.h>

class FWindowsWindow : public FPlatformWindow
{
public:
    FWindowsWindow(uint32 Width, uint32 Height, const char* Title);
    ~FWindowsWindow() override;

public:
    void* GetNativeHandle() const override { return static_cast<void*>(Hwnd); }
    bool ProcessMessages() override;
    void GetSize(uint32& OutWidth, uint32& OutHeight) const override;
    bool IsMinimized() const override;

private:
    HWND Hwnd = nullptr;
    uint32 Width = 0;
    uint32 Height = 0;
    bool bRunning = true;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
