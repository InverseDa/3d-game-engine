#pragma once

#include "Containers/Array.h"
#include "Types/EngineTypes.h"
#include <utility>

namespace LE
{

using FPlatformWindowId = uint64;
inline constexpr FPlatformWindowId InvalidPlatformWindowId = 0;

enum class EPlatformEventType : uint8
{
    CloseRequested = 0,
    Resized,
    Minimized,
    Restored,
    KeyPressed,
    KeyReleased,
    MouseCaptureStarted,
    MouseCaptureEnded,
    MouseMoved,
    InputCanceled,
};

enum class EPlatformKey : uint8
{
    Unknown = 0,
    W,
    A,
    S,
    D,
};

struct FPlatformEvent
{
    EPlatformEventType Type = EPlatformEventType::CloseRequested;
    FPlatformWindowId WindowId = InvalidPlatformWindowId;
    uint32 Width = 0;
    uint32 Height = 0;
    EPlatformKey Key = EPlatformKey::Unknown;
    int32 MouseDeltaX = 0;
    int32 MouseDeltaY = 0;
    bool bRepeat = false;
};

// A small FIFO value queue. Events carry a stable WindowId instead of a
// borrowed window pointer, so a drained event remains safe after destruction.
class FPlatformEventQueue
{
public:
    FPlatformEventQueue() = default;
    FPlatformEventQueue(const FPlatformEventQueue&) = default;
    FPlatformEventQueue& operator=(const FPlatformEventQueue&) = default;
    FPlatformEventQueue(FPlatformEventQueue&& Other) noexcept : Events(std::move(Other.Events)), ReadIndex(std::exchange(Other.ReadIndex, 0)) {}
    FPlatformEventQueue& operator=(FPlatformEventQueue&& Other) noexcept
    {
        if (this != &Other)
        {
            Events = std::move(Other.Events);
            ReadIndex = std::exchange(Other.ReadIndex, 0);
        }
        return *this;
    }

    bool IsEmpty() const noexcept { return ReadIndex == Events.Size(); }
    std::size_t Size() const noexcept { return Events.Size() - ReadIndex; }

    bool TryPush(const FPlatformEvent& Event) { return Events.TryPushBack(Event); }
    void Push(const FPlatformEvent& Event) { Events.PushBack(Event); }

    bool Poll(FPlatformEvent& OutEvent)
    {
        if (IsEmpty())
        {
            return false;
        }

        OutEvent = Events[ReadIndex++];
        if (ReadIndex >= Size())
        {
            // Compact only after consuming at least as many events as we move.
            Events.Erase(0, ReadIndex);
            ReadIndex = 0;
        }
        return true;
    }

    void Clear() noexcept
    {
        Events.Clear();
        ReadIndex = 0;
    }

private:
    LE::Array<FPlatformEvent> Events;
    std::size_t ReadIndex = 0;
};

struct FPlatformInputFrame
{
    bool IsKeyDown(EPlatformKey Key) const noexcept { return (DownKeys & KeyMask(Key)) != 0; }
    bool WasKeyPressed(EPlatformKey Key) const noexcept { return (PressedKeys & KeyMask(Key)) != 0; }
    bool WasKeyReleased(EPlatformKey Key) const noexcept { return (ReleasedKeys & KeyMask(Key)) != 0; }

    bool bMouseCaptured = false;
    int64 MouseDeltaX = 0;
    int64 MouseDeltaY = 0;

private:
    friend class FPlatformWindowInput;

    static uint8 KeyMask(EPlatformKey Key) noexcept
    {
        switch (Key)
        {
        case EPlatformKey::W:
            return 1 << 0;
        case EPlatformKey::A:
            return 1 << 1;
        case EPlatformKey::S:
            return 1 << 2;
        case EPlatformKey::D:
            return 1 << 3;
        default:
            return 0;
        }
    }

    uint8 DownKeys = 0;
    uint8 PressedKeys = 0;
    uint8 ReleasedKeys = 0;
};

// Reduces one window's ordered events into a frame value without native APIs.
class FPlatformWindowInput
{
public:
    void BeginFrame() noexcept
    {
        Frame.PressedKeys = 0;
        Frame.ReleasedKeys = 0;
        Frame.MouseDeltaX = 0;
        Frame.MouseDeltaY = 0;
    }

    void Apply(FPlatformWindowId WindowId, const FPlatformEvent& Event) noexcept
    {
        if (WindowId == InvalidPlatformWindowId || Event.WindowId != WindowId)
        {
            return;
        }

        switch (Event.Type)
        {
        case EPlatformEventType::KeyPressed:
            if (!Event.bRepeat && !Frame.IsKeyDown(Event.Key))
            {
                const uint8 Mask = FPlatformInputFrame::KeyMask(Event.Key);
                Frame.DownKeys |= Mask;
                Frame.PressedKeys |= Mask;
            }
            break;
        case EPlatformEventType::KeyReleased:
            if (Frame.IsKeyDown(Event.Key))
            {
                const uint8 Mask = FPlatformInputFrame::KeyMask(Event.Key);
                Frame.DownKeys &= static_cast<uint8>(~Mask);
                Frame.ReleasedKeys |= Mask;
            }
            break;
        case EPlatformEventType::MouseCaptureStarted:
            Frame.bMouseCaptured = true;
            break;
        case EPlatformEventType::MouseCaptureEnded:
            Frame.bMouseCaptured = false;
            break;
        case EPlatformEventType::MouseMoved:
            if (Frame.bMouseCaptured)
            {
                Frame.MouseDeltaX += Event.MouseDeltaX;
                Frame.MouseDeltaY += Event.MouseDeltaY;
            }
            break;
        case EPlatformEventType::InputCanceled:
            Reset();
            break;
        default:
            break;
        }
    }

    void Reset() noexcept { Frame = {}; }
    void ResetKeysAndMotion() noexcept
    {
        const bool bMouseCaptured = Frame.bMouseCaptured;
        Reset();
        Frame.bMouseCaptured = bMouseCaptured;
    }

    const FPlatformInputFrame& GetFrame() const noexcept { return Frame; }

private:
    FPlatformInputFrame Frame;
};

} // namespace LE
