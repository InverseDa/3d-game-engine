#include "Platform/PlatformTime.h"

#include <chrono>
#include <thread>

namespace LE
{

double FPlatformTime::Seconds()
{
    using FClock = std::chrono::steady_clock;
    return std::chrono::duration<double>(FClock::now().time_since_epoch()).count();
}

void FPlatformTime::SleepForMilliseconds(const uint32 Milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(Milliseconds));
}

} // namespace LE
