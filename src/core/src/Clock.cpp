#include "gth/core/Clock.h"

#include <chrono>
#include <format>

namespace gth::core::Clock {

std::string utcNowIso8601() {
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);
}

}  // namespace gth::core::Clock
