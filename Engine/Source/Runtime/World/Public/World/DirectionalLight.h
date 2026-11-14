#pragma once

#include "Types/EngineTypes.h"

namespace LE
{

enum class EDirectionalLightParameterError : uint8
{
    None,
    Direction,
    Color,
    Intensity,
};

WORLD_API EDirectionalLightParameterError ValidateDirectionalLightParameters(
    const Math::Vector3f& Direction, const Math::Vector3f& Color, float Intensity) noexcept;

} // namespace LE
