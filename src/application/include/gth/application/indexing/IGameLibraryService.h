#pragma once

#include <vector>

#include "gth/application/indexing/GameLibraryEntry.h"

namespace gth::application {

// Read side of the Game Library, for the UI: one row per reachable
// (Game, GameVariant) with its current version resolved. Indexed variants on
// an offline removable storage stay persisted but are omitted until the
// storage becomes available again. Writing to the library happens through
// IGameIndexer, not through this interface.
class IGameLibraryService {
public:
    virtual ~IGameLibraryService() = default;
    virtual std::vector<GameLibraryEntry> getAllEntries() = 0;
};

}  // namespace gth::application
