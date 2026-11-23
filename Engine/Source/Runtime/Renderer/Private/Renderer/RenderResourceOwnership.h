#pragma once

#include "Memory/UniquePtr.h"
#include "RHI/RHIDevice.h"

namespace LE
{

template <typename Resource> UniquePtr<Resource> OwnRHI(Resource* Value) noexcept
{
    return UniquePtr<Resource>::Adopt(Value, nullptr, [](void*, Resource* Object) noexcept { RHI::DestroyResource(Object); });
}

} // namespace LE
