#pragma once

#include "Math/Matrix.h"
#include "Math/Quaternion.h"
#include "Platform/PlatformEvents.h"

#include <algorithm>
#include <cmath>

namespace LE
{

class FDemoCameraNavigation
{
public:
    void Start(const Math::Matrix4f& AuthoredCameraToWorld) noexcept { CameraToWorld = AuthoredCameraToWorld; }

    const Math::Matrix4f& GetCameraToWorld() const noexcept { return CameraToWorld; }

    bool Update(const FPlatformInputFrame& Input, const double DeltaSeconds) noexcept
    {
        const float ForwardAxis = ToAxis(Input, EPlatformKey::W, EPlatformKey::S);
        const float RightAxis = ToAxis(Input, EPlatformKey::D, EPlatformKey::A);
        const bool bLook = Input.MouseDeltaX != 0 || Input.MouseDeltaY != 0;
        const bool bMove = (ForwardAxis != 0.0f || RightAxis != 0.0f) && std::isfinite(DeltaSeconds) && DeltaSeconds > 0.0;
        if (!bLook && !bMove)
        {
            return false;
        }

        if (bLook)
        {
            Look(Input.MouseDeltaX, Input.MouseDeltaY);
        }
        if (bMove)
        {
            const Math::Vector3f Direction = (Column(2) * -ForwardAxis + Column(0) * RightAxis).Normalized();
            const float Step = MoveSpeed * static_cast<float>((std::min)(DeltaSeconds, MaxMoveSeconds));
            SetColumn(3, Column(3) + Direction * Step);
        }
        return true;
    }

private:
    static float ToAxis(const FPlatformInputFrame& Input, const EPlatformKey Positive, const EPlatformKey Negative) noexcept { return static_cast<float>(Input.IsKeyDown(Positive)) - static_cast<float>(Input.IsKeyDown(Negative)); }

    Math::Vector3f Column(const std::size_t Index) const noexcept { return Math::Vector3f(CameraToWorld(0, Index), CameraToWorld(1, Index), CameraToWorld(2, Index)); }

    void SetColumn(const std::size_t Index, const Math::Vector3f& Value) noexcept
    {
        CameraToWorld(0, Index) = Value[0];
        CameraToWorld(1, Index) = Value[1];
        CameraToWorld(2, Index) = Value[2];
    }

    void Look(const int64 DeltaX, const int64 DeltaY) noexcept
    {
        constexpr float PitchLimit = 1.55334306f; // 89 degrees
        constexpr double FullTurn = 6.283185307179586;
        const Math::Vector3f WorldUp(0.0f, 1.0f, 0.0f);
        const float YawAngle = static_cast<float>(std::remainder(-MouseSensitivity * DeltaX, FullTurn));
        const auto Yaw = Math::Quaternionf::FromAxisAngle(WorldUp, YawAngle);
        Math::Vector3f Back = Yaw.RotateVector(Column(2)).Normalized();
        Math::Vector3f Right = Yaw.RotateVector(Column(0));
        if (DeltaY != 0)
        {
            const float CurrentPitch = std::asin(std::clamp(-Back[1], -1.0f, 1.0f));
            const float TargetPitch = std::clamp(CurrentPitch - static_cast<float>(MouseSensitivity * DeltaY), -PitchLimit, PitchLimit);
            Math::Vector3f PitchAxis = Math::Vector3f::Cross(WorldUp, Back).Normalized();
            if (PitchAxis.LengthSquared() == 0.0f)
            {
                // At a vertical authored pose, the camera's right axis defines the heading.
                PitchAxis = Right.Normalized();
            }
            const auto Pitch = Math::Quaternionf::FromAxisAngle(PitchAxis, TargetPitch - CurrentPitch);
            Back = Pitch.RotateVector(Back).Normalized();
            Right = Pitch.RotateVector(Right);
        }

        // Rebuild the basis to prevent rounding drift over successive input frames.
        const Math::Vector3f Up = Math::Vector3f::Cross(Back, Right).Normalized();
        SetColumn(0, Math::Vector3f::Cross(Up, Back));
        SetColumn(1, Up);
        SetColumn(2, Back);
    }

    static constexpr float MoveSpeed = 3.0f;
    static constexpr double MaxMoveSeconds = 0.05;
    static constexpr double MouseSensitivity = 0.0025;
    Math::Matrix4f CameraToWorld = Math::Matrix4f::Identity();
};

} // namespace LE
