#include "Windows/WindowsPlatformWindow.h"

#include "Containers/String.h"
#include "Logger/Log.h"

#include <hidusage.h>

#include <cstddef>
#include <new>

namespace LE
{

namespace
{
EPlatformKey GetPlatformKey(const WPARAM VirtualKey) noexcept
{
    switch (VirtualKey)
    {
    case 'W':
        return EPlatformKey::W;
    case 'A':
        return EPlatformKey::A;
    case 'S':
        return EPlatformKey::S;
    case 'D':
        return EPlatformKey::D;
    case VK_F5:
        return EPlatformKey::F5;
    default:
        return EPlatformKey::Unknown;
    }
}

void DestroyPlatformWindow(void*, FPlatformWindow* const Window) noexcept
{
    delete Window;
}
}

FWindowsPlatformWindow::FWindowsPlatformWindow(const FPlatformWindowDesc& Description) : Width(Description.Width), Height(Description.Height), bInputEnabled(Description.bEnableInput)
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

    RECT WindowRect = {0, 0, static_cast<LONG>(Width), static_cast<LONG>(Height)};
    if (!AdjustWindowRect(&WindowRect, WS_OVERLAPPEDWINDOW, FALSE))
    {
        return;
    }

    const LE::String Title(Description.Title);
    Hwnd = CreateWindowExA(0, WindowClass.lpszClassName, Title.IsEmpty() ? "Limitless Engine" : Title.Data(), WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, WindowRect.right - WindowRect.left, WindowRect.bottom - WindowRect.top, nullptr, nullptr, WindowClass.hInstance, this);
    if (Hwnd == nullptr)
    {
        return;
    }
    bHasFocus = GetFocus() == Hwnd;
    if (bInputEnabled)
    {
        RegisterRawMouse();
    }
}

FWindowsPlatformWindow::~FWindowsPlatformWindow()
{
    if (Hwnd != nullptr)
    {
        RequestClose();
        DestroyWindow(Hwnd);
        Hwnd = nullptr;
    }
    PendingEvents.Clear();
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
            RequestClose();
            continue;
        }
        TranslateMessage(&Message);
        DispatchMessageA(&Message);
    }

    DrainPendingEvents(OutEvents);
}

void FWindowsPlatformWindow::PushEvent(FPlatformEvent Event)
{
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

bool FWindowsPlatformWindow::CanReceiveInput() const noexcept
{
    return bInputEnabled && bHasFocus && !bCloseRequested && !bMinimized && Width != 0 && Height != 0;
}

void FWindowsPlatformWindow::HandleKey(const EPlatformEventType Type, const WPARAM VirtualKey, const LPARAM KeyFlags)
{
    const EPlatformKey Key = GetPlatformKey(VirtualKey);
    if (CanReceiveInput() && Key != EPlatformKey::Unknown)
    {
        FPlatformEvent Event{Type};
        Event.Key = Key;
        constexpr LPARAM PreviousKeyStateBit = LPARAM{1} << 30;
        Event.bRepeat = Type == EPlatformEventType::KeyPressed && (KeyFlags & PreviousKeyStateBit) != 0;
        PushEvent(Event);
    }
}

void FWindowsPlatformWindow::RegisterRawMouse()
{
    const RAWINPUTDEVICE Device{HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_MOUSE, 0, Hwnd};
    bRawMouseRegistered = RegisterRawInputDevices(&Device, 1, sizeof(Device)) != FALSE;
    if (!bRawMouseRegistered)
    {
        const DWORD Error = GetLastError();
        auto* const Logger = Log::GetLoggerOrCreate("Platform");
        LE_LOG(Logger, Warn, "Could not register raw mouse input for window {} (Win32 error {}).", GetId(), Error);
    }
}

void FWindowsPlatformWindow::UnregisterRawMouse()
{
    if (!bRawMouseRegistered)
    {
        return;
    }
    bRawMouseRegistered = false;
    const RAWINPUTDEVICE Device{HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_MOUSE, RIDEV_REMOVE, nullptr};
    if (!RegisterRawInputDevices(&Device, 1, sizeof(Device)))
    {
        const DWORD Error = GetLastError();
        auto* const Logger = Log::GetLoggerOrCreate("Platform");
        LE_LOG(Logger, Warn, "Could not unregister raw mouse input for window {} (Win32 error {}).", GetId(), Error);
    }
}

void FWindowsPlatformWindow::HandleRawMouse(const HRAWINPUT Input)
{
    if (!bMouseCaptured || !CanReceiveInput())
    {
        return;
    }

    RAWINPUT Raw{};
    UINT Size = sizeof(Raw);
    const UINT Bytes = GetRawInputData(Input, RID_INPUT, &Raw, &Size, sizeof(RAWINPUTHEADER));
    if (Bytes == static_cast<UINT>(-1) || Bytes < offsetof(RAWINPUT, data) + sizeof(RAWMOUSE) || Raw.header.dwType != RIM_TYPEMOUSE || (Raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0 || (Raw.data.mouse.lLastX == 0 && Raw.data.mouse.lLastY == 0))
    {
        return;
    }

    FPlatformEvent Event{EPlatformEventType::MouseMoved};
    Event.MouseDeltaX = Raw.data.mouse.lLastX;
    Event.MouseDeltaY = Raw.data.mouse.lLastY;
    PushEvent(Event);
}

void FWindowsPlatformWindow::BeginMouseCapture()
{
    if (!CanReceiveInput() || !bRawMouseRegistered || bMouseCaptured)
    {
        return;
    }

    SetCapture(Hwnd);
    if (GetCapture() != Hwnd)
    {
        auto* const Logger = Log::GetLoggerOrCreate("Platform");
        LE_LOG(Logger, Warn, "Window {} could not acquire right-button mouse capture.", GetId());
        return;
    }
    bMouseCaptured = true;
    PushEvent(EPlatformEventType::MouseCaptureStarted);
}

void FWindowsPlatformWindow::EndMouseCapture()
{
    if (!bMouseCaptured)
    {
        return;
    }
    bMouseCaptured = false;
    PushEvent(EPlatformEventType::MouseCaptureEnded);
    if (GetCapture() == Hwnd)
    {
        ReleaseCapture();
    }
}

void FWindowsPlatformWindow::CancelInput()
{
    if (!bInputEnabled)
    {
        return;
    }
    const bool bReleaseCapture = bMouseCaptured && GetCapture() == Hwnd;
    // The synchronous WM_CAPTURECHANGED from our own release is not a cancellation.
    bMouseCaptured = false;
    PushEvent(EPlatformEventType::InputCanceled);
    if (bReleaseCapture)
    {
        ReleaseCapture();
    }
}

void FWindowsPlatformWindow::RequestClose()
{
    if (!bCloseRequested)
    {
        bCloseRequested = true;
        CancelInput();
        PushEvent(EPlatformEventType::CloseRequested);
    }
}

LRESULT CALLBACK FWindowsPlatformWindow::WndProc(const HWND WindowHandle, const UINT Message, const WPARAM WParam, const LPARAM LParam)
{
    FWindowsPlatformWindow* Window = reinterpret_cast<FWindowsPlatformWindow*>(GetWindowLongPtrA(WindowHandle, GWLP_USERDATA));
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
        if (Window != nullptr)
        {
            Window->RequestClose();
        }
        return 0;

    case WM_DESTROY:
        if (Window != nullptr)
        {
            Window->RequestClose();
            Window->UnregisterRawMouse();
        }
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

            if (Window->bMinimized || Window->Width == 0 || Window->Height == 0)
            {
                Window->CancelInput();
            }
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

    case WM_SETFOCUS:
        if (Window != nullptr)
        {
            Window->bHasFocus = true;
        }
        break;

    case WM_KILLFOCUS:
        if (Window != nullptr)
        {
            Window->bHasFocus = false;
            Window->CancelInput();
        }
        break;

    case WM_CANCELMODE:
        if (Window != nullptr)
        {
            Window->CancelInput();
        }
        break;

    case WM_KEYDOWN:
        if (Window != nullptr)
        {
            Window->HandleKey(EPlatformEventType::KeyPressed, WParam, LParam);
        }
        break;

    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (Window != nullptr)
        {
            Window->HandleKey(EPlatformEventType::KeyReleased, WParam, LParam);
        }
        break;

    case WM_RBUTTONDOWN:
        if (Window != nullptr)
        {
            Window->BeginMouseCapture();
        }
        break;

    case WM_RBUTTONUP:
        if (Window != nullptr)
        {
            Window->EndMouseCapture();
        }
        break;

    case WM_CAPTURECHANGED:
        if (Window != nullptr && Window->bMouseCaptured)
        {
            Window->CancelInput();
        }
        break;

    case WM_INPUT:
        if (GET_RAWINPUT_CODE_WPARAM(WParam) == RIM_INPUT)
        {
            if (Window != nullptr)
            {
                Window->HandleRawMouse(reinterpret_cast<HRAWINPUT>(LParam));
            }
            // DefWindowProc performs foreground raw-input cleanup.
            break;
        }
        return 0;

    default:
        break;
    }
    return DefWindowProcA(WindowHandle, Message, WParam, LParam);
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
