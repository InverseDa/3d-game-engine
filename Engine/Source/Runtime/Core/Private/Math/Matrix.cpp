#include "Math/Matrix.h"

#include <algorithm>
#include <cmath>

namespace LE::Math
{

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
