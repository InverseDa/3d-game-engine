#include "World/DirectionalLight.h"

#include <cmath>

namespace LE
{

EDirectionalLightParameterError ValidateDirectionalLightParameters(const Math::Vector3f& Direction,
    const Math::Vector3f& Color, const float Intensity) noexcept
{
    if (!std::isfinite(Direction[0]) || !std::isfinite(Direction[1]) || !std::isfinite(Direction[2]) ||
        (Direction[0] == 0.0f && Direction[1] == 0.0f && Direction[2] == 0.0f))
    {
        return EDirectionalLightParameterError::Direction;
    }
    for (const float Channel : Color.Values)
    {
        if (!std::isfinite(Channel) || Channel < 0.0f || Channel > 1.0f)
        {
            return EDirectionalLightParameterError::Color;
        }
    }
    if (!std::isfinite(Intensity) || Intensity < 0.0f || Intensity > 16.0f)
    {
        return EDirectionalLightParameterError::Intensity;
    }
    return EDirectionalLightParameterError::None;
}

} // namespace LE
