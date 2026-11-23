#include "Platform/PlatformEvents.h"

#include <cstdio>
#include <utility>

namespace LE
{
namespace
{

bool ExpectInput(const bool bCondition, const char* const Label)
{
    if (!bCondition)
    {
        std::fprintf(stderr, "FAILED: Platform input: %s\n", Label);
    }
    return bCondition;
}

bool TestWindowInput()
{
    constexpr FPlatformWindowId WindowId = 1;
    constexpr FPlatformWindowId OtherWindowId = 2;
    FPlatformWindowInput Input;
    const FPlatformInputFrame& Frame = Input.GetFrame();
    const FPlatformEvent KeyDown{EPlatformEventType::KeyPressed, WindowId, 0, 0, EPlatformKey::W};
    const FPlatformEvent KeyUp{EPlatformEventType::KeyReleased, WindowId, 0, 0, EPlatformKey::W};
    FPlatformEvent Repeat = KeyDown;
    Repeat.bRepeat = true;
    const FPlatformEvent StartCapture{EPlatformEventType::MouseCaptureStarted, WindowId};
    const FPlatformEvent EndCapture{EPlatformEventType::MouseCaptureEnded, WindowId};
    const FPlatformEvent Cancel{EPlatformEventType::InputCanceled, WindowId};
    FPlatformEvent Motion{EPlatformEventType::MouseMoved, WindowId};
    Motion.MouseDeltaX = 16;
    Motion.MouseDeltaY = -8;

    Input.BeginFrame();
    Input.Apply(WindowId, {EPlatformEventType::KeyPressed, OtherWindowId, 0, 0, EPlatformKey::A});
    Input.Apply(WindowId, KeyDown);
    Input.Apply(WindowId, KeyUp);
    bool bSuccess = ExpectInput(!Frame.IsKeyDown(EPlatformKey::A) && !Frame.IsKeyDown(EPlatformKey::W) && Frame.WasKeyPressed(EPlatformKey::W) && Frame.WasKeyReleased(EPlatformKey::W), "same-frame press and release preserve both edges and other windows are ignored");

    Input.BeginFrame();
    Input.Apply(WindowId, KeyDown);
    bSuccess &= ExpectInput(Frame.IsKeyDown(EPlatformKey::W) && Frame.WasKeyPressed(EPlatformKey::W), "fresh press starts a hold");
    Input.BeginFrame();
    Input.Apply(WindowId, Repeat);
    Input.Apply(WindowId, {EPlatformEventType::KeyReleased, OtherWindowId, 0, 0, EPlatformKey::W});
    bSuccess &= ExpectInput(Frame.IsKeyDown(EPlatformKey::W) && !Frame.WasKeyPressed(EPlatformKey::W) && !Frame.WasKeyReleased(EPlatformKey::W), "cross-frame hold and repeat have no extra edges");

    Input.Apply(WindowId, Motion);
    Input.Apply(WindowId, StartCapture);
    Input.Apply(WindowId, Motion);
    Input.Apply(WindowId, Motion);
    Input.Apply(WindowId, EndCapture);
    Input.Apply(WindowId, Motion);
    bSuccess &= ExpectInput(!Frame.bMouseCaptured && Frame.IsKeyDown(EPlatformKey::W) && Frame.MouseDeltaX == 32 && Frame.MouseDeltaY == -16, "normal release preserves motion accrued during capture and keyboard hold");
    Input.BeginFrame();
    bSuccess &= ExpectInput(Frame.MouseDeltaX == 0 && Frame.MouseDeltaY == 0, "motion is never carried to the next frame");

    Input.Apply(WindowId, StartCapture);
    Input.Apply(WindowId, Motion);
    Input.Apply(WindowId, {EPlatformEventType::InputCanceled, OtherWindowId});
    bSuccess &= ExpectInput(Frame.bMouseCaptured && Frame.IsKeyDown(EPlatformKey::W) && Frame.MouseDeltaX == 16, "other window cancellation cannot interrupt the active window");
    Input.Apply(WindowId, EndCapture);
    Input.Apply(WindowId, Cancel);
    Input.Apply(WindowId, Repeat);
    Input.Apply(WindowId, Motion);
    bSuccess &= ExpectInput(!Frame.bMouseCaptured && !Frame.IsKeyDown(EPlatformKey::W) && !Frame.WasKeyPressed(EPlatformKey::W) && Frame.MouseDeltaX == 0 && Frame.MouseDeltaY == 0, "cancellation clears same-frame motion and repeats cannot restore a canceled hold");
    Input.Apply(WindowId, KeyDown);
    Input.Apply(WindowId, StartCapture);
    Input.Apply(WindowId, Motion);
    Input.Apply(WindowId, Cancel);
    bSuccess &= ExpectInput(!Frame.bMouseCaptured && !Frame.IsKeyDown(EPlatformKey::W) && !Frame.WasKeyPressed(EPlatformKey::W) && Frame.MouseDeltaX == 0, "cancellation stops active capture and clears held state and key edges");
    Input.Apply(WindowId, KeyDown);
    bSuccess &= ExpectInput(Frame.IsKeyDown(EPlatformKey::W) && Frame.WasKeyPressed(EPlatformKey::W), "fresh key input works after cancellation");
    Input.Apply(WindowId, StartCapture);
    Input.Apply(WindowId, Motion);
    Input.ResetKeysAndMotion();
    Input.Apply(WindowId, Repeat);
    bSuccess &= ExpectInput(Frame.bMouseCaptured && !Frame.IsKeyDown(EPlatformKey::W) && Frame.MouseDeltaX == 0, "scene change clears pending input and keeps the native capture in sync");
    Input.Apply(WindowId, Motion);
    bSuccess &= ExpectInput(Frame.MouseDeltaX == 16, "new captured motion continues after scene change");
    return bSuccess;
}

bool TestInputQueue()
{
    FPlatformEventQueue Queue;
    for (int32 Index = 0; Index < 1024; ++Index)
    {
        FPlatformEvent Motion{EPlatformEventType::MouseMoved, 1};
        Motion.MouseDeltaX = Index;
        Queue.Push(Motion);
    }
    FPlatformEvent Event;
    bool bSuccess = ExpectInput(Queue.Poll(Event) && Event.MouseDeltaX == 0, "initial FIFO read");
    FPlatformEventQueue Moved = std::move(Queue);
    bSuccess &= ExpectInput(Queue.IsEmpty() && Moved.Size() == 1023, "moving a partially read queue retains unread events and empties the source");
    Moved.Push({EPlatformEventType::InputCanceled, 1});
    for (int32 Index = 1; Index < 1024; ++Index)
    {
        if (!ExpectInput(Moved.Poll(Event) && Event.Type == EPlatformEventType::MouseMoved && Event.MouseDeltaX == Index, "FIFO preserves motion order across compaction"))
        {
            return false;
        }
    }
    bSuccess &= ExpectInput(Moved.Poll(Event) && Event.Type == EPlatformEventType::InputCanceled && Moved.IsEmpty() && !Moved.Poll(Event), "cancellation follows preceding motion in the FIFO");
    return bSuccess;
}

bool TestReloadInput()
{
    constexpr FPlatformWindowId WindowId = 1;
    FPlatformWindowInput Input;
    const FPlatformInputFrame& Frame = Input.GetFrame();
    const FPlatformEvent Press{EPlatformEventType::KeyPressed, WindowId, 0, 0, EPlatformKey::F5};
    FPlatformEvent Repeat = Press;
    Repeat.bRepeat = true;

    Input.Apply(WindowId, Press);
    bool bSuccess = ExpectInput(Frame.WasKeyPressed(EPlatformKey::F5) && !Frame.WasKeyPressed(EPlatformKey::W), "F5 records a dedicated reload edge");
    Input.ResetKeysAndMotion();
    Input.BeginFrame();
    Input.Apply(WindowId, Repeat);
    bSuccess &= ExpectInput(!Frame.WasKeyPressed(EPlatformKey::F5), "holding F5 after scene publication does not request a second reload");
    Input.Apply(WindowId, {EPlatformEventType::KeyReleased, WindowId, 0, 0, EPlatformKey::F5});
    Input.Apply(WindowId, Press);
    bSuccess &= ExpectInput(Frame.WasKeyPressed(EPlatformKey::F5), "releasing and pressing F5 requests another reload");
    return bSuccess;
}

} // namespace

bool RunPlatformInputTests()
{
    const bool bWindowInput = TestWindowInput();
    const bool bEventQueue = TestInputQueue();
    const bool bReloadInput = TestReloadInput();
    return bWindowInput && bEventQueue && bReloadInput;
}

} // namespace LE
