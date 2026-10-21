#include "gth/core/enums/GameVariantStatus.h"

namespace gth::core {

std::string toString(GameVariantStatus status) {
    switch (status) {
        case GameVariantStatus::Active: return "Active";
        case GameVariantStatus::Missing: return "Missing";
        case GameVariantStatus::Unknown:
        default: return "Unknown";
    }
}

GameVariantStatus gameVariantStatusFromString(std::string_view text) {
    if (text == "Active") return GameVariantStatus::Active;
    if (text == "Missing") return GameVariantStatus::Missing;
    return GameVariantStatus::Unknown;
}

}  // namespace gth::core
