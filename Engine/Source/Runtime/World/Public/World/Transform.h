#pragma once

#include "CoreMinimal.h"

#include <cmath>
#include <type_traits>

namespace LE
{

// Authored local transform. Matrices use Core's row-major storage and column
// vectors, so the local matrix is composed as Translation * Rotation * Scale.
struct FTransform
{
    Math::Vector3f Translation{};
    Math::Quaternionf Rotation = Math::Quaternionf::Identity();
    Math::Vector3f Scale{1.0f};

    Math::Quaternionf NormalizedRotation() const noexcept
    {
        float Maximum = std::fabs(Rotation.x);
        if (std::fabs(Rotation.y) > Maximum)
        {
            Maximum = std::fabs(Rotation.y);
        }
        if (std::fabs(Rotation.z) > Maximum)
        {
            Maximum = std::fabs(Rotation.z);
        }
        if (std::fabs(Rotation.w) > Maximum)
        {
            Maximum = std::fabs(Rotation.w);
        }
        if (Maximum == 0.0f)
        {
            return Math::Quaternionf::Identity();
        }

        const float X = Rotation.x / Maximum;
        const float Y = Rotation.y / Maximum;
        const float Z = Rotation.z / Maximum;
        const float W = Rotation.w / Maximum;
        const float InverseLength = 1.0f / std::sqrt(X * X + Y * Y + Z * Z + W * W);
        return Math::Quaternionf(X * InverseLength, Y * InverseLength, Z * InverseLength, W * InverseLength);
    }

    Math::Matrix4f ToMatrix() const noexcept
    {
        const Math::Quaternionf UnitRotation = NormalizedRotation();
        const float x = UnitRotation.x;
        const float y = UnitRotation.y;
        const float z = UnitRotation.z;
        const float w = UnitRotation.w;
        const float xx = x * x;
        const float yy = y * y;
        const float zz = z * z;
        const float xy = x * y;
        const float xz = x * z;
        const float yz = y * z;
        const float xw = x * w;
        const float yw = y * w;
        const float zw = z * w;

        Math::Matrix4f Result = Math::Matrix4f::Identity();
        Result(0, 0) = (1.0f - 2.0f * (yy + zz)) * Scale[0];
        Result(0, 1) = (2.0f * (xy - zw)) * Scale[1];
        Result(0, 2) = (2.0f * (xz + yw)) * Scale[2];
        Result(1, 0) = (2.0f * (xy + zw)) * Scale[0];
        Result(1, 1) = (1.0f - 2.0f * (xx + zz)) * Scale[1];
        Result(1, 2) = (2.0f * (yz - xw)) * Scale[2];
        Result(2, 0) = (2.0f * (xz - yw)) * Scale[0];
        Result(2, 1) = (2.0f * (yz + xw)) * Scale[1];
        Result(2, 2) = (1.0f - 2.0f * (xx + yy)) * Scale[2];
        Result(0, 3) = Translation[0];
        Result(1, 3) = Translation[1];
        Result(2, 3) = Translation[2];
        return Result;
    }

    friend bool operator==(const FTransform& Left, const FTransform& Right) noexcept { return Left.Translation == Right.Translation && Left.Rotation == Right.Rotation && Left.Scale == Right.Scale; }

    friend bool operator!=(const FTransform& Left, const FTransform& Right) noexcept { return !(Left == Right); }
};

enum class ETransformUpdateResult : uint8
{
    Updated = 0,
    Unchanged,
    InvalidEntity,
    MissingTransform,
};

enum class ETransformParentResult : uint8
{
    Reparented = 0,
    Unchanged,
    InvalidEntity,
    MissingTransform,
    MissingParentTransform,
    CycleRejected,
};

static_assert(std::is_standard_layout<FTransform>::value, "FTransform must remain standard-layout across the World API");
static_assert(std::is_trivially_copyable<FTransform>::value, "FTransform must remain trivially copyable across the World API");

} // namespace LE
