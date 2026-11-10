#include "gth/logging/Logging.h"

#include <vector>

#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>

namespace gth::logging {

namespace {
std::shared_ptr<spdlog::logger>& loggerInstance() {
    static std::shared_ptr<spdlog::logger> instance;
    return instance;
}
}

void init(const std::filesystem::path& logDirectory) {
    std::filesystem::create_directories(logDirectory);

    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::daily_file_sink_mt>(
        (logDirectory / "log.txt").string(), 0, 0));
#ifdef _DEBUG
    sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
#endif

    auto& currentLogger = loggerInstance();
    currentLogger = std::make_shared<spdlog::logger>("gth", sinks.begin(), sinks.end());
    currentLogger->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] %v");
    currentLogger->set_level(spdlog::level::info);
    currentLogger->flush_on(spdlog::level::info);

    spdlog::set_default_logger(currentLogger);
}

std::shared_ptr<spdlog::logger>& logger() { return loggerInstance(); }

}  // namespace gth::logging
