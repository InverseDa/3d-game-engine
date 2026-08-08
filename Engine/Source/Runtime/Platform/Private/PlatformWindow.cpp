#include "Platform/PlatformWindow.h"

#include <atomic>

namespace LE
{

namespace
{
std::atomic<FPlatformWindowId> GNextPlatformWindowId { 1 };
}

FPlatformWindow::FPlatformWindow() noexcept
    : WindowId(GNextPlatformWindowId.fetch_add(1, std::memory_order_relaxed))
{
}

} // namespace LE
