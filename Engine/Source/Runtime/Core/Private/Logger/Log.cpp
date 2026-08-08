#include "Logger/Log.h"

#include <spdlog/pattern_formatter.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>

#include <memory>
#include <string>
#include <vector>

#if PLATFORM_WINDOWS
#include <spdlog/details/windows_include.h>
#endif

namespace LE {

spdlog::logger* s_CoreLogger = nullptr;
std::vector<spdlog::sink_ptr> s_Sinks;

class LevelTagFormatter final : public spdlog::custom_flag_formatter
{
public:
    void format(const spdlog::details::log_msg& msg, const std::tm&, spdlog::memory_buf_t& dest) override
    {
        const char* tag = "INFO";
        switch (msg.level)
        {
        case spdlog::level::trace:    tag = "Trace";   break;
        case spdlog::level::debug:    tag = "Debug";   break;
        case spdlog::level::info:     tag = "Info";    break;
        case spdlog::level::warn:     tag = "WARNING"; break;
        case spdlog::level::err:      tag = "ERROR";   break;
        case spdlog::level::critical: tag = "FATAL";   break;
        case spdlog::level::off:      tag = "OFF";     break;
        default:                      tag = "Info";    break;
        }

        const size_t len = std::char_traits<char>::length(tag);
        dest.push_back('[');
        dest.append(tag, tag + len);
        dest.push_back(']');
    }

    std::unique_ptr<spdlog::custom_flag_formatter> clone() const override
    {
        return std::make_unique<LevelTagFormatter>();
    }
};

class LevelAnsiBeginFormatter final : public spdlog::custom_flag_formatter
{
public:
    void format(const spdlog::details::log_msg& msg, const std::tm&, spdlog::memory_buf_t& dest) override
    {
        const char* code = "";
        switch (msg.level)
        {
        case spdlog::level::warn: code = "\x1b[33;1m"; break; // yellow
        case spdlog::level::err:
        case spdlog::level::critical: code = "\x1b[31;1m"; break; // red
        default: break;
        }

        if (code[0] != '\0')
        {
            dest.append(code, code + std::char_traits<char>::length(code));
        }
    }

    std::unique_ptr<spdlog::custom_flag_formatter> clone() const override
    {
        return std::make_unique<LevelAnsiBeginFormatter>();
    }
};

class LevelAnsiEndFormatter final : public spdlog::custom_flag_formatter
{
public:
    void format(const spdlog::details::log_msg& msg, const std::tm&, spdlog::memory_buf_t& dest) override
    {
        if (msg.level == spdlog::level::warn || msg.level == spdlog::level::err || msg.level == spdlog::level::critical)
        {
            constexpr const char* reset = "\x1b[0m";
            dest.append(reset, reset + std::char_traits<char>::length(reset));
        }
    }

    std::unique_ptr<spdlog::custom_flag_formatter> clone() const override
    {
        return std::make_unique<LevelAnsiEndFormatter>();
    }
};

void Log::Init()
{
#if PLATFORM_WINDOWS
    HANDLE hOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != nullptr && hOut != INVALID_HANDLE_VALUE)
    {
        DWORD mode = 0;
        if (::GetConsoleMode(hOut, &mode) != 0)
        {
            ::SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
#endif

    auto consoleSink = std::make_shared<spdlog::sinks::stdout_sink_mt>();

    auto makeFormatter = [](const std::string& pattern)
    {
        auto formatter = std::make_unique<spdlog::pattern_formatter>();
        formatter->add_flag<LevelAnsiBeginFormatter>('K');
        formatter->add_flag<LevelAnsiEndFormatter>('k');
        formatter->add_flag<LevelTagFormatter>('q');
        formatter->set_pattern(pattern);
        return formatter;
    };

    consoleSink->set_formatter(makeFormatter("%K[%T] [%n] %q: %v%k"));

    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("Firefly.log", true);
    fileSink->set_formatter(makeFormatter("[%T] [%n] %q: %v"));

    s_Sinks = { consoleSink, fileSink };

    std::shared_ptr<spdlog::logger> CoreLogger = std::make_shared<spdlog::logger>("CORE", begin(s_Sinks), end(s_Sinks));
    spdlog::register_logger(CoreLogger);
    s_CoreLogger = CoreLogger.get();
    s_CoreLogger->set_level(spdlog::level::trace);
    s_CoreLogger->flush_on(spdlog::level::trace);

    spdlog::apply_all([&](std::shared_ptr<spdlog::logger> l) {
        if (l->sinks().empty())
        {
            l->sinks() = s_Sinks;
            l->set_level(spdlog::level::trace);
        }
    });
}

spdlog::logger* Log::GetCoreLogger()
{
    return s_CoreLogger;
}

spdlog::logger* Log::GetLoggerOrCreate(const StringView Name)
{
    // spdlog owns its registry keys and requires std::string-compatible input;
    // keep that ownership at this exact third-party boundary.
    const std::string name(Name.Data() == nullptr ? "" : Name.Data(), Name.Size());
    auto logger = spdlog::get(name);
    if (logger) {
        return logger.get();
    }

    logger = std::make_shared<spdlog::logger>(name, begin(s_Sinks), end(s_Sinks));
    spdlog::register_logger(logger);
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::trace);
    return logger.get();
}
}
