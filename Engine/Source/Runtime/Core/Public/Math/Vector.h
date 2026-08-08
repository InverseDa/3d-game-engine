#pragma once

#include "Memory/Allocator.h"

#include <cmath>
#include <cstddef>
#include <type_traits>

namespace LE::Math
{

template <typename T>
constexpr T DefaultEpsilon() noexcept
{
    static_assert(std::is_floating_point<T>::value, "LE::Math scalar must be floating point");
    return std::is_same<T, float>::value ? static_cast<T>(1.0e-6f) : static_cast<T>(1.0e-12);
}

template <typename T, std::size_t N>
struct Vector
{
    static_assert(std::is_floating_point<T>::value, "Vector scalar must be float or double");
    static_assert(N > 0, "Vector dimension must be non-zero");

    using Scalar = T;
    static constexpr std::size_t Dimension = N;

    T Values[N]{};

    constexpr Vector() noexcept = default;

    explicit constexpr Vector(const T Value) noexcept
    {
        for (std::size_t Index = 0; Index < N; ++Index)
        {
            Values[Index] = Value;
        }
    }

    template <typename... Args,
        typename std::enable_if<sizeof...(Args) == N, int>::type = 0>
    explicit constexpr Vector(Args... Components) noexcept
        : Values{static_cast<T>(Components)...}
    {
    }

    T* Data() noexcept { return Values; }
    const T* Data() const noexcept { return Values; }

    T& operator[](const std::size_t Index)
    {
        ValidateIndex(Index);
        return Values[Index];
    }

    const T& operator[](const std::size_t Index) const
    {
        ValidateIndex(Index);
        return Values[Index];
    }

    Vector& operator+=(const Vector& Other) noexcept
    {
        for (std::size_t Index = 0; Index < N; ++Index)
        {
            Values[Index] += Other.Values[Index];
        }
        return *this;
    }

    Vector& operator-=(const Vector& Other) noexcept
    {
        for (std::size_t Index = 0; Index < N; ++Index)
        {
            Values[Index] -= Other.Values[Index];
        }
        return *this;
    }

    Vector& operator*=(const T ScalarValue) noexcept
    {
        for (std::size_t Index = 0; Index < N; ++Index)
        {
            Values[Index] *= ScalarValue;
        }
        return *this;
    }

    Vector& operator/=(const T ScalarValue)
    {
        if (ScalarValue == static_cast<T>(0))
        {
            HandleContractViolation("cannot divide a vector by zero", __FILE__, __LINE__);
        }
        const T Inverse = static_cast<T>(1) / ScalarValue;
        return *this *= Inverse;
    }

    // Component-wise multiplication is retained for compatibility with the old
    // Core vector. Dot and Cross make geometric products explicit.
    Vector& operator*=(const Vector& Other) noexcept
    {
        for (std::size_t Index = 0; Index < N; ++Index)
        {
            Values[Index] *= Other.Values[Index];
        }
        return *this;
    }

    T LengthSquared() const noexcept { return Dot(*this, *this); }
    T SizeSqr() const noexcept { return LengthSquared(); }
    T Length() const noexcept { return std::sqrt(LengthSquared()); }
    T Size() const noexcept { return Length(); }

    Vector Normalized(const T Epsilon = DefaultEpsilon<T>()) const noexcept
    {
        const T LengthValue = Length();
        if (LengthValue <= Epsilon)
        {
            return Vector{};
        }
        return *this * (static_cast<T>(1) / LengthValue);
    }

    bool Normalize(const T Epsilon = DefaultEpsilon<T>()) noexcept
    {
        const T LengthValue = Length();
        if (LengthValue <= Epsilon)
        {
            *this = Vector{};
            return false;
        }
        *this *= static_cast<T>(1) / LengthValue;
        return true;
    }

    static T Dot(const Vector& Left, const Vector& Right) noexcept
    {
        T Result = static_cast<T>(0);
        for (std::size_t Index = 0; Index < N; ++Index)
        {
            Result += Left.Values[Index] * Right.Values[Index];
        }
        return Result;
    }

    static Vector Cross(const Vector& Left, const Vector& Right) noexcept
    {
        static_assert(N == 3, "Cross is defined only for three-dimensional vectors");
        return Vector(
            Left.Values[1] * Right.Values[2] - Left.Values[2] * Right.Values[1],
            Left.Values[2] * Right.Values[0] - Left.Values[0] * Right.Values[2],
            Left.Values[0] * Right.Values[1] - Left.Values[1] * Right.Values[0]);
    }

    static T Cross2D(const Vector& Left, const Vector& Right) noexcept
    {
        static_assert(N == 2, "Cross2D is defined only for two-dimensional vectors");
        return Left.Values[0] * Right.Values[1] - Left.Values[1] * Right.Values[0];
    }

    Vector PerpendicularClockwise() const noexcept
    {
        static_assert(N == 2, "PerpendicularClockwise is defined only for two-dimensional vectors");
        return Vector(Values[1], -Values[0]);
    }

    Vector PerpendicularCounterClockwise() const noexcept
    {
        static_assert(N == 2, "PerpendicularCounterClockwise is defined only for two-dimensional vectors");
        return Vector(-Values[1], Values[0]);
    }

    // Compatibility spellings. The old PerpCCW implementation accidentally
    // returned the clockwise result; C4 corrects that mathematical bug.
    Vector Perp() const noexcept { return PerpendicularClockwise(); }
    Vector PerpCCW() const noexcept { return PerpendicularCounterClockwise(); }

private:
    static void ValidateIndex(const std::size_t Index)
    {
        if (Index >= N)
        {
            HandleContractViolation("vector index is out of bounds", __FILE__, __LINE__);
        }
    }
};

template <typename T, std::size_t N>
Vector<T, N> operator+(Vector<T, N> Left, const Vector<T, N>& Right) noexcept
{
    return Left += Right;
}

template <typename T, std::size_t N>
Vector<T, N> operator-(Vector<T, N> Left, const Vector<T, N>& Right) noexcept
{
    return Left -= Right;
}

template <typename T, std::size_t N>
Vector<T, N> operator-(Vector<T, N> Value) noexcept
{
    return Value *= static_cast<T>(-1);
}

template <typename T, std::size_t N>
Vector<T, N> operator*(Vector<T, N> Value, const T ScalarValue) noexcept
{
    return Value *= ScalarValue;
}

template <typename T, std::size_t N>
Vector<T, N> operator*(const T ScalarValue, Vector<T, N> Value) noexcept
{
    return Value *= ScalarValue;
}

template <typename T, std::size_t N>
Vector<T, N> operator*(Vector<T, N> Left, const Vector<T, N>& Right) noexcept
{
    return Left *= Right;
}

template <typename T, std::size_t N>
Vector<T, N> operator/(Vector<T, N> Value, const T ScalarValue)
{
    return Value /= ScalarValue;
}

template <typename T, std::size_t N>
bool operator==(const Vector<T, N>& Left, const Vector<T, N>& Right) noexcept
{
    for (std::size_t Index = 0; Index < N; ++Index)
    {
        if (Left.Values[Index] != Right.Values[Index])
        {
            return false;
        }
    }
    return true;
}

template <typename T, std::size_t N>
bool operator!=(const Vector<T, N>& Left, const Vector<T, N>& Right) noexcept
{
    return !(Left == Right);
}

using Vector2f = Vector<float, 2>;
using Vector3f = Vector<float, 3>;
using Vector4f = Vector<float, 4>;
using Vector2d = Vector<double, 2>;
using Vector3d = Vector<double, 3>;
using Vector4d = Vector<double, 4>;

} // namespace LE::Math
