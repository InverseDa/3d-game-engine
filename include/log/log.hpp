#ifndef VULKAN_LIB_LOG_HPP
#define VULKAN_LIB_LOG_HPP

#include <iostream>
#include <format>
#include "fmt/core.h"

enum LOG_LEVEL {
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR
};

namespace IO {
template <typename... Args>
inline void PrintLog(LOG_LEVEL level, const fmt::format_string<Args...>& fmt, Args&&... args) {
    switch (level) {
    case LOG_LEVEL_INFO:
        fmt::print("[INFO] ");
        break;
    case LOG_LEVEL_WARNING:
        fmt::print("[WARNING] ");
        break;
    case LOG_LEVEL_ERROR:
        fmt::print("[ERROR] ");
        break;
    }
    fmt::print(fmt, std::forward<Args>(args)...);
    fmt::print("\n");
}

template <typename... Args>
inline void ThrowError(const fmt::format_string<Args...>& fmt, Args&&... args) {
    auto msg = fmt::format(fmt, std::forward<Args>(args)...);
    msg = "[ERROR] " + msg;
    fmt::print("{}\n", msg);
    throw std::runtime_error(msg);
}

template <typename... Args>
inline void Assert(bool condition, const fmt::format_string<Args...>& fmt, Args&&... args) {
    if (!condition) {
        ThrowError(fmt, std::forward<Args>(args)...);
    }
}

} // namespace IO

#endif // VULKAN_LIB_LOG_HPP
