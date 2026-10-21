#pragma once

#include <string>
#include <string_view>

namespace gth::core {

// Mirrors the NAS & HDD Health Monitor spec's Section 29 exactly. Unknown
// is a first-class state, not a fallback for "0%" - see the spec's Section
// 71: "Unknown must never become 0%", a provider that couldn't get real
// data reports Unknown, not Critical/Offline.
enum class DiskStatus { Healthy = 0, Warning = 1, Critical = 2, Offline = 3, Unknown = 4 };

std::string toString(DiskStatus status);
DiskStatus diskStatusFromString(std::string_view text);

}  // namespace gth::core
