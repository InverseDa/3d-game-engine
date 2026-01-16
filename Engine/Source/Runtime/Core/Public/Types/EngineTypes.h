#pragma once

#include <string>

#define     LE_USE_GLM      1

#if LE_USE_GLM
#include "glm/glm.hpp"
#endif

// ************************************************************************
// Numeric Types

typedef     int                     int32;
typedef     long long               int64;   // 64-bit integer
typedef     unsigned int            uint32;  // 32-bit unsigned integer
typedef     unsigned long long      uint64;  // 64-bit unsigned integer
typedef     float                   float32;   // 32-bit floating point number
typedef     double                  float64;   // 64-bit floating point number

// ************************************************************************
// String

typedef     std::string     FString;

#if LE_USE_GLM
typedef     glm::vec4       FVector;
typedef     glm::mat4       FMatrix;
typedef     glm::mat3       FMatrix3x3;
#endif
