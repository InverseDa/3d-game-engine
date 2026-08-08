#include "Windows/WindowsPlatformWindow.h"

#include "Containers/String.h"

#include <new>

namespace LE
{

namespace
{
void DestroyPlatformWindow(void*, FPlatformWindow* const Window) noexcept
{
    delete Window;
}
}

FWindowsPlatformWindow::FWindowsPlatformWindow(const FPlatformWindowDesc& Description)
    : Width(Description.Width)
    , Height(Description.Height)
{
    WNDCLASSEXA WindowClass = {};
    WindowClass.cbSize = sizeof(WNDCLASSEXA);
    WindowClass.style = CS_HREDRAW | CS_VREDRAW;
    WindowClass.lpfnWndProc = &FWindowsPlatformWindow::WndProc;
    WindowClass.hInstance = GetModuleHandleA(nullptr);
    WindowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    WindowClass.lpszClassName = "LimitlessEngineWindowClass";
    if (RegisterClassExA(&WindowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return;
    }

    RECT WindowRect = { 0, 0, static_cast<LONG>(Width), static_cast<LONG>(Height) };
    if (!AdjustWindowRect(&WindowRect, WS_OVERLAPPEDWINDOW, FALSE))
    {
        return;
    }

    const LE::String Title(Description.Title);
    Hwnd = CreateWindowExA(
        0,
        WindowClass.lpszClassName,
        Title.IsEmpty() ? "Limitless Engine" : Title.Data(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        WindowRect.right - WindowRect.left,
        WindowRect.bottom - WindowRect.top,
        nullptr,
        nullptr,
        WindowClass.hInstance,
        this);
    if (Hwnd == nullptr)
    {
        return;
    }
}

FWindowsPlatformWindow::~FWindowsPlatformWindow()
{
    PendingEvents.Clear();
    if (Hwnd != nullptr)
    {
        DestroyWindow(Hwnd);
        Hwnd = nullptr;
    }
}

FPlatformSurface FWindowsPlatformWindow::GetSurface() const noexcept
{
    FPlatformSurface Surface;
    Surface.Type = EPlatformSurfaceType::Win32;
    Surface.WindowHandle = static_cast<void*>(Hwnd);
    return Surface;
}

void FWindowsPlatformWindow::PumpEvents(FPlatformEventQueue& OutEvents)
{
    MSG Message;
    while (PeekMessageA(&Message, nullptr, 0, 0, PM_REMOVE))
    {
        if (Message.message == WM_QUIT)
        {
            if (!bCloseRequested)
            {
                bCloseRequested = true;
                PushEvent(EPlatformEventType::CloseRequested);
            }
            continue;
        }
        TranslateMessage(&Message);
        DispatchMessageA(&Message);
    }

    DrainPendingEvents(OutEvents);
}

void FWindowsPlatformWindow::PushEvent(const EPlatformEventType Type)
{
    FPlatformEvent Event;
    Event.Type = Type;
    Event.WindowId = GetId();
    Event.Width = Width;
    Event.Height = Height;
    PendingEvents.Push(Event);
}

void FWindowsPlatformWindow::DrainPendingEvents(FPlatformEventQueue& OutEvents)
{
    FPlatformEvent Event;
    while (PendingEvents.Poll(Event))
    {
        OutEvents.Push(Event);
    }
}

LRESULT CALLBACK FWindowsPlatformWindow::WndProc(
    const HWND WindowHandle,
    const UINT Message,
    const WPARAM WParam,
    const LPARAM LParam)
{
    FWindowsPlatformWindow* Window = reinterpret_cast<FWindowsPlatformWindow*>(
        GetWindowLongPtrA(WindowHandle, GWLP_USERDATA));
    if (Message == WM_NCCREATE)
    {
        CREATESTRUCTA* const Create = reinterpret_cast<CREATESTRUCTA*>(LParam);
        Window = static_cast<FWindowsPlatformWindow*>(Create->lpCreateParams);
        if (Window != nullptr)
        {
            Window->Hwnd = WindowHandle;
            SetWindowLongPtrA(WindowHandle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(Window));
        }
    }

    switch (Message)
    {
        case WM_CLOSE:
            if (Window != nullptr && !Window->bCloseRequested)
            {
                Window->bCloseRequested = true;
                Window->PushEvent(EPlatformEventType::CloseRequested);
            }
            return 0;

        case WM_DESTROY:
            return 0;

        case WM_NCDESTROY:
            SetWindowLongPtrA(WindowHandle, GWLP_USERDATA, 0);
            if (Window != nullptr && Window->Hwnd == WindowHandle)
            {
                Window->Hwnd = nullptr;
            }
            return DefWindowProcA(WindowHandle, Message, WParam, LParam);

        case WM_SIZE:
            if (Window != nullptr)
            {
                const bool bWasMinimized = Window->bMinimized;
                Window->Width = static_cast<uint32>(LOWORD(LParam));
                Window->Height = static_cast<uint32>(HIWORD(LParam));
                Window->bMinimized = WParam == SIZE_MINIMIZED;

                if (Window->bMinimized && !bWasMinimized)
                {
                    Window->PushEvent(EPlatformEventType::Minimized);
                }
                else if (!Window->bMinimized && bWasMinimized)
                {
                    Window->PushEvent(EPlatformEventType::Restored);
                }
                if (!Window->bMinimized)
                {
                    Window->PushEvent(EPlatformEventType::Resized);
                }
            }
            return 0;

        default:
            return DefWindowProcA(WindowHandle, Message, WParam, LParam);
    }
}

FPlatformWindowPtr CreatePlatformWindow(const FPlatformWindowDesc& Description)
{
    FWindowsPlatformWindow* const Window = new (std::nothrow) FWindowsPlatformWindow(Description);
    if (Window == nullptr)
    {
        return {};
    }
    if (!Window->IsValid())
    {
        delete Window;
        return {};
    }
    return FPlatformWindowPtr::Adopt(Window, nullptr, &DestroyPlatformWindow);
}

} // namespace LE
