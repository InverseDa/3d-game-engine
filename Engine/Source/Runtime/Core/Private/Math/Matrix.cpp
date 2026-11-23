#include "Math/Matrix.h"

#include <algorithm>
#include <cmath>

namespace LE::Math
{

bool TryBuildNormalMatrix(const Matrix4f& LocalToWorld, Matrix3f& Out) noexcept
{
    for (const float Value : LocalToWorld.Values)
    {
        if (!std::isfinite(Value))
        {
            return false;
        }
    }

    Vector3d Columns[3];
    for (std::size_t Column = 0; Column < 3; ++Column)
    {
        Columns[Column] = Vector3d(LocalToWorld(0, Column), LocalToWorld(1, Column), LocalToWorld(2, Column));
    }
    const Vector3d Cofactors[] = {
        Vector3d::Cross(Columns[1], Columns[2]),
        Vector3d::Cross(Columns[2], Columns[0]),
        Vector3d::Cross(Columns[0], Columns[1]),
    };
    const double Determinant = Vector3d::Dot(Columns[0], Cofactors[0]);
    const double ColumnScale = Columns[0].Length() * Columns[1].Length() * Columns[2].Length();
    if (!std::isfinite(Determinant) || std::fabs(Determinant) <= 1.0e-8 * ColumnScale)
    {
        return false;
    }

    double MaxCofactor = 0.0;
    for (const Vector3d& Cofactor : Cofactors)
    {
        for (const double Value : Cofactor.Values)
        {
            MaxCofactor = (std::max)(MaxCofactor, std::fabs(Value));
        }
    }
    // The result is normalized by its consumer. Keep the determinant sign
    // for mirrored transforms while bounding the float coefficients.
    const double Divisor = std::copysign(MaxCofactor, Determinant);
    Matrix3f Candidate;
    for (std::size_t Column = 0; Column < 3; ++Column)
    {
        for (std::size_t Row = 0; Row < 3; ++Row)
        {
            const double Coefficient = Cofactors[Column][Row] / Divisor;
            if (!std::isfinite(Coefficient))
            {
                return false;
            }
            Candidate(Row, Column) = static_cast<float>(Coefficient);
        }
    }
    Out = Candidate;
    return true;
}

bool TryInvertMatrix(const Matrix4f& Matrix, Matrix4f& Out) noexcept
{
    double Augmented[4][8]{};
    for (std::size_t Row = 0; Row < 4; ++Row)
    {
        for (std::size_t Column = 0; Column < 4; ++Column)
        {
            if (!std::isfinite(Matrix(Row, Column)))
            {
                return false;
            }
            Augmented[Row][Column] = Matrix(Row, Column);
        }
        Augmented[Row][Row + 4] = 1.0;
    }

    for (std::size_t Column = 0; Column < 4; ++Column)
    {
        std::size_t Pivot = Column;
        for (std::size_t Row = Column + 1; Row < 4; ++Row)
        {
            if (std::fabs(Augmented[Row][Column]) > std::fabs(Augmented[Pivot][Column]))
            {
                Pivot = Row;
            }
        }
        const double Divisor = Augmented[Pivot][Column];
        if (Divisor == 0.0)
        {
            return false;
        }
        if (Pivot != Column)
        {
            std::swap(Augmented[Column], Augmented[Pivot]);
        }
        for (double& Element : Augmented[Column])
        {
            Element /= Divisor;
        }
        for (std::size_t Row = 0; Row < 4; ++Row)
        {
            if (Row == Column)
            {
                continue;
            }
            const double Factor = Augmented[Row][Column];
            for (std::size_t Index = Column; Index < 8; ++Index)
            {
                Augmented[Row][Index] -= Factor * Augmented[Column][Index];
            }
        }
    }

    Matrix4f Candidate;
    for (std::size_t Row = 0; Row < 4; ++Row)
    {
        for (std::size_t Column = 0; Column < 4; ++Column)
        {
            const float Value = static_cast<float>(Augmented[Row][Column + 4]);
            if (!std::isfinite(Value))
            {
                return false;
            }
            Candidate(Row, Column) = Value;
        }
    }
    Out = Candidate;
    return true;
}

} // namespace LE::Math
