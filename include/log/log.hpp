#ifndef VULKAN_LIB_LOG_HPP
#define VULKAN_LIB_LOG_HPP

#include <iostream>
#include <format>
#include "spdlog/spdlog.h"

enum LogLevel {
    info,
    warning,
    error
};

namespace IO {
template <LogLevel level, typename... Args>
inline void PrintLog(const fmt::format_string<Args...>& fmt, Args&&... args) {
    switch (level) {
    case info:
        spdlog::info(fmt, std::forward<Args>(args)...);
        break;
    case warning:
        spdlog::warn(fmt, std::forward<Args>(args)...);
        break;
    case error:
        spdlog::error(fmt, std::forward<Args>(args)...);
        break;
    }
}

template <typename... Args>
inline void PrintLog(LogLevel level, const fmt::format_string<Args...>& fmt, Args&&... args) {
    switch (level) {
    case info:
        spdlog::info(fmt, std::forward<Args>(args)...);
        break;
    case warning:
        spdlog::warn(fmt, std::forward<Args>(args)...);
        break;
    case error:
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
