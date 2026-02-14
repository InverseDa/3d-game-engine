#pragma once

#include <iosfwd>
#include <string>

#if LE_USE_GLM
#include "glm/glm.hpp"
#endif

#define FORCE_INLINE __forceinline

// ************************************************************************
// Numeric Types

using int8    = int8_t             ;  //  8-bit integer
using int32   = int32_t            ;  // 32-bit integer
using int64   = int64_t            ;  // 64-bit integer
using uint8   = uint8_t            ;  //  8-bit unsigned integer
using uint32  = uint32_t           ;  // 32-bit unsigned integer
using uint64  = uint64_t           ;  // 64-bit unsigned integer
using float32 = float              ;  // 32-bit floating point number
using float64 = double             ;  // 64-bit floating point number

#define LE_SMALL_NUMBER         1e-8
#define LE_KINDA_SMALL_NUMBER   1e-4

// ************************************************************************

class FNonCopyable
{
public:
    FNonCopyable() = default;
    virtual ~FNonCopyable() = default;

    // Disallow copying
    FNonCopyable(const FNonCopyable&) = delete;
    FNonCopyable& operator=(const FNonCopyable&) = delete;

    // allow moving
    FNonCopyable(FNonCopyable&&) = default;
    FNonCopyable& operator=(FNonCopyable&&) = default;
};

// ************************************************************************
// String

class CORE_API FString
{
    using UWString = std::string ; // Unwrapped String, temporary use std::string

public:
    FString();
    FString(const UWString& InString);
    FString(const char* InString);
    FString(const FString& Other);
    FString(FString&& Other) noexcept;
    ~FString();

    FString& operator=(const FString& Other);
    FString& operator=(FString&& Other) noexcept;

public:
    const char* operator*() const;
    const char* GetData() const;

    bool IsEmpty() const;
    int32 Length() const;

public:
    FString operator+(const FString& Other) const;
    FString& operator+=(const FString& Other);

    bool operator==(const FString& Other) const;
    bool operator!=(const FString& Other) const;

    friend CORE_API std::ostream& operator<<(std::ostream& Os, const FString& Str);
    
private:
    UWString Real;
};

// ***********************************************************************************************
// ********************************** Regular Math Calc ******************************************
// ***********************************************************************************************

namespace FMath
{

    inline float32 Sqrt(float32 Value)
    {
        return std::sqrt(Value);
    }
}

namespace FMath
{

    template<typename T, int32 N>
    struct TVectorData
    {
        T Data[N];
    };

    template<typename T>
    struct TVectorData<T, 2>
    {
        union 
        {
            T Data[2];
            struct { T x, y; };
            struct { T r, g; };
            struct { T s, t; };
            struct { T u, v; };
        };
    };

    template<typename T>
    struct TVectorData<T, 3>
    {
        union 
        {
            T Data[3];
            struct { T x, y, z; };
            struct { T r, g, b; };
            struct { T s, t, p; };
        };
    };

    template<typename T>
    struct TVectorData<T, 4>
    {
        union 
        {
            T Data[4];
            struct { T x, y, z, w; };
            struct { T r, g, b, a; };
        };
    };

    template<typename T, int32 N>
    class CORE_API TVector : public TVectorData<T, N>
    {
        static_assert(
            std::is_same<T, float32>::value || std::is_same<T, float64>::value, 
            "TVector instantiation failed: T must be float32 or float64"
            );
        static_assert(
            0 < N && N <= 4,
            "TVector error: Dimension N must be <= 4"
            );

    public:
        TVector()
        {
            for (int32 i = 0; i < N; ++i)
            {
                Data[i] = static_cast<T>(0);
            }
        }

        explicit TVector(T Scalar)
        {
            for (int32 i = 0; i < N; ++i)
            {
                Data[i] = Scalar[i];
            }
        }

        template<typename... Args, typename std::enable_if<sizeof...(Args) == N, int32>::type = 0>
        TVector(Args... args) : TVectorData<T, N>{ static_cast<T>(args)... } {}

    private:
        using TVectorData<T, N>::Data;

    public:
        T& operator[](int32 Index)
        {
            assert(0 < Index && Index < N);
            return Data[Index];
        }

        const T& operator[](int32 Index) const
        {
            assert(0 < Index && Index < N);
            return Data[Index];
        }

        TVector& operator+=(const TVector& Other) {
            for (int i = 0; i < N; ++i)
                Data[i] += Other.Data[i];
            return *this;
        }

        TVector& operator-=(const TVector& Other) {
            for (int i = 0; i < N; ++i)
                Data[i] -= Other.Data[i];
            return *this;
        }

        TVector& operator*=(T scalar) {
            for (int i = 0; i < N; ++i)
                Data[i] *= scalar;
            return *this;
        }
        
        TVector& operator*=(const TVector& Other) {
            for (int i = 0; i < N; ++i)
                Data[i] *= Other.Data[i];
            return *this;
        }

        T SizeSqr() const {
            T sum = 0;
            for (int i = 0; i < N; ++i)
                sum += Data[i] * Data[i];
            return sum;
        }

        T Size() const {
            return Sqrt(this->SizeSqr());
        }

        TVector Normalized() const {
            T Size = Size();
            if (Size < LE_SMALL_NUMBER)
                return TVector(0);
            TVector Result;
            T InvSize = 1.0 / Size;
            for (int i=0; i<N; ++i)
                Result.Data[i] = Data[i] * InvSize;
            return Result;
        }

        TVector Perp() const
        {
            return TVector(this->y, -this->x);
        }

        TVector PerpCCW() const
        {
            return TVector(this->y, -this->x);
        }

        static T Dot(const TVector& A, const TVector& B) {
            T Sum = 0;
            for (int i = 0; i < N; ++i)
                Sum += A.Data[i] * B.Data[i];
            return Sum;
        }

        static TVector Cross(const TVector& A, const TVector& B) {
            assert(N == 3, "Cross product is only defined for 3D vectors");
            return TVector(
                A.y * B.z - A.z * B.y,
                A.z * B.x - A.x * B.z,
                A.x * B.y - A.y * B.x
            );
        }

        static T Cross2D(const TVector& A, const TVector& B) {
            assert(N == 2, "Cross2D product is only defined for 2D vectors");
            return (A.x * B.y) - (A.y * B.x);
        }
    };

    template<typename T, int N>
    TVector<T, N> operator+(TVector<T, N> Lhs, const TVector<T, N>& Rhs) {
        Lhs += Rhs;
        return Lhs;
    }

    template<typename T, int N>
    TVector<T, N> operator-(TVector<T, N> Lhs, const TVector<T, N>& Rhs) {
        Lhs -= Rhs;
        return Lhs;
    }

    template<typename T, int N>
    TVector<T, N> operator*(TVector<T, N> Vector, T Scalar) {
        Vector *= Scalar;
        return Vector;
    }

    template<typename T, int N>
    TVector<T, N> operator*(T Scalar, TVector<T, N> Vector) {
        Vector *= Scalar;
        return Vector;
    }
}

using FVector2f = FMath::TVector<float32, 2>;
using FVector3f = FMath::TVector<float32, 3>;
using FVector4f = FMath::TVector<float32, 4>;
using FVector2d = FMath::TVector<float64, 2>;
using FVector3d = FMath::TVector<float64, 3>;
using FVector4d = FMath::TVector<float64, 4>;
using FVector   = FVector4f;

namespace FMath
{

    template<typename T, int32 N>
    struct TMatrixData
    {
        T Data[N * N];
    };

    template<typename T>
    struct TMatrixData<T, 2>
    {
        union
        {
            T Data[4];
            T M[2][2]; // M[Row][Col]
            struct 
            {
                T m00, m01;
                T m10, m11;
            };
        };
    };

    template<typename T>
    struct TMatrixData<T, 3>
    {
        union
        {
            T Data[9];
            T M[3][3];
            struct 
            {
                T m00, m01, m02;
                T m10, m11, m12;
                T m20, m21, m22;
            };
        };
    };

    // 4x4 特化
    template<typename T>
    struct TMatrixData<T, 4>
    {
        union
        {
            T Data[16];
            T M[4][4];
            struct 
            {
                T m00, m01, m02, m03; // Row 0
                T m10, m11, m12, m13; // Row 1
                T m20, m21, m22, m23; // Row 2
                T m30, m31, m32, m33; // Row 3
            };
        };
    };

    template<typename T, int32 N>
    class CORE_API TMatrix : public TMatrixData<T, N>
    {
        static_assert(std::is_same<T, float32>::value || std::is_same<T, float64>::value, "TMatrix: T must be float type");

    public:
        using TMatrixData<T, N>::Data;
        using TMatrixData<T, N>::M;

        TMatrix() = default;

        explicit TMatrix(T Scalar)
        {
            SetIdentity(Scalar);
        }

        TMatrix(const T* InData)
        {
            for (int32 i = 0; i < N * N; ++i)
                Data[i] = InData[i];
        }

        static TMatrix Identity()
        {
            TMatrix Result;
            Result.SetIdentity(1);
            return Result;
        }

        static TMatrix Zero()
        {
            TMatrix Result;
            for (int32 i = 0; i < N * N; ++i)
                Result.Data[i] = 0;
            return Result;
        }

        void SetIdentity(T Scale = 1)
        {
            for (int32 r = 0; r < N; ++r)
                for (int32 c = 0; c < N; ++c)
                    M[r][c] = (r == c) ? Scale : static_cast<T>(0);
        }

        TMatrix Transposed() const
        {
            TMatrix Result;
            for (int32 r = 0; r < N; ++r)
                for (int32 c = 0; c < N; ++c)
                    Result.M[r][c] = M[c][r];
            return Result;
        }

        TMatrix operator*(const TMatrix& Other) const
        {
            TMatrix Result;
            for (int32 r = 0; r < N; ++r)
            {
                for (int32 c = 0; c < N; ++c)
                {
                    T Sum = 0;
                    for (int32 k = 0; k < N; ++k)
                    {
                        Sum += M[r][k] * Other.M[k][c];
                    }
                    Result.M[r][c] = Sum;
                }
            }
            return Result;
        }

        TMatrix operator*(T Scalar) const
        {
            TMatrix Result;
            for (int32 i = 0; i < N * N; ++i)
                Result.Data[i] = Data[i] * Scalar;
            return Result;
        }

        TVector<T, N> operator*(const TVector<T, N>& Vec) const
        {
            TVector<T, N> Result;
            for (int32 r = 0; r < N; ++r)
            {
                T Sum = 0;
                for (int32 c = 0; c < N; ++c)
                {
                    Sum += M[r][c] * Vec[c];
                }
                Result[r] = Sum;
            }
            return Result;
        }

        T* operator[](int32 RowIndex) 
        { 
            return M[RowIndex]; 
        }
        
        const T* operator[](int32 RowIndex) const 
        { 
            return M[RowIndex]; 
        }
    };
}

using FMatrix2f = FMath::TMatrix<float32, 2>;
using FMatrix3f = FMath::TMatrix<float32, 3>;
using FMatrix4f = FMath::TMatrix<float32, 4>;
using FMatrix2d = FMath::TMatrix<float64, 2>;
using FMatrix3d = FMath::TMatrix<float64, 3>;
using FMatrix4d = FMath::TMatrix<float64, 4>;

namespace FMath
{

    template<typename T>
    struct TQuatData
    {
        union 
        {
            T Data[4];
            struct { T x, y, z, w; };
        };
    };

    template<typename T>
    class CORE_API TQuaternion : public TQuatData<T>
    {
        static_assert(
            std::is_same<T, float32>::value || std::is_same<T, float64>::value, 
            "TQuaternion instantiation failed: T must be float32 or float64"
            );

    public:
        using TQuatData<T>::Data;
        using TQuatData<T>::x;
        using TQuatData<T>::y;
        using TQuatData<T>::z;
        using TQuatData<T>::w;

        TQuaternion() : x(0), y(0), z(0), w(1)
        {
        }

        TQuaternion(T InX, T InY, T InZ, T InW) : x(InX), y(InY), z(InZ), w(InW)
        {
        }

        TQuaternion(const TVector<T, 3>& Axis, T AngleRad)
        {
            const T HalfAngle = AngleRad * 0.5f;
            const T SinHalf = FMath::Sin(HalfAngle);
            const T CosHalf = FMath::Cos(HalfAngle);

            this->x = Axis.x * SinHalf;
            this->y = Axis.y * SinHalf;
            this->z = Axis.z * SinHalf;
            this->z = CosHalf;
        }

        static TQuaternion Identity()
        {
            return TQuaternion(0, 0, 0, 1);
        }

    public:
        TQuaternion operator*(const TQuaternion& Other) const
        {
            TQuaternion Result;
            Result.w = w * Other.w - x * Other.x - y * Other.y - z * Other.z;
            Result.x = w * Other.x + x * Other.w + y * Other.z - z * Other.y;
            Result.y = w * Other.y - x * Other.z + y * Other.w + z * Other.x;
            Result.z = w * Other.z + x * Other.y - y * Other.x + z * Other.w;
            return Result;
        }

        TVector<T, 3> RotateVector(const TVector<T, 3>& V) const
        {
            TVector<T, 3> QVec(x, y, z);
            TVector<T, 3> UV = TVector<T, 3>::Cross(QVec, V); 
            TVector<T, 3> UUV = TVector<T, 3>::Cross(QVec, UV); 

            UV *= (static_cast<T>(2) * w);
            UUV *= static_cast<T>(2);
            return V + UV + UUV;
        }

        TVector<T, 3> operator*(const TVector<T, 3>& V) const
        {
            return RotateVector(V);
        }

        void Normalize()
        {
            T LengthSq = x*x + y*y + z*z + w*w;
            if (LengthSq < 1e-8) {
                *this = Identity();
                return;
            }
            T InvLen = static_cast<T>(1) / std::sqrt(LengthSq);
            x *= InvLen;
            y *= InvLen;
            z *= InvLen;
            w *= InvLen;
        }
        
        TQuaternion Inverse() const
        {
            return TQuaternion(-x, -y, -z, w);
        }
    };
}
