#include "CoreMinimal.h"
#include "Windows/WindowsWindow.h"

namespace LE
{

// 静态窗口映射（用于 WndProc 回调）
static LE::HashMap<HWND, FWindowsWindow*> g_WindowMap;

FWindowsWindow::FWindowsWindow(uint32 InWidth, uint32 InHeight, const char* Title)
    : Width(InWidth), Height(InHeight)
{
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &FWindowsWindow::WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "LimitlessEngineWindowClass";
    RegisterClassEx(&wc);

    RECT rect = { 0, 0, (LONG)Width, (LONG)Height };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    Hwnd = CreateWindowEx(
        0, "LimitlessEngineWindowClass", Title,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr
    );

    g_WindowMap.InsertOrAssign(Hwnd, this);
}

FWindowsWindow::~FWindowsWindow()
{
    if (Hwnd)
    {
        g_WindowMap.Erase(Hwnd);
        DestroyWindow(Hwnd);
        Hwnd = nullptr;
    }
}

LE::FRALSurfaceDesc FWindowsWindow::GetSurfaceDesc() const
{
    LE::FRALSurfaceDesc SurfaceDesc;
    SurfaceDesc.Type = LE::ERALSurfaceType::Win32;
    SurfaceDesc.WindowHandle = static_cast<void*>(Hwnd);
    return SurfaceDesc;
}

bool FWindowsWindow::ProcessMessages()
{
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
            return false;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return bRunning;
}

void FWindowsWindow::GetSize(uint32& OutWidth, uint32& OutHeight) const
{
    OutWidth = Width;
    OutHeight = Height;
}

bool FWindowsWindow::IsMinimized() const
{
    return IsIconic(Hwnd);
}

LRESULT CALLBACK FWindowsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    FWindowsWindow* const* const Found = g_WindowMap.Find(hwnd);
    FWindowsWindow* Window = Found != nullptr ? *Found : nullptr;

    switch (msg)
    {
        case WM_CLOSE:
            if (Window) Window->bRunning = false;
            PostQuitMessage(0);
            return 0;
        case WM_DESTROY:
            return 0;
        case WM_SIZE:
            if (Window)
            {
                Window->Width = static_cast<uint32>(LOWORD(lParam));
                Window->Height = static_cast<uint32>(HIWORD(lParam));
            }
            return 0;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

} // namespace LE
