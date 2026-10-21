#pragma once

#include <string>

#include "gth/core/Clock.h"
#include "gth/core/Guid.h"

namespace gth::core {

// A logical game title, grouping every region/platform/version variant
// indexed under it (e.g. "Spider-Man 2" groups its Europe, USA, and Japan
// GameVariant rows).
struct Game {
    std::string gameId = Guid::newGuid();
    std::string title;

    std::string createdAt = Clock::utcNowIso8601();
    std::string updatedAt = Clock::utcNowIso8601();
};

}  // namespace gth::core
