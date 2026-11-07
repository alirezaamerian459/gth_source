#pragma once

#include <optional>
#include <string>

#include "gth/core/entities/GameVersion.h"
#include "gth/core/enums/GameVariantStatus.h"
#include "gth/core/index/IGameVersionRepository.h"

namespace gth::application {

// The row the Games list shows for a variant: its current Active version, or
// failing that the first Active one.
inline std::optional<core::GameVersion> currentActiveVersion(core::IGameVersionRepository& versions,
                                                              const std::string& gameVariantId) {
    std::optional<core::GameVersion> fallback;
    for (auto& version : versions.getByVariantId(gameVariantId)) {
        if (version.status != core::GameVariantStatus::Active) continue;
        if (version.isCurrent) return version;
        if (!fallback.has_value()) fallback = std::move(version);
    }
    return fallback;
}

}  // namespace gth::application
