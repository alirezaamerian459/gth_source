#pragma once

#include <filesystem>
#include <memory>

#include <spdlog/spdlog.h>

namespace gth::logging {

// Initializes the process-wide logger: a daily rolling file under
// `logDirectory` plus (in debug builds) output to the Visual Studio debugger
// - the closest native equivalent to the console+rolling-file sink pair a
// server-side app would use, adapted for a GUI process with no console.
void init(const std::filesystem::path& logDirectory);

// The shared logger created by init(). Valid only after init() has run.
std::shared_ptr<spdlog::logger>& logger();

}  // namespace gth::logging

#define GTH_LOG_DEBUG(...) ::gth::logging::logger()->debug(__VA_ARGS__)
#define GTH_LOG_INFO(...) ::gth::logging::logger()->info(__VA_ARGS__)
#define GTH_LOG_WARN(...) ::gth::logging::logger()->warn(__VA_ARGS__)
#define GTH_LOG_ERROR(...) ::gth::logging::logger()->error(__VA_ARGS__)
