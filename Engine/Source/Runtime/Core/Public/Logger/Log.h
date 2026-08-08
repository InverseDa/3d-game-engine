#pragma once

#include "Containers/StringView.h"

#include <spdlog/common.h>
#include <spdlog/spdlog.h> 
#include <spdlog/fmt/ostr.h>

namespace spdlog {
    class logger;
}

// 引擎名：LE，Limitless Engine
namespace LE {

enum class LogLevel {
    Trace = 0,
    Info,
    Warn,
    Error,
    Fatal,
};

class CORE_API Log
{
public:
    static void Init();
    // Borrowed from spdlog's registry. The registry owns the logger until
    // LE_SHUTDOWN/spdlog::shutdown; callers must not retain or use it after shutdown.
    static spdlog::logger* GetCoreLogger();
    static spdlog::logger* GetLoggerOrCreate(StringView Name);
};

inline spdlog::level::level_enum LogLevelToSpdlog(LogLevel Level) {
    switch (Level) {
        case LogLevel::Trace: return spdlog::level::trace;
        case LogLevel::Info:  return spdlog::level::info;
        case LogLevel::Warn:  return spdlog::level::warn;
        case LogLevel::Error: return spdlog::level::err;
        case LogLevel::Fatal: return spdlog::level::critical;
    }
    return spdlog::level::info;
}
}

#define LE_INIT() \
    LE::Log::Init();

#define LE_DECLARE_LOG_CATEGORY_EXTERN(CategoryName) \
    extern spdlog::logger* CategoryName

#define LE_DECLARE_LOG_CATEGORY(CategoryName) \
    spdlog::logger* CategoryName = LE::Log::GetLoggerOrCreate(#CategoryName)

#define LE_LOG(CategoryName, Level, ...) \
    if (CategoryName) { \
        CategoryName->log(LE::LogLevelToSpdlog(LE::LogLevel::Level), __VA_ARGS__); \
    }

#define LE_SHUTDOWN() \
    spdlog::shutdown();
