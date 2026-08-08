#pragma once

#include "Containers/String.h"
#include "Math/Matrix.h"
#include "Math/Quaternion.h"
#include "Math/Vector.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>

#if defined(_MSC_VER)
    #define FORCE_INLINE __forceinline
#else
    #define FORCE_INLINE inline __attribute__((always_inline))
#endif

namespace LE
{

using int8 = std::int8_t;
using int32 = std::int32_t;
using int64 = std::int64_t;
using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
using float32 = float;
using float64 = double;

#define LE_SMALL_NUMBER 1e-8
#define LE_KINDA_SMALL_NUMBER 1e-4

class FNonCopyable
{
public:
    FNonCopyable() = default;
    virtual ~FNonCopyable() = default;

    FNonCopyable(const FNonCopyable&) = delete;
    FNonCopyable& operator=(const FNonCopyable&) = delete;
    FNonCopyable(FNonCopyable&&) = default;
    FNonCopyable& operator=(FNonCopyable&&) = default;
};

} // namespace LE
