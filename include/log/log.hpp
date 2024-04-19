#ifndef VULKAN_LIB_LOG_HPP
#define VULKAN_LIB_LOG_HPP

#include <iostream>
#include <format>
#include "spdlog/spdlog.h"

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
        spdlog::info(fmt, std::forward<Args>(args)...);
        break;
    case LOG_LEVEL_WARNING:
        spdlog::warn(fmt, std::forward<Args>(args)...);
        break;
    case LOG_LEVEL_ERROR:
        spdlog::error(fmt, std::forward<Args>(args)...);
        break;
    }
}

template <typename... Args>
inline void ThrowError(const fmt::format_string<Args...>& fmt, Args&&... args) {
    // 改成spdlog
    spdlog::error(fmt, std::forward<Args>(args)...);
    throw std::runtime_error(fmt::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
inline void Assert(bool condition, const fmt::format_string<Args...>& fmt, Args&&... args) {
    if (!condition) {
        ThrowError(fmt, std::forward<Args>(args)...);
    }
}

} // namespace IO

#endif // VULKAN_LIB_LOG_HPP
