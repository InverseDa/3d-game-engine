#pragma once

#include <memory>
#include <string>

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

class Log
{
public:
    static void Init();
    static std::shared_ptr<spdlog::logger>& GetCoreLogger();
    static std::shared_ptr<spdlog::logger>  GetLoggerOrCreate(const std::string& Name);
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
    extern std::shared_ptr<spdlog::logger> CategoryName

#define LE_DECLARE_LOG_CATEGORY(CategoryName) \
    std::shared_ptr<spdlog::logger> CategoryName = LE::Log::GetLoggerOrCreate(#CategoryName)

#define LE_LOG(CategoryName, Level, ...) \
    if (CategoryName) { \
        CategoryName->log(LE::LogLevelToSpdlog(LE::LogLevel::Level), __VA_ARGS__); \
    }

#define LE_SHUTDOWN() \
    spdlog::shutdown();
