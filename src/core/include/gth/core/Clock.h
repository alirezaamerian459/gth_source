#pragma once

#include <string>

namespace gth::core::Clock {

// Current UTC time formatted as ISO-8601 ("2026-09-06T18:12:36Z"), the
// representation stored for every timestamp column in the database.
std::string utcNowIso8601();

}  // namespace gth::core::Clock
