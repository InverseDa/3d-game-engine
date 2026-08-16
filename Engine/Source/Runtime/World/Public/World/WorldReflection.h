#pragma once

#include "Reflection/ReflectionRegistry.h"

namespace LE
{

// Explicit provider entry points. The registry borrows World thunk addresses,
// so UnregisterWorldReflection must run before unloading the World module.
WORLD_API EReflectionRegisterResult RegisterWorldReflection(
    FReflectionRegistry& Registry,
    FReflectionModuleHandle& OutHandle) noexcept;
WORLD_API EReflectionUnregisterResult UnregisterWorldReflection(
    FReflectionRegistry& Registry,
    FReflectionModuleHandle Handle) noexcept;

} // namespace LE
