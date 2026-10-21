#pragma once

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace gth::application {

// Collecting Sony links game by game: the game to work on next. The first of `games` (title ID, title) - in the
// order given - that has no link yet and hasn't been skipped; nothing when every game is covered or skipped.
inline std::optional<std::pair<std::string, std::string>> NextGameNeedingLink(
    const std::vector<std::pair<std::string, std::string>>& games, const std::set<std::string>& knownTitleIds,
    const std::set<std::string>& skippedTitleIds) {
    for (const auto& game : games) {
        if (knownTitleIds.contains(game.first) || skippedTitleIds.contains(game.first)) continue;
        return game;
    }
    return std::nullopt;
}

}  // namespace gth::application
