#pragma once

#include "Containers/Array.h"
#include "Types/EngineTypes.h"

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
};

struct FPlatformEvent
{
    EPlatformEventType Type = EPlatformEventType::CloseRequested;
    FPlatformWindowId WindowId = InvalidPlatformWindowId;
    uint32 Width = 0;
    uint32 Height = 0;
};

// A small FIFO value queue. Events carry a stable WindowId instead of a
// borrowed window pointer, so a drained event remains safe after destruction.
class FPlatformEventQueue
{
public:
    bool IsEmpty() const noexcept { return Events.IsEmpty(); }
    std::size_t Size() const noexcept { return Events.Size(); }

    bool TryPush(const FPlatformEvent& Event) { return Events.TryPushBack(Event); }
    void Push(const FPlatformEvent& Event) { Events.PushBack(Event); }

    bool Poll(FPlatformEvent& OutEvent)
    {
        if (Events.IsEmpty())
        {
            return false;
        }

        OutEvent = Events[0];
        Events.Erase(0);
        return true;
    }

    void Clear() noexcept { Events.Clear(); }

private:
    LE::Array<FPlatformEvent> Events;
};

} // namespace LE
