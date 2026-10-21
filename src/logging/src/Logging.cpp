#include "gth/logging/Logging.h"

#include <vector>

#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>

namespace gth::logging {

namespace {
std::shared_ptr<spdlog::logger> g_logger;
}

void init(const std::filesystem::path& logDirectory) {
    std::filesystem::create_directories(logDirectory);

    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::daily_file_sink_mt>(
        (logDirectory / "log.txt").string(), 0, 0));
#ifdef _DEBUG
    sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
#endif

    g_logger = std::make_shared<spdlog::logger>("gth", sinks.begin(), sinks.end());
    g_logger->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] %v");
    g_logger->set_level(spdlog::level::info);
    g_logger->flush_on(spdlog::level::info);

    spdlog::set_default_logger(g_logger);
}

std::shared_ptr<spdlog::logger>& logger() { return g_logger; }

}  // namespace gth::logging
