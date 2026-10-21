#pragma once

#include <string>
#include <string_view>

namespace gth::core {

// Whether a previously indexed game variant's folder is still present on its
// storage. Mirrors the "never silently delete" rule used for Storage: a
// variant that disappears from a scan is marked Missing, not removed - the
// user decides what to do about a folder that vanished.
enum class GameVariantStatus {
    Unknown = 0,
    Active = 1,
    Missing = 2
};

std::string toString(GameVariantStatus status);
GameVariantStatus gameVariantStatusFromString(std::string_view text);

}  // namespace gth::core
