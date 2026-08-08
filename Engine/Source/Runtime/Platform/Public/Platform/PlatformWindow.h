#pragma once

#include "Containers/StringView.h"
#include "Memory/UniquePtr.h"
#include "Platform/PlatformEvents.h"
#include "Platform/PlatformSurface.h"
#include "Types/EngineTypes.h"

namespace LE
{

struct FPlatformWindowDesc
{
    uint32 Width = 1280;
    uint32 Height = 720;
    LE::StringView Title = "Limitless Engine";
};

struct FPlatformWindowSize
{
    uint32 Width = 0;
    uint32 Height = 0;
};

class PLATFORM_API FPlatformWindow
{
public:
    virtual ~FPlatformWindow() = default;

    FPlatformWindowId GetId() const noexcept { return WindowId; }

    virtual bool IsValid() const noexcept = 0;
    virtual FPlatformSurface GetSurface() const noexcept = 0;
    virtual void PumpEvents(FPlatformEventQueue& OutEvents) = 0;
    virtual FPlatformWindowSize GetSize() const noexcept = 0;
    virtual bool IsMinimized() const noexcept = 0;
    virtual bool IsCloseRequested() const noexcept = 0;

protected:
    FPlatformWindow() noexcept;

private:
    FPlatformWindowId WindowId = InvalidPlatformWindowId;
};

using FPlatformWindowPtr = LE::UniquePtr<FPlatformWindow>;

// The returned pointer owns its native resources. Allocation failure or any
// native initialization failure returns an empty pointer after cleanup inside
// the Platform module.
PLATFORM_API FPlatformWindowPtr CreatePlatformWindow(const FPlatformWindowDesc& Description);

} // namespace LE
