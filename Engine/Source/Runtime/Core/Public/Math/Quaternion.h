#pragma once

#include "Math/Vector.h"

#include <cmath>
#include <cstddef>
#include <type_traits>

namespace LE::Math
{

template <typename T>
struct Quaternion
{
    static_assert(std::is_floating_point<T>::value, "Quaternion scalar must be float or double");

    // Physical and indexed component order is x, y, z, w. The vector part is
    // first and the scalar part is last.
    T x = static_cast<T>(0);
    T y = static_cast<T>(0);
    T z = static_cast<T>(0);
    T w = static_cast<T>(1);

    constexpr Quaternion() noexcept = default;

    constexpr Quaternion(const T InX, const T InY, const T InZ, const T InW) noexcept
        : x(InX)
        , y(InY)
        , z(InZ)
        , w(InW)
    {
    }

    Quaternion(const Vector<T, 3>& Axis, const T AngleRadians) noexcept
        : Quaternion(FromAxisAngle(Axis, AngleRadians))
    {
    }

    static constexpr Quaternion Identity() noexcept { return Quaternion{}; }

    static Quaternion FromAxisAngle(
        const Vector<T, 3>& Axis,
        const T AngleRadians,
        const T Epsilon = DefaultEpsilon<T>()) noexcept
    {
        const Vector<T, 3> UnitAxis = Axis.Normalized(Epsilon);
        if (UnitAxis.LengthSquared() <= Epsilon * Epsilon)
        {
            return Identity();
        }

        const T HalfAngle = AngleRadians * static_cast<T>(0.5);
        const T SinHalfAngle = std::sin(HalfAngle);
        return Quaternion(
            UnitAxis.Values[0] * SinHalfAngle,
            UnitAxis.Values[1] * SinHalfAngle,
            UnitAxis.Values[2] * SinHalfAngle,
            std::cos(HalfAngle));
    }

    T& operator[](const std::size_t Index)
    {
        ValidateIndex(Index);
        switch (Index)
        {
        case 0: return x;
        case 1: return y;
        case 2: return z;
        default: return w;
        }
    }

    const T& operator[](const std::size_t Index) const
    {
        ValidateIndex(Index);
        switch (Index)
        {
        case 0: return x;
        case 1: return y;
        case 2: return z;
        default: return w;
        }
    }

    T LengthSquared() const noexcept { return x * x + y * y + z * z + w * w; }
    T Length() const noexcept { return std::sqrt(LengthSquared()); }

    Quaternion Conjugated() const noexcept { return Quaternion(-x, -y, -z, w); }

    Quaternion Normalized(const T Epsilon = DefaultEpsilon<T>()) const noexcept
    {
        const T LengthValue = Length();
        if (LengthValue <= Epsilon)
        {
            return Identity();
        }
        const T InverseLength = static_cast<T>(1) / LengthValue;
        return Quaternion(x * InverseLength, y * InverseLength, z * InverseLength, w * InverseLength);
    }

    bool Normalize(const T Epsilon = DefaultEpsilon<T>()) noexcept
    {
        const T LengthValue = Length();
        if (LengthValue <= Epsilon)
        {
            *this = Identity();
            return false;
        }
        const T InverseLength = static_cast<T>(1) / LengthValue;
        x *= InverseLength;
        y *= InverseLength;
        z *= InverseLength;
        w *= InverseLength;
        return true;
    }

    Quaternion Inverse(const T Epsilon = DefaultEpsilon<T>()) const noexcept
    {
        const T NormSquared = LengthSquared();
        if (NormSquared <= Epsilon * Epsilon)
        {
            return Identity();
        }
        const T InverseNormSquared = static_cast<T>(1) / NormSquared;
        return Quaternion(
            -x * InverseNormSquared,
            -y * InverseNormSquared,
            -z * InverseNormSquared,
            w * InverseNormSquared);
    }

    // Hamilton product. With column-vector transforms, Left * Right applies
    // Right first and Left second.
    Quaternion operator*(const Quaternion& Other) const noexcept
    {
        return Quaternion(
            w * Other.x + x * Other.w + y * Other.z - z * Other.y,
            w * Other.y - x * Other.z + y * Other.w + z * Other.x,
            w * Other.z + x * Other.y - y * Other.x + z * Other.w,
            w * Other.w - x * Other.x - y * Other.y - z * Other.z);
    }

    Vector<T, 3> RotateVector(const Vector<T, 3>& Value) const noexcept
    {
        const Quaternion Rotation = Normalized();
        const Vector<T, 3> QuaternionVector(Rotation.x, Rotation.y, Rotation.z);
        const Vector<T, 3> TwiceCross = static_cast<T>(2)
            * Vector<T, 3>::Cross(QuaternionVector, Value);
        return Value
            + Rotation.w * TwiceCross
            + Vector<T, 3>::Cross(QuaternionVector, TwiceCross);
    }

    Vector<T, 3> operator*(const Vector<T, 3>& Value) const noexcept
    {
        return RotateVector(Value);
    }

private:
    static void ValidateIndex(const std::size_t Index)
    {
        if (Index >= 4)
        {
            HandleContractViolation("quaternion index is out of bounds", __FILE__, __LINE__);
        }
    }
};

template <typename T>
bool operator==(const Quaternion<T>& Left, const Quaternion<T>& Right) noexcept
{
    return Left.x == Right.x && Left.y == Right.y && Left.z == Right.z && Left.w == Right.w;
}

template <typename T>
bool operator!=(const Quaternion<T>& Left, const Quaternion<T>& Right) noexcept
{
    return !(Left == Right);
}

using Quaternionf = Quaternion<float>;
using Quaterniond = Quaternion<double>;

} // namespace LE::Math
