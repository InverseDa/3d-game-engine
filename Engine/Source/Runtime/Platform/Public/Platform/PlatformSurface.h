#pragma once

#include "Types/EngineTypes.h"

namespace LE
{

enum class EPlatformSurfaceType : uint8
{
    Unknown = 0,
    Win32,
    MetalLayer,
};

// Opaque native handles needed to create a graphics surface. Consumers must
// branch on Type before interpreting a handle; no OS type crosses this API.
struct FPlatformSurface
{
    EPlatformSurfaceType Type = EPlatformSurfaceType::Unknown;
    void* WindowHandle = nullptr;
    void* ViewHandle = nullptr;
    void* LayerHandle = nullptr;

    bool IsValid() const noexcept
    {
        if (Type == EPlatformSurfaceType::Win32)
        {
            return WindowHandle != nullptr;
        }
        if (Type == EPlatformSurfaceType::MetalLayer)
        {
            return WindowHandle != nullptr && LayerHandle != nullptr;
        }
        return false;
    }
};

} // namespace LE
