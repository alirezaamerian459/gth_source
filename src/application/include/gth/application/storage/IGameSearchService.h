#pragma once

#include <optional>
#include <string>
#include <vector>

#include "gth/core/entities/Game.h"
#include "gth/core/entities/GameVariant.h"
#include "gth/core/entities/GameVersion.h"

namespace gth::application {

// Result of a game search - contains the matched game and its current version
struct GameSearchResult {
    core::Game game;
    core::GameVariant variant;
    core::GameVersion version;
    std::string fullPath;  // Full path to the version folder on storage
};

// Searches for games in local storage by name/title, enabling automatic
// detection of existing games before falling back to download links
class IGameSearchService {
public:
    virtual ~IGameSearchService() = default;

    // Search for a game by partial title match (case-insensitive)
    // Returns the best match if found, empty optional if no match
    virtual std::optional<GameSearchResult> searchByTitle(const std::string& partialTitle) = 0;

    // Search for a game by exact title match
    virtual std::optional<GameSearchResult> searchByExactTitle(const std::string& title) = 0;

    // Search for a game by platform and region (e.g., "PS5", "USA")
    virtual std::vector<GameSearchResult> searchByPlatformAndRegion(const std::string& platform,
                                                                     const std::string& region) = 0;

    // Extract game title from a CDN URL (e.g., from PSN URL structure)
    // Returns the extracted title if recognizable, empty string otherwise
    virtual std::string extractTitleFromUrl(const std::string& url) = 0;
};

}  // namespace gth::application
