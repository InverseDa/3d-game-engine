#pragma once

#include "Math/Vector.h"

#include <cstddef>
#include <type_traits>

namespace LE::Math
{

template <typename T, std::size_t R, std::size_t C>
struct Matrix
{
    static_assert(std::is_floating_point<T>::value, "Matrix scalar must be float or double");
    static_assert(R > 0 && C > 0, "Matrix dimensions must be non-zero");

    using Scalar = T;
    static constexpr std::size_t RowCount = R;
    static constexpr std::size_t ColumnCount = C;

    // Row-major physical storage. Mathematical operations use column vectors
    // on the right, so Result = MatrixValue * VectorValue.
    T Values[R * C]{};

    constexpr Matrix() noexcept = default;

    explicit constexpr Matrix(const T Diagonal) noexcept
    {
        const std::size_t DiagonalCount = R < C ? R : C;
        for (std::size_t Index = 0; Index < DiagonalCount; ++Index)
        {
            Values[Index * C + Index] = Diagonal;
        }
    }

    explicit Matrix(const T* const RowMajorValues)
    {
        if (RowMajorValues == nullptr)
        {
            HandleContractViolation("matrix source pointer cannot be null", __FILE__, __LINE__);
        }
        for (std::size_t Index = 0; Index < R * C; ++Index)
        {
            Values[Index] = RowMajorValues[Index];
        }
    }

    template <typename... Args,
        typename std::enable_if<(sizeof...(Args) == R * C) && (R * C != 1), int>::type = 0>
    explicit constexpr Matrix(Args... Components) noexcept
        : Values{static_cast<T>(Components)...}
    {
    }

    static constexpr Matrix Zero() noexcept { return Matrix{}; }

    static constexpr Matrix Identity() noexcept
    {
        static_assert(R == C, "Identity is defined only for square matrices");
        return Matrix(static_cast<T>(1));
    }

    void SetIdentity(const T Diagonal = static_cast<T>(1)) noexcept
    {
        static_assert(R == C, "SetIdentity is defined only for square matrices");
        *this = Matrix(Diagonal);
    }

    T* Data() noexcept { return Values; }
    const T* Data() const noexcept { return Values; }

    T& operator()(const std::size_t Row, const std::size_t Column)
    {
        ValidateIndices(Row, Column);
        return Values[Row * C + Column];
    }

    const T& operator()(const std::size_t Row, const std::size_t Column) const
    {
        ValidateIndices(Row, Column);
        return Values[Row * C + Column];
    }

    T* operator[](const std::size_t Row)
    {
        ValidateRow(Row);
        return Values + Row * C;
    }

    const T* operator[](const std::size_t Row) const
    {
        ValidateRow(Row);
        return Values + Row * C;
    }

    Matrix<T, C, R> Transposed() const noexcept
    {
        Matrix<T, C, R> Result;
        for (std::size_t Row = 0; Row < R; ++Row)
        {
            for (std::size_t Column = 0; Column < C; ++Column)
            {
                Result.Values[Column * R + Row] = Values[Row * C + Column];
            }
        }
        return Result;
    }

    template <std::size_t OtherColumns>
    Matrix<T, R, OtherColumns> operator*(const Matrix<T, C, OtherColumns>& Other) const noexcept
    {
        Matrix<T, R, OtherColumns> Result;
        for (std::size_t Row = 0; Row < R; ++Row)
        {
            for (std::size_t Column = 0; Column < OtherColumns; ++Column)
            {
                T Sum = static_cast<T>(0);
                for (std::size_t Inner = 0; Inner < C; ++Inner)
                {
                    Sum += Values[Row * C + Inner]
                        * Other.Values[Inner * OtherColumns + Column];
                }
                Result.Values[Row * OtherColumns + Column] = Sum;
            }
        }
        return Result;
    }

    Vector<T, R> operator*(const Vector<T, C>& Value) const noexcept
    {
        Vector<T, R> Result;
        for (std::size_t Row = 0; Row < R; ++Row)
        {
            T Sum = static_cast<T>(0);
            for (std::size_t Column = 0; Column < C; ++Column)
            {
                Sum += Values[Row * C + Column] * Value.Values[Column];
            }
            Result.Values[Row] = Sum;
        }
        return Result;
    }

    Matrix& operator*=(const T ScalarValue) noexcept
    {
        for (T& Value : Values)
        {
            Value *= ScalarValue;
        }
        return *this;
    }

    Matrix operator*(const T ScalarValue) const noexcept
    {
        Matrix Result(*this);
        return Result *= ScalarValue;
    }

private:
    static void ValidateRow(const std::size_t Row)
    {
        if (Row >= R)
        {
            HandleContractViolation("matrix row is out of bounds", __FILE__, __LINE__);
        }
    }

    static void ValidateIndices(const std::size_t Row, const std::size_t Column)
    {
        if (Row >= R || Column >= C)
        {
            HandleContractViolation("matrix index is out of bounds", __FILE__, __LINE__);
        }
    }
};

template <typename T, std::size_t R, std::size_t C>
Matrix<T, R, C> operator*(const T ScalarValue, Matrix<T, R, C> Value) noexcept
{
    return Value *= ScalarValue;
}

template <typename T, std::size_t R, std::size_t C>
bool operator==(const Matrix<T, R, C>& Left, const Matrix<T, R, C>& Right) noexcept
{
    for (std::size_t Index = 0; Index < R * C; ++Index)
    {
        if (Left.Values[Index] != Right.Values[Index])
        {
            return false;
        }
    }
    return true;
}

template <typename T, std::size_t R, std::size_t C>
bool operator!=(const Matrix<T, R, C>& Left, const Matrix<T, R, C>& Right) noexcept
{
    return !(Left == Right);
}

using Matrix2f = Matrix<float, 2, 2>;
using Matrix3f = Matrix<float, 3, 3>;
using Matrix4f = Matrix<float, 4, 4>;
using Matrix2d = Matrix<double, 2, 2>;
using Matrix3d = Matrix<double, 3, 3>;
using Matrix4d = Matrix<double, 4, 4>;

} // namespace LE::Math
