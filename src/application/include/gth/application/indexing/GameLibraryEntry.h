#pragma once

#include <string>

#include "gth/core/enums/GameVariantStatus.h"

namespace gth::application {

// Flat, display-ready projection of a Game + one of its GameVariants + that
// variant's current GameVersion, for the Games list. Keeps the UI from
// having to join three entities itself.
struct GameLibraryEntry {
    std::string gameId;
    std::string title;

    std::string gameVariantId;
    std::string platform;
    std::string region;
    core::GameVariantStatus variantStatus = core::GameVariantStatus::Unknown;

    std::string currentVersion;  // empty if this variant has no Active version
    long long currentVersionSizeBytes = 0;

    // "PPSA02343" - read from the current version's Content ID; empty when
    // the release has none. What the tracker sites list a game under.
    std::string titleId;
};

}  // namespace gth::application
