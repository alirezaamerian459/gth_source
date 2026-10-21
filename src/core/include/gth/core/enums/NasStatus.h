#pragma once

#include <string>
#include <string_view>

namespace gth::core {

// Mirrors the NAS & HDD Health Monitor spec's Section 30 exactly.
enum class NasStatus { Online = 0, Warning = 1, Offline = 2, Unreachable = 3, AuthenticationFailed = 4 };

std::string toString(NasStatus status);
NasStatus nasStatusFromString(std::string_view text);

}  // namespace gth::core
