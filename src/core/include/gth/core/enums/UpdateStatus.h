#pragma once

#include <string>
#include <string_view>

namespace gth::core {

// Result of comparing a GameVariant's current (IsCurrent) GameVersion
// against what an IUpdateSource reports as latest. NoSafeMatch means the
// source couldn't confirm it was looking at the same title/region/platform
// with confidence - per the spec's "Exact Variant Matching" rule, an update
// must never proceed on an uncertain match.
enum class UpdateStatus {
    Unknown = 0,
    UpToDate = 1,
    UpdateAvailable = 2,
    NoSafeMatch = 3
};

std::string toString(UpdateStatus status);
UpdateStatus updateStatusFromString(std::string_view text);

}  // namespace gth::core
