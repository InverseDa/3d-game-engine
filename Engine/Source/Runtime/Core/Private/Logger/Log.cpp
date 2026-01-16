#include "Logger/Log.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <vector>

namespace LE {

std::shared_ptr<spdlog::logger> s_CoreLogger;
std::vector<spdlog::sink_ptr> s_Sinks;

void Log::Init()
{
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_pattern("%^[%T] [%n] : %v%$");

    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("Firefly.log", true);
    fileSink->set_pattern("[%T] [%n] [%l]: %v");

    s_Sinks = { consoleSink, fileSink };

    s_CoreLogger = std::make_shared<spdlog::logger>("CORE", begin(s_Sinks), end(s_Sinks));
    spdlog::register_logger(s_CoreLogger);
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

std::shared_ptr<spdlog::logger>& Log::GetCoreLogger()
{
    return s_CoreLogger;
}

std::shared_ptr<spdlog::logger> Log::GetLoggerOrCreate(const std::string& name)
{
    auto logger = spdlog::get(name);
    if (logger) {
        return logger;
    }

    logger = std::make_shared<spdlog::logger>(name, begin(s_Sinks), end(s_Sinks));
    spdlog::register_logger(logger);
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::trace);
    return logger;
}
}