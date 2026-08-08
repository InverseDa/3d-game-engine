#pragma once

#include "Types/EngineTypes.h"

namespace LE
{

class PLATFORM_API FPlatformTime final
{
public:
    // Seconds from an unspecified steady-clock epoch. Only differences are
    // meaningful; the value never follows wall-clock adjustments.
    static double Seconds();
    static void SleepForMilliseconds(uint32 Milliseconds);
};

} // namespace LE
